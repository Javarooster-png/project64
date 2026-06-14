#include <Project64-plugin-spec/Audio.h>
#include <Project64-plugin-spec/Base.h>

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
enum
{
    SYSTEM_NTSC = 0,
    SYSTEM_PAL = 1,
    SYSTEM_MPAL = 2,
};

AUDIO_INFO g_AudioInfo = {};
SDL_AudioDeviceID g_AudioDevice = 0;
uint32_t g_Frequency = 0;
std::atomic<uint32_t> g_QueuedBytes{0};

uint32_t VideoClockForSystem(int32_t SystemType)
{
    switch (SystemType)
    {
    case SYSTEM_PAL: return 49656530;
    case SYSTEM_MPAL: return 48628316;
    case SYSTEM_NTSC:
    default: return 48681812;
    }
}

void CloseAudioDevice()
{
    if (g_AudioDevice != 0)
    {
        SDL_ClearQueuedAudio(g_AudioDevice);
        SDL_CloseAudioDevice(g_AudioDevice);
        g_AudioDevice = 0;
    }
    g_QueuedBytes = 0;
}

bool OpenAudioDevice(uint32_t Frequency)
{
    if (Frequency < 8000)
    {
        return false;
    }

    if (g_AudioDevice != 0 && g_Frequency == Frequency)
    {
        return true;
    }

    CloseAudioDevice();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        std::fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired = {};
    desired.freq = static_cast<int>(Frequency);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;

    SDL_AudioSpec obtained = {};
    g_AudioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (g_AudioDevice == 0)
    {
        std::fprintf(stderr, "SDL audio open failed: %s\n", SDL_GetError());
        return false;
    }

    g_Frequency = static_cast<uint32_t>(obtained.freq);
    SDL_PauseAudioDevice(g_AudioDevice, 0);
    return true;
}

void QueueAudio(const uint8_t * Source, uint32_t Length)
{
    if (g_AudioDevice == 0 || Source == nullptr || Length == 0)
    {
        return;
    }

    Length &= ~3U;
    std::vector<uint8_t> buffer(Length);
    for (uint32_t i = 0; i < Length; i += 4)
    {
        buffer[i + 0] = Source[i + 2];
        buffer[i + 1] = Source[i + 3];
        buffer[i + 2] = Source[i + 0];
        buffer[i + 3] = Source[i + 1];
    }

    const uint32_t maxQueuedBytes = std::max<uint32_t>(g_Frequency / 2, 32768);
    while (SDL_GetQueuedAudioSize(g_AudioDevice) > maxQueuedBytes)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (SDL_QueueAudio(g_AudioDevice, buffer.data(), buffer.size()) == 0)
    {
        g_QueuedBytes = SDL_GetQueuedAudioSize(g_AudioDevice);
    }
}
}

EXPORT void CALL PluginLoaded(void)
{
}

EXPORT void CALL CloseDLL(void)
{
    CloseAudioDevice();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

EXPORT void CALL DllAbout(void *)
{
}

EXPORT void CALL DllConfig(void *)
{
}

EXPORT void CALL DllTest(void *)
{
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    PluginInfo->Version = AUDIO_SPECS_VERSION;
    PluginInfo->Type = PLUGIN_TYPE_AUDIO;
    std::snprintf(PluginInfo->Name, sizeof(PluginInfo->Name), "Project64 Linux SDL audio");
    PluginInfo->Reserved1 = false;
    PluginInfo->Reserved2 = true;
}

EXPORT int32_t CALL InitiateAudio(AUDIO_INFO Audio_Info)
{
    g_AudioInfo = Audio_Info;
    return true;
}

EXPORT void CALL RomOpen(void)
{
    g_QueuedBytes = 0;
}

EXPORT void CALL RomClosed(void)
{
    CloseAudioDevice();
}

EXPORT void CALL AiDacrateChanged(int32_t SystemType)
{
    if (g_AudioInfo.AI_DACRATE_REG == nullptr)
    {
        return;
    }

    const uint32_t dacrate = *g_AudioInfo.AI_DACRATE_REG & 0x00003FFF;
    OpenAudioDevice(VideoClockForSystem(SystemType) / (dacrate + 1));
}

EXPORT void CALL AiLenChanged(void)
{
    if (g_AudioInfo.RDRAM == nullptr || g_AudioInfo.AI_LEN_REG == nullptr || g_AudioInfo.AI_DRAM_ADDR_REG == nullptr)
    {
        return;
    }

    const uint32_t length = *g_AudioInfo.AI_LEN_REG & 0x3FFF8;
    const uint32_t address = *g_AudioInfo.AI_DRAM_ADDR_REG & 0x00FFFFF8;
    QueueAudio(g_AudioInfo.RDRAM + address, length);
}

EXPORT uint32_t CALL AiReadLength(void)
{
    g_QueuedBytes = g_AudioDevice != 0 ? SDL_GetQueuedAudioSize(g_AudioDevice) : 0;
    return 0;
}

EXPORT void CALL AiUpdate(int32_t)
{
    g_QueuedBytes = g_AudioDevice != 0 ? SDL_GetQueuedAudioSize(g_AudioDevice) : 0;
}

EXPORT void CALL ProcessAList(void)
{
}
