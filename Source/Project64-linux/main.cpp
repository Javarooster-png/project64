#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Notification.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Common/path.h>
#include <Common/Trace.h>

#include <GL/gl.h>
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

class LinuxNotification :
    public CNotification
{
public:
    void DisplayError(const char * Message) const override
    {
        std::fprintf(stderr, "error: %s\n", Message ? Message : "");
    }

    void DisplayError(LanguageStringID StringID) const override
    {
        std::fprintf(stderr, "error: language string %d\n", (int)StringID);
    }

    void FatalError(const char * Message) const override
    {
        std::fprintf(stderr, "fatal: %s\n", Message ? Message : "");
        std::abort();
    }

    void FatalError(LanguageStringID StringID) const override
    {
        std::fprintf(stderr, "fatal: language string %d\n", (int)StringID);
        std::abort();
    }

    void DisplayWarning(const char * Message) const override
    {
        std::fprintf(stderr, "warning: %s\n", Message ? Message : "");
    }

    void DisplayWarning(LanguageStringID StringID) const override
    {
        std::fprintf(stderr, "warning: language string %d\n", (int)StringID);
    }

    void DisplayMessage(int /*DisplayTime*/, const char * Message) const override
    {
        std::fprintf(stdout, "%s\n", Message ? Message : "");
    }

    void DisplayMessage(int /*DisplayTime*/, LanguageStringID StringID) const override
    {
        std::fprintf(stdout, "message: language string %d\n", (int)StringID);
    }

    void DisplayMessage2(const char * Message) const override
    {
        std::fprintf(stdout, "%s\n", Message ? Message : "");
    }

    bool AskYesNoQuestion(const char * Question) const override
    {
        std::fprintf(stderr, "question defaulting to no: %s\n", Question ? Question : "");
        return false;
    }

    void BreakPoint(const char * FileName, int32_t LineNumber) override
    {
        TraceFlushLog();
        FatalError(stdstr_f("Break point found at\n%s\nLine: %d", FileName, LineNumber).c_str());
    }

    void AppInitDone(void) override
    {
    }

    bool ProcessGuiMessages(void) const override
    {
        return false;
    }

    void ChangeFullScreen(void) const override
    {
    }
};

class LinuxRenderWindow :
    public RenderWindow
{
public:
    ~LinuxRenderWindow()
    {
        GfxThreadDone();
    }

    void GfxThreadInit() override
    {
        if (m_Window != nullptr)
        {
            SDL_GL_MakeCurrent(m_Window, m_GLContext);
            return;
        }

        if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            std::fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
            return;
        }

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

        m_Window = SDL_CreateWindow(
            "Project64 Linux",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            640,
            480,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (m_Window == nullptr)
        {
            std::fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
            return;
        }

        m_GLContext = SDL_GL_CreateContext(m_Window);
        if (m_GLContext == nullptr)
        {
            std::fprintf(stderr, "SDL GL context creation failed: %s\n", SDL_GetError());
            return;
        }

        SDL_GL_MakeCurrent(m_Window, m_GLContext);
        SDL_GL_SetSwapInterval(1);
        SDL_SetWindowOpacity(m_Window, 1.0f);
        SDL_ShowWindow(m_Window);
        SDL_RaiseWindow(m_Window);
        glViewport(0, 0, 640, 480);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SDL_GL_SwapWindow(m_Window);
    }

    void GfxThreadDone() override
    {
        if (m_GLContext != nullptr)
        {
            SDL_GL_DeleteContext(m_GLContext);
            m_GLContext = nullptr;
        }
        if (m_Window != nullptr)
        {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }

    void SwapWindow() override
    {
        PumpEvents();
        if (m_Window != nullptr)
        {
            SDL_GL_SwapWindow(m_Window);
        }
    }

private:
    void PumpEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                std::exit(EXIT_SUCCESS);
            }
        }
    }

    SDL_Window * m_Window = nullptr;
    SDL_GLContext m_GLContext = nullptr;
};

static bool IsDiskImage(const stdstr & ext)
{
    return strcasecmp(ext.c_str(), "ndd") == 0 || strcasecmp(ext.c_str(), "d64") == 0;
}

static void PumpSdlEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
        {
            std::exit(EXIT_SUCCESS);
        }
    }
}

int main(int argc, char ** argv)
{
    LinuxNotification notification;
    LinuxRenderWindow mainWindow;
    LinuxRenderWindow syncWindow;

    CPath executablePath(CPath::MODULE_DIRECTORY);
    if (!AppInit(&notification, executablePath, argc, argv))
    {
        AppCleanup();
        return EXIT_FAILURE;
    }

    g_Plugins->SetRenderWindows(&mainWindow, &syncWindow);
    g_Settings->SaveString(Directory_Plugin, "build-linux");
    g_Settings->SaveString(Plugin_GFX_Current, "libGLideN64.so");
    g_Settings->SaveString(Game_Plugin_Gfx, "libGLideN64.so");
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);

    bool loaded = false;
    stdstr romFile = g_Settings->LoadStringVal(Cmd_RomFile);
    stdstr comboDisk = g_Settings->LoadStringVal(Cmd_ComboDiskFile);

    if (!romFile.empty() && !comboDisk.empty())
    {
        stdstr romExt = CPath(romFile).GetExtension();
        stdstr diskExt = CPath(comboDisk).GetExtension();
        if (!IsDiskImage(romExt) && IsDiskImage(diskExt))
        {
            loaded = CN64System::RunDiskComboImage(romFile.c_str(), comboDisk.c_str());
        }
    }
    else if (!romFile.empty())
    {
        stdstr ext = CPath(romFile).GetExtension();
        loaded = IsDiskImage(ext) ? CN64System::RunDiskImage(romFile.c_str()) : CN64System::RunFileImage(romFile.c_str());
    }
    else
    {
        std::fprintf(stderr, "usage: project64-linux <rom-file>\n");
    }

    if (loaded && g_BaseSystem != nullptr)
    {
        for (;;)
        {
            PumpSdlEvents();
            SDL_Delay(16);
        }
    }

    AppCleanup();
    return loaded ? EXIT_SUCCESS : EXIT_FAILURE;
}
