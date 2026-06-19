#include <Project64-plugin-spec/Video.h>

#include <GL/gl.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <memory>
#include <vector>

#include <context.hpp>
#include <device.hpp>
#include <rdp_device.hpp>

extern "C" int Project64LinuxGetDrawableSize(int * width, int * height);
extern "C" void Project64LinuxCopyHiddenRdramToRdp(void * hiddenRdram, size_t hiddenRdramSize, uint32_t rdramOffset, uint32_t byteCount);
extern "C" void Project64LinuxUpdateHiddenRdramFromRdp(const void * hiddenRdram, size_t hiddenRdramSize, uint32_t rdramOffset, uint32_t byteCount);

namespace
{
constexpr uint32_t DP_INTERRUPT = 0x20;
constexpr uint32_t DPC_STATUS_XBUS_DMEM_DMA = 0x001;
constexpr uint32_t DPC_STATUS_START_GCLK = 0x008;
constexpr uint32_t DPC_STATUS_TMEM_BUSY = 0x010;
constexpr uint32_t DPC_STATUS_PIPE_BUSY = 0x020;
constexpr uint32_t DPC_STATUS_CMD_BUSY = 0x040;
constexpr uint32_t DPC_STATUS_CBUF_READY = 0x080;
constexpr uint32_t DPC_STATUS_DMA_BUSY = 0x100;

GFX_INFO g_Gfx = {};
std::unique_ptr<Vulkan::Context> g_Context;
std::unique_ptr<Vulkan::Device> g_Device;
std::unique_ptr<RDP::CommandProcessor> g_Processor;
bool g_Running = false;
uint32_t g_CommandRead = 0;
uint32_t g_CommandWrite = 0;
uint32_t g_CommandBuffer[0x40000 >> 2] = {};
GLuint g_Texture = 0;
uint32_t g_TextureWidth = 0;
uint32_t g_TextureHeight = 0;
std::vector<uint32_t> g_Frame;
uint32_t g_ColorImageAddress = 0;
uint32_t g_ColorImageWidth = 320;
uint32_t g_ColorImageHeight = 240;
uint32_t g_ColorImageBytesPerPixel = 2;
bool g_HiddenRdramPrepared = false;
bool g_HiddenRdramDirty = false;
uint32_t g_ColorImageDumpIndex = 0;
FILE * g_CommandLog = nullptr;
uint32_t g_CommandLogIndex = 0;
uint32_t g_LastOtherModesHi = 0;
uint32_t g_LastOtherModesLo = 0;
uint32_t g_LastFillColor = 0;

const uint8_t CommandLength[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
    1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
};

uint32_t Reg(const uint32_t * Value)
{
    return Value != nullptr ? *Value : 0;
}

void SetReg(uint32_t * Value, uint32_t Data)
{
    if (Value != nullptr)
    {
        *Value = Data;
    }
}

void AddReg(uint32_t * Value, uint32_t Data)
{
    if (Value != nullptr)
    {
        *Value += Data;
    }
}

void SetStatusBits(uint32_t Bits)
{
    if (g_Gfx.DPC_STATUS_REG != nullptr)
    {
        *g_Gfx.DPC_STATUS_REG |= Bits;
    }
}

void ClearStatusBits(uint32_t Bits)
{
    if (g_Gfx.DPC_STATUS_REG != nullptr)
    {
        *g_Gfx.DPC_STATUS_REG &= ~Bits;
    }
}

void BeginDpWork()
{
    SetStatusBits(DPC_STATUS_START_GCLK | DPC_STATUS_DMA_BUSY | DPC_STATUS_CMD_BUSY | DPC_STATUS_PIPE_BUSY);
}

void RecordDpCommand(uint32_t CommandLength)
{
    AddReg(g_Gfx.DPC_CLOCK_REG, CommandLength * 8);
    AddReg(g_Gfx.DPC_BUFBUSY_REG, 1);
    AddReg(g_Gfx.DPC_PIPEBUSY_REG, CommandLength);
}

void FinishDpWork()
{
    ClearStatusBits(DPC_STATUS_DMA_BUSY | DPC_STATUS_CMD_BUSY | DPC_STATUS_PIPE_BUSY | DPC_STATUS_TMEM_BUSY);
    SetStatusBits(DPC_STATUS_CBUF_READY | DPC_STATUS_START_GCLK);
}

void RaiseDpInterrupt()
{
    if (g_Gfx.MI_INTR_REG != nullptr)
    {
        *g_Gfx.MI_INTR_REG |= DP_INTERRUPT;
    }
    if (g_Gfx.CheckInterrupts != nullptr)
    {
        g_Gfx.CheckInterrupts();
    }
}

uint32_t BytesPerPixelFromImageSize(uint32_t size)
{
    switch (size & 3)
    {
    case 0: return 1;
    case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    default: return 2;
    }
}

void TrackDpCommand(uint32_t command, uint32_t w0, uint32_t w1)
{
    if (command == 0x2F)
    {
        g_LastOtherModesHi = w0;
        g_LastOtherModesLo = w1;
    }
    if (command == 0x2D)
    {
        const uint32_t yhi = w1 & 0xFFF;
        g_ColorImageHeight = std::max<uint32_t>(1, (yhi >> 2) + 1);
    }
    if (command == 0x37)
    {
        g_LastFillColor = w1;
    }
    if (command == 0x3F)
    {
        g_ColorImageWidth = (w0 & 0xFFF) + 1;
        g_ColorImageBytesPerPixel = BytesPerPixelFromImageSize((w0 >> 19) & 3);
        g_ColorImageAddress = w1 & 0x00FFFFFF;
        g_HiddenRdramPrepared = false;
    }
}

uint32_t CurrentColorImageByteCount()
{
    uint32_t byteCount = g_ColorImageWidth * g_ColorImageBytesPerPixel * g_ColorImageHeight;
    if (g_ColorImageAddress >= g_Gfx.RDRAM_SIZE)
    {
        return 0;
    }
    if (byteCount == 0 || byteCount > (g_Gfx.RDRAM_SIZE - g_ColorImageAddress))
    {
        byteCount = g_Gfx.RDRAM_SIZE - g_ColorImageAddress;
    }
    return byteCount;
}

bool CommandWritesColorImage(uint32_t command)
{
    return (command >= 0x08 && command <= 0x0F) ||
           command == 0x24 ||
           command == 0x25 ||
           command == 0x36;
}

void OpenCommandLog()
{
    if (g_CommandLog != nullptr)
    {
        return;
    }

    const char * path = std::getenv("PJ64_PARALLEL_RDP_COMMAND_LOG");
    if (path == nullptr || *path == '\0')
    {
        return;
    }

    g_CommandLog = std::fopen(path, "w");
    if (g_CommandLog == nullptr)
    {
        std::fprintf(stderr, "parallel-rdp-pj64: failed to open command log %s: %s\n", path, std::strerror(errno));
        return;
    }

    std::fprintf(g_CommandLog, "# idx op words ci_addr ci_width ci_height ci_bpp other_hi other_lo fill_color\n");
}

void CloseCommandLog()
{
    if (g_CommandLog != nullptr)
    {
        std::fclose(g_CommandLog);
        g_CommandLog = nullptr;
    }
    g_CommandLogIndex = 0;
}

void LogCommand(uint32_t command, uint32_t commandLength, const uint32_t * words)
{
    if (g_CommandLog == nullptr)
    {
        return;
    }

    std::fprintf(g_CommandLog, "%u %02X", g_CommandLogIndex++, command);
    for (uint32_t i = 0; i < commandLength * 2; i++)
    {
        std::fprintf(g_CommandLog, " %08X", words[i]);
    }
    std::fprintf(g_CommandLog, " | ci=%06X %u %u %u other=%08X:%08X fill=%08X\n",
        g_ColorImageAddress, g_ColorImageWidth, g_ColorImageHeight, g_ColorImageBytesPerPixel * 8,
        g_LastOtherModesHi, g_LastOtherModesLo, g_LastFillColor);
}

void SyncHiddenRdramFromCore()
{
    if (!g_Processor || g_HiddenRdramPrepared)
    {
        return;
    }

    const uint32_t byteCount = CurrentColorImageByteCount();
    if (byteCount == 0)
    {
        return;
    }

    void * hiddenRdram = g_Processor->begin_write_hidden_rdram();
    if (hiddenRdram != nullptr)
    {
        Project64LinuxCopyHiddenRdramToRdp(hiddenRdram, g_Processor->get_hidden_rdram_size(), g_ColorImageAddress, byteCount);
        g_Processor->end_write_hidden_rdram();
    }
    g_HiddenRdramPrepared = true;
}

void SyncHiddenRdramToCore()
{
    if (!g_Processor || !g_HiddenRdramDirty)
    {
        return;
    }

    const uint32_t byteCount = CurrentColorImageByteCount();
    if (byteCount == 0)
    {
        return;
    }

    const void * hiddenRdram = g_Processor->begin_read_hidden_rdram();
    if (hiddenRdram != nullptr)
    {
        if (std::getenv("PJ64_PARALLEL_RDP_DEBUG_HIDDEN") != nullptr)
        {
            const uint8_t * hidden = static_cast<const uint8_t *>(hiddenRdram);
            const size_t first = g_ColorImageAddress / 2;
            size_t last = (g_ColorImageAddress + byteCount + 1) / 2;
            const size_t hiddenSize = g_Processor->get_hidden_rdram_size();
            if (last > hiddenSize)
            {
                last = hiddenSize;
            }

            size_t nonDefault = 0;
            size_t nonZero = 0;
            uint8_t firstValue = 0;
            bool haveFirst = false;
            for (size_t i = first; i < last; i++)
            {
                const uint8_t value = hidden[i] & 3;
                if (!haveFirst)
                {
                    firstValue = value;
                    haveFirst = true;
                }
                if (value != 3)
                {
                    nonDefault++;
                }
                if (value != 0)
                {
                    nonZero++;
                }
            }
            std::fprintf(stderr, "parallel-rdp-pj64 hidden sync: addr=%06X bytes=%u hidden=[%zu,%zu) first=%u nonDefault=%zu nonZero=%zu\n",
                g_ColorImageAddress, byteCount, first, last, firstValue, nonDefault, nonZero);
        }
        Project64LinuxUpdateHiddenRdramFromRdp(hiddenRdram, g_Processor->get_hidden_rdram_size(), g_ColorImageAddress, byteCount);
        g_Processor->end_write_hidden_rdram();
        g_HiddenRdramPrepared = false;
        g_HiddenRdramDirty = false;
    }
}

void DumpColorImage()
{
    const char * dumpDir = std::getenv("PJ64_PARALLEL_RDP_DUMP_CI");
    if (dumpDir == nullptr || *dumpDir == '\0' || g_Gfx.RDRAM == nullptr)
    {
        return;
    }

    const uint32_t byteCount = CurrentColorImageByteCount();
    if (byteCount == 0)
    {
        return;
    }

    char path[512];
    std::snprintf(path, sizeof(path), "%s/pj64-parallel-ci-%04u-%06X-%ux%u-%ubpp.raw",
        dumpDir, g_ColorImageDumpIndex++, g_ColorImageAddress, g_ColorImageWidth,
        g_ColorImageHeight, g_ColorImageBytesPerPixel * 8);

    FILE * file = std::fopen(path, "wb");
    if (file == nullptr)
    {
        std::fprintf(stderr, "parallel-rdp-pj64: failed to open color image dump %s\n", path);
        return;
    }
    std::fwrite(g_Gfx.RDRAM + g_ColorImageAddress, 1, byteCount, file);
    std::fclose(file);
    std::fprintf(stderr, "parallel-rdp-pj64: dumped color image %s (%u bytes)\n", path, byteCount);
}

void SetViRegisters()
{
    if (!g_Processor)
    {
        return;
    }
    g_Processor->set_vi_register(RDP::VIRegister::Control, Reg(g_Gfx.VI_STATUS_REG));
    g_Processor->set_vi_register(RDP::VIRegister::Origin, Reg(g_Gfx.VI_ORIGIN_REG));
    g_Processor->set_vi_register(RDP::VIRegister::Width, Reg(g_Gfx.VI_WIDTH_REG));
    g_Processor->set_vi_register(RDP::VIRegister::Intr, Reg(g_Gfx.VI_INTR_REG));
    g_Processor->set_vi_register(RDP::VIRegister::VCurrentLine, Reg(g_Gfx.VI_V_CURRENT_LINE_REG));
    g_Processor->set_vi_register(RDP::VIRegister::Timing, Reg(g_Gfx.VI_TIMING_REG));
    g_Processor->set_vi_register(RDP::VIRegister::VSync, Reg(g_Gfx.VI_V_SYNC_REG));
    g_Processor->set_vi_register(RDP::VIRegister::HSync, Reg(g_Gfx.VI_H_SYNC_REG));
    g_Processor->set_vi_register(RDP::VIRegister::Leap, Reg(g_Gfx.VI_LEAP_REG));
    g_Processor->set_vi_register(RDP::VIRegister::HStart, Reg(g_Gfx.VI_H_START_REG));
    g_Processor->set_vi_register(RDP::VIRegister::VStart, Reg(g_Gfx.VI_V_START_REG));
    g_Processor->set_vi_register(RDP::VIRegister::VBurst, Reg(g_Gfx.VI_V_BURST_REG));
    g_Processor->set_vi_register(RDP::VIRegister::XScale, Reg(g_Gfx.VI_X_SCALE_REG));
    g_Processor->set_vi_register(RDP::VIRegister::YScale, Reg(g_Gfx.VI_Y_SCALE_REG));
}

bool InitParallelRdp()
{
    if (g_Running)
    {
        return true;
    }
    if (g_Gfx.RDRAM == nullptr || g_Gfx.RDRAM_SIZE == 0)
    {
        return false;
    }

    g_Context.reset(new Vulkan::Context);
    g_Device.reset(new Vulkan::Device);

    if (!Vulkan::Context::init_loader(nullptr))
    {
        std::fprintf(stderr, "parallel-rdp-pj64: Vulkan loader initialization failed\n");
        return false;
    }
    if (!g_Context->init_instance_and_device(nullptr, 0, nullptr, 0, 0))
    {
        std::fprintf(stderr, "parallel-rdp-pj64: Vulkan instance/device initialization failed\n");
        return false;
    }

    uintptr_t alignedRdram = reinterpret_cast<uintptr_t>(g_Gfx.RDRAM);
    uintptr_t offset = 0;
    g_Device->set_context(*g_Context);

    if (g_Device->get_device_features().supports_external_memory_host)
    {
        const size_t align = g_Device->get_device_features().host_memory_properties.minImportedHostPointerAlignment;
        offset = alignedRdram & (align - 1);
        if (offset != 0)
        {
            std::fprintf(stderr, "parallel-rdp-pj64: RDRAM pointer is not aligned for Vulkan host memory import\n");
            return false;
        }
    }

    g_Device->init_frame_contexts(3);
    g_Processor.reset(new RDP::CommandProcessor(
        *g_Device,
        reinterpret_cast<void *>(alignedRdram - offset),
        offset,
        g_Gfx.RDRAM_SIZE,
        g_Gfx.RDRAM_SIZE / 2,
        RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT));

    if (!g_Processor->device_is_supported())
    {
        std::fprintf(stderr, "parallel-rdp-pj64: Vulkan device is not supported by paraLLEl-RDP\n");
        g_Processor.reset();
        return false;
    }

    RDP::Quirks quirks;
    quirks.set_native_texture_lod(false);
    quirks.set_native_resolution_tex_rect(false);
    g_Processor->set_quirks(quirks);
    OpenCommandLog();
    g_Running = true;
    return true;
}

void DestroyParallelRdp()
{
    g_Running = false;
    g_Processor.reset();
    g_Device.reset();
    g_Context.reset();
    if (g_Texture != 0)
    {
        glDeleteTextures(1, &g_Texture);
        g_Texture = 0;
    }
    g_TextureWidth = 0;
    g_TextureHeight = 0;
    g_Frame.clear();
    g_CommandRead = 0;
    g_CommandWrite = 0;
    g_HiddenRdramPrepared = false;
    g_HiddenRdramDirty = false;
    CloseCommandLog();
}

void EnsureTexture(uint32_t width, uint32_t height)
{
    if (g_Texture == 0)
    {
        glGenTextures(1, &g_Texture);
    }
    glBindTexture(GL_TEXTURE_2D, g_Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    if (g_TextureWidth != width || g_TextureHeight != height)
    {
        g_TextureWidth = width;
        g_TextureHeight = height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
}

void DrawFrame(uint32_t width, uint32_t height)
{
    EnsureTexture(width, height);
    glBindTexture(GL_TEXTURE_2D, g_Texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_Frame.data());

    int drawableWidth = (int)width;
    int drawableHeight = (int)height;
    Project64LinuxGetDrawableSize(&drawableWidth, &drawableHeight);

    constexpr double TargetAspect = 4.0 / 3.0;
    int viewportWidth = drawableWidth;
    int viewportHeight = (int)(viewportWidth / TargetAspect + 0.5);
    if (viewportHeight > drawableHeight)
    {
        viewportHeight = drawableHeight;
        viewportWidth = (int)(viewportHeight * TargetAspect + 0.5);
    }
    const int viewportX = (drawableWidth - viewportWidth) / 2;
    const int viewportY = (drawableHeight - viewportHeight) / 2;

    glViewport(0, 0, drawableWidth, drawableHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}
}

EXPORT void CALL CaptureScreen(const char *)
{
}

EXPORT void CALL ChangeWindow(void)
{
}

EXPORT void CALL CloseDLL(void)
{
    DestroyParallelRdp();
}

EXPORT void CALL DllAbout(void *)
{
}

EXPORT void CALL DllConfig(void *)
{
}

EXPORT void CALL DrawScreen(void)
{
    UpdateScreen();
}

EXPORT void CALL DrawStatus(const char *, int32_t)
{
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    if (PluginInfo == nullptr)
    {
        return;
    }
    std::memset(PluginInfo, 0, sizeof(*PluginInfo));
    PluginInfo->Version = VIDEO_SPECS_VERSION;
    PluginInfo->Type = PLUGIN_TYPE_VIDEO;
    std::snprintf(PluginInfo->Name, sizeof(PluginInfo->Name), "paraLLEl-RDP Linux");
}

EXPORT int CALL InitiateGFX(GFX_INFO Gfx_Info)
{
    g_Gfx = Gfx_Info;
    return 1;
}

EXPORT void CALL PluginLoaded(void)
{
}

EXPORT void CALL MoveScreen(int, int)
{
}

EXPORT void CALL ProcessDList(void)
{
}

EXPORT void CALL ProcessRDPList(void)
{
    if (!g_Running || g_Processor == nullptr)
    {
        return;
    }

    uint32_t current = Reg(g_Gfx.DPC_CURRENT_REG) & 0x00FFFFF8;
    const uint32_t end = Reg(g_Gfx.DPC_END_REG) & 0x00FFFFF8;
    if (end <= current)
    {
        return;
    }
    BeginDpWork();

    uint32_t length = (end - current) >> 3;
    if ((g_CommandWrite + length) > ((sizeof(g_CommandBuffer) / sizeof(g_CommandBuffer[0])) >> 1))
    {
        FinishDpWork();
        return;
    }

    if ((Reg(g_Gfx.DPC_STATUS_REG) & DPC_STATUS_XBUS_DMEM_DMA) != 0)
    {
        while (length-- > 0)
        {
            const uint32_t offset = current & 0xFF8;
            g_CommandBuffer[2 * g_CommandWrite + 0] = *reinterpret_cast<const uint32_t *>(g_Gfx.DMEM + offset);
            g_CommandBuffer[2 * g_CommandWrite + 1] = *reinterpret_cast<const uint32_t *>(g_Gfx.DMEM + offset + 4);
            current += 8;
            g_CommandWrite++;
        }
    }
    else
    {
        if (end > g_Gfx.RDRAM_SIZE || current > g_Gfx.RDRAM_SIZE)
        {
            FinishDpWork();
            return;
        }
        while (length-- > 0)
        {
            const uint32_t offset = current & 0xFFFFF8;
            g_CommandBuffer[2 * g_CommandWrite + 0] = *reinterpret_cast<const uint32_t *>(g_Gfx.RDRAM + offset);
            g_CommandBuffer[2 * g_CommandWrite + 1] = *reinterpret_cast<const uint32_t *>(g_Gfx.RDRAM + offset + 4);
            current += 8;
            g_CommandWrite++;
        }
    }

    while (g_CommandRead < g_CommandWrite)
    {
        const uint32_t w0 = g_CommandBuffer[2 * g_CommandRead + 0];
        const uint32_t command = (w0 >> 24) & 63;
        const uint32_t commandLength = CommandLength[command];

        if ((g_CommandWrite - g_CommandRead) < commandLength)
        {
            SetReg(g_Gfx.DPC_START_REG, Reg(g_Gfx.DPC_END_REG));
            SetReg(g_Gfx.DPC_CURRENT_REG, Reg(g_Gfx.DPC_END_REG));
            FinishDpWork();
            return;
        }

        RecordDpCommand(commandLength);
        TrackDpCommand(command, w0, g_CommandBuffer[2 * g_CommandRead + 1]);
        LogCommand(command, commandLength, &g_CommandBuffer[2 * g_CommandRead]);
        if (command >= 8)
        {
            if (CommandWritesColorImage(command))
            {
                SyncHiddenRdramFromCore();
                g_HiddenRdramDirty = true;
            }
            g_Processor->enqueue_command(commandLength * 2, &g_CommandBuffer[2 * g_CommandRead]);
        }
        if (RDP::Op(command) == RDP::Op::SyncFull)
        {
            g_Processor->wait_for_timeline(g_Processor->signal_timeline());
            SyncHiddenRdramToCore();
            DumpColorImage();
            RaiseDpInterrupt();
        }
        g_CommandRead += commandLength;
    }

    g_CommandRead = 0;
    g_CommandWrite = 0;
    SetReg(g_Gfx.DPC_START_REG, Reg(g_Gfx.DPC_END_REG));
    SetReg(g_Gfx.DPC_CURRENT_REG, Reg(g_Gfx.DPC_END_REG));
    FinishDpWork();
}

EXPORT void CALL RomClosed(void)
{
    DestroyParallelRdp();
}

EXPORT void CALL RomOpen(void)
{
    InitParallelRdp();
}

EXPORT void CALL ShowCFB(void)
{
    UpdateScreen();
}

EXPORT void CALL SoftReset(void)
{
    g_CommandRead = 0;
    g_CommandWrite = 0;
}

EXPORT void CALL UpdateScreen(void)
{
    if (!g_Running || g_Processor == nullptr)
    {
        return;
    }

    SetViRegisters();
    g_Processor->begin_frame_context();
    RDP::ScanoutOptions opts = {};
    opts.persist_frame_on_invalid_input = true;
    opts.vi.aa = true;
    opts.vi.scale = false;
    opts.vi.dither_filter = true;
    opts.vi.divot_filter = true;
    opts.vi.gamma_dither = true;
    opts.upscale_deinterlacing = true;

    RDP::VIScanoutBuffer scanout;
    g_Processor->scanout_async_buffer(scanout, opts);
    if (scanout.width == 0 || scanout.height == 0 || !scanout.buffer)
    {
        return;
    }

    scanout.fence->wait();
    const size_t pixelCount = static_cast<size_t>(scanout.width) * static_cast<size_t>(scanout.height);
    g_Frame.resize(pixelCount);
    const void * mapped = g_Device->map_host_buffer(*scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
    std::memcpy(g_Frame.data(), mapped, pixelCount * sizeof(uint32_t));
    g_Device->unmap_host_buffer(*scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
    DrawFrame(scanout.width, scanout.height);

    if (g_Gfx.SwapBuffers != nullptr)
    {
        g_Gfx.SwapBuffers();
    }
}

EXPORT void CALL ViStatusChanged(void)
{
}

EXPORT void CALL ViWidthChanged(void)
{
}
