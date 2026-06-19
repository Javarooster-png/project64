#include <vector>

#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/Mips/SystemEvents.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Notification.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/Plugins/GFXPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Common/path.h>
#include <Common/Trace.h>

#include <GL/gl.h>
#include <SDL.h>
#include <wx/wx.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filepicker.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/timer.h>

#include <cstdio>
#include <cstdlib>
#include <limits.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>

class LinuxNotification :
    public CNotification
{
public:
    void DisplayError(const char * Message) const override
    {
        const char * text = NormalizeMessage(Message);
        fprintf(stderr, "Project64-linux error: %s\n", text);
        wxLogError("%s", text);
    }

    void DisplayError(LanguageStringID StringID) const override
    {
        DisplayError(GS(StringID));
    }

    void FatalError(const char * Message) const override
    {
        const char * text = NormalizeMessage(Message);
        fprintf(stderr, "Project64-linux fatal: %s\n", text);
        wxLogFatalError("%s", text);
    }

    void FatalError(LanguageStringID StringID) const override
    {
        FatalError(GS(StringID));
    }

    void DisplayWarning(const char * Message) const override
    {
        const char * text = NormalizeMessage(Message);
        fprintf(stderr, "Project64-linux warning: %s\n", text);
        wxLogWarning("%s", text);
    }

    void DisplayWarning(LanguageStringID StringID) const override
    {
        DisplayWarning(GS(StringID));
    }

    void DisplayMessage(int /*DisplayTime*/, const char * Message) const override
    {
        if (Message != nullptr && *Message != '\0')
        {
            wxLogStatus("%s", Message);
        }
    }

    void DisplayMessage(int /*DisplayTime*/, LanguageStringID StringID) const override
    {
        wxLogStatus("%s", GS(StringID));
    }

    void DisplayMessage2(const char * Message) const override
    {
        if (Message != nullptr && *Message != '\0')
        {
            wxLogStatus("%s", Message);
        }
    }

    bool AskYesNoQuestion(const char * Question) const override
    {
        return wxMessageBox(Question ? Question : "", "Project64", wxYES_NO | wxICON_QUESTION) == wxYES;
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
        if (wxTheApp != nullptr)
        {
            wxTheApp->Yield(true);
        }
        return true;
    }

    void ChangeFullScreen(void) const override
    {
    }

private:
    static const char * NormalizeMessage(const char * Message)
    {
        return Message != nullptr && *Message != '\0' ? Message : "(empty notification message)";
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
        EnsureCreated();
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

    bool EnsureCreated()
    {
        if (m_Window != nullptr)
        {
            SDL_GL_MakeCurrent(m_Window, m_GLContext);
            return true;
        }

        if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            wxLogError("SDL video init failed: %s", SDL_GetError());
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

        m_Window = SDL_CreateWindow(
            "Project64 Linux - Video",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            640,
            480,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (m_Window == nullptr)
        {
            wxLogError("SDL window creation failed: %s", SDL_GetError());
            return false;
        }

        m_GLContext = SDL_GL_CreateContext(m_Window);
        if (m_GLContext == nullptr)
        {
            wxLogError("SDL GL context creation failed: %s", SDL_GetError());
            return false;
        }

        SDL_GL_MakeCurrent(m_Window, m_GLContext);
        SDL_GL_SetSwapInterval(1);
        SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
        DrawBlankFrame();
        return true;
    }

    bool PumpEvents()
    {
        SDL_PumpEvents();
        SDL_Event event;
        while (SDL_PeepEvents(&event, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) > 0)
        {
            return false;
        }
        return true;
    }

    bool GetDrawableSize(int & width, int & height)
    {
        EnsureCreated();
        if (m_Window == nullptr)
        {
            return false;
        }
        SDL_GL_GetDrawableSize(m_Window, &width, &height);
        return width > 0 && height > 0;
    }

private:
    void DrawBlankFrame()
    {
        glViewport(0, 0, 640, 480);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SDL_GL_SwapWindow(m_Window);
    }

    SDL_Window * m_Window = nullptr;
    SDL_GLContext m_GLContext = nullptr;
};

static bool IsDiskImage(const stdstr & ext)
{
    return strcasecmp(ext.c_str(), "ndd") == 0 || strcasecmp(ext.c_str(), "d64") == 0;
}

extern "C" void Project64LinuxGfxThreadInit()
{
    RenderWindow * render = g_Plugins != nullptr ? g_Plugins->MainWindow() : nullptr;
    if (render != nullptr)
    {
        render->GfxThreadInit();
    }
}

extern "C" void Project64LinuxGfxThreadDone()
{
    RenderWindow * render = g_Plugins != nullptr ? g_Plugins->MainWindow() : nullptr;
    if (render != nullptr)
    {
        render->GfxThreadDone();
    }
}

extern "C" int Project64LinuxGetDrawableSize(int * width, int * height)
{
    RenderWindow * render = g_Plugins != nullptr ? g_Plugins->MainWindow() : nullptr;
    LinuxRenderWindow * linuxRender = dynamic_cast<LinuxRenderWindow *>(render);
    if (linuxRender == nullptr || width == nullptr || height == nullptr)
    {
        return 0;
    }
    return linuxRender->GetDrawableSize(*width, *height) ? 1 : 0;
}

extern "C" void Project64LinuxUpdateHiddenRdramFromRdp(const void * hiddenRdram, size_t hiddenRdramSize, uint32_t rdramOffset, uint32_t byteCount)
{
    if (g_MMU == nullptr || hiddenRdram == nullptr || hiddenRdramSize == 0 || byteCount == 0)
    {
        return;
    }
    g_MMU->UpdateHiddenRdramFromRdp(static_cast<const uint8_t *>(hiddenRdram), hiddenRdramSize, rdramOffset, byteCount);
}

extern "C" void Project64LinuxCopyHiddenRdramToRdp(void * hiddenRdram, size_t hiddenRdramSize, uint32_t rdramOffset, uint32_t byteCount)
{
    if (g_MMU == nullptr || hiddenRdram == nullptr || hiddenRdramSize == 0 || byteCount == 0)
    {
        return;
    }
    g_MMU->CopyHiddenRdramToRdp(static_cast<uint8_t *>(hiddenRdram), hiddenRdramSize, rdramOffset, byteCount);
}

static std::string ResolveLinuxPluginDirectory(const CPath & moduleDirectory, int argc, char ** argv)
{
    auto directoryContainsPlugins = [](const std::string & directory) {
        std::string fileName = directory;
        if (!fileName.empty() && fileName[fileName.size() - 1] != '/')
        {
            fileName += "/";
        }
        fileName += "libGLideN64.so";

        struct stat fileInfo;
        return stat(fileName.c_str(), &fileInfo) == 0 && S_ISREG(fileInfo.st_mode);
    };

    auto normalizeDirectory = [](const std::string & directory) {
        char resolved[PATH_MAX];
        if (realpath(directory.c_str(), resolved) != nullptr)
        {
            return std::string(resolved);
        }
        return directory;
    };

    auto parentDirectory = [](const std::string & path) {
        size_t slash = path.find_last_of('/');
        if (slash == std::string::npos)
        {
            return std::string();
        }
        if (slash == 0)
        {
            return std::string("/");
        }
        return path.substr(0, slash);
    };

    std::vector<std::string> candidates;
    candidates.emplace_back((const char *)moduleDirectory);

    if (argc > 0 && argv != nullptr && argv[0] != nullptr && argv[0][0] != '\0')
    {
        std::string executablePath(argv[0]);
        if (executablePath.find('/') != std::string::npos && executablePath[0] != '/')
        {
            char currentDirectory[PATH_MAX];
            if (getcwd(currentDirectory, sizeof(currentDirectory)) != nullptr)
            {
                executablePath = std::string(currentDirectory) + "/" + executablePath;
            }
        }
        candidates.emplace_back(parentDirectory(executablePath));
    }

    char currentDirectory[PATH_MAX];
    if (getcwd(currentDirectory, sizeof(currentDirectory)) != nullptr)
    {
        candidates.emplace_back(std::string(currentDirectory) + "/build-linux");
    }
    candidates.emplace_back(std::string((const char *)moduleDirectory) + "/build-linux");

    for (const std::string & candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }
        std::string normalized = normalizeDirectory(candidate);
        if (directoryContainsPlugins(normalized))
        {
            return normalized;
        }
    }

    return (const char *)moduleDirectory;
}

enum LinuxMenuId
{
    ID_FILE_OPEN_ROM = wxID_HIGHEST + 4000,
    ID_FILE_OPEN_COMBO,
    ID_FILE_ROM_INFO,
    ID_FILE_STARTEMULATION,
    ID_FILE_ENDEMULATION,
    ID_FILE_ROMDIRECTORY,
    ID_FILE_REFRESHROMLIST,

    ID_SYSTEM_RESET_SOFT,
    ID_SYSTEM_RESET_HARD,
    ID_SYSTEM_PAUSE,
    ID_SYSTEM_BITMAP,
    ID_SYSTEM_LIMITFPS,
    ID_SYSTEM_RESTORE,
    ID_SYSTEM_LOAD,
    ID_SYSTEM_SAVE,
    ID_SYSTEM_SAVEAS,
    ID_SYSTEM_ENHANCEMENT,
    ID_SYSTEM_CHEAT,
    ID_SYSTEM_GSBUTTON,

    ID_OPTIONS_FULLSCREEN,
    ID_OPTIONS_ALWAYSONTOP,
    ID_OPTIONS_CONFIG_GFX,
    ID_OPTIONS_CONFIG_AUDIO,
    ID_OPTIONS_CONFIG_CONT,
    ID_OPTIONS_CONFIG_RSP,
    ID_OPTIONS_SETTINGS,
    ID_OPTIONS_DISPLAY_FR,
    ID_OPTIONS_INCREASE_SPEED,
    ID_OPTIONS_DECREASE_SPEED,

    ID_DEBUGGER_LOGOPTIONS,
    ID_DEBUGGER_GENERATELOG,
    ID_DEBUGGER_DUMPMEMORY,
    ID_DEBUGGER_SEARCHMEMORY,
    ID_DEBUGGER_TLBENTRIES,
    ID_DEBUGGER_BREAKPOINTS,
    ID_DEBUGGER_MEMORY,
    ID_DEBUGGER_R4300REGISTERS,
    ID_DEBUGGER_SCRIPTS,
    ID_DEBUGGER_SYMBOLS,
    ID_DEBUGGER_DMALOG,
    ID_DEBUGGER_CPULOG,
    ID_DEBUGGER_STACKTRACE,
    ID_DEBUGGER_STACKVIEW,

    ID_HELP_WEBSITE,
    ID_HELP_ABOUT,
    ID_TIMER_SDL_EVENTS
};

class SettingsDialog :
    public wxDialog
{
public:
    SettingsDialog(wxWindow * parent) :
        wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxSize(620, 460), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        wxBoxSizer * rootSizer = new wxBoxSizer(wxVERTICAL);
        wxNotebook * notebook = new wxNotebook(this, wxID_ANY);

        notebook->AddPage(CreateGeneralPage(notebook), "General");
        notebook->AddPage(CreatePluginPage(notebook), "Plugins");
        notebook->AddPage(CreateDirectoryPage(notebook), "Directories");

        wxStdDialogButtonSizer * buttons = new wxStdDialogButtonSizer();
        buttons->AddButton(new wxButton(this, wxID_OK));
        buttons->AddButton(new wxButton(this, wxID_CANCEL));
        buttons->Realize();

        rootSizer->Add(notebook, 1, wxEXPAND | wxALL, 8);
        rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        SetSizer(rootSizer);

        Bind(wxEVT_BUTTON, &SettingsDialog::OnOk, this, wxID_OK);
    }

private:
    wxWindow * CreateGeneralPage(wxWindow * parent)
    {
        wxPanel * panel = new wxPanel(parent);
        wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);

        m_ForceInterpreter = new wxCheckBox(panel, wxID_ANY, "Always use interpreter core");
        m_ForceInterpreter->SetValue(g_Settings->LoadBool(Setting_ForceInterpreterCPU));
        sizer->Add(m_ForceInterpreter, 0, wxALL, 8);

        m_LimitFps = new wxCheckBox(panel, wxID_ANY, "Limit FPS");
        m_LimitFps->SetValue(g_Settings->LoadBool(GameRunning_LimitFPS));
        sizer->Add(m_LimitFps, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        wxStaticText * note = new wxStaticText(panel, wxID_ANY, "These settings are saved through Project64's existing settings backend.");
        sizer->Add(note, 0, wxLEFT | wxRIGHT | wxTOP, 8);
        sizer->AddStretchSpacer();
        panel->SetSizer(sizer);
        return panel;
    }

    wxWindow * CreatePluginPage(wxWindow * parent)
    {
        wxPanel * panel = new wxPanel(parent);
        wxFlexGridSizer * grid = new wxFlexGridSizer(2, 8, 8);
        grid->AddGrowableCol(1, 1);

        m_PluginDir = AddDirectoryPicker(panel, grid, "Plugin directory", g_Settings->LoadStringVal(Directory_Plugin));
        m_GfxPlugin = AddComboBox(panel, grid, "Graphics plugin", g_Settings->LoadStringVal(Plugin_GFX_Current),
            {"libGLideN64.so", "libparallel-rdp-pj64.so"});
        m_AudioPlugin = AddTextBox(panel, grid, "Audio plugin", g_Settings->LoadStringVal(Plugin_AUDIO_Current));
        m_InputPlugin = AddTextBox(panel, grid, "Controller plugin", g_Settings->LoadStringVal(Plugin_CONT_Current));
        m_RspPlugin = AddTextBox(panel, grid, "RSP plugin", g_Settings->LoadStringVal(Plugin_RSP_Current));

        wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, 0, wxEXPAND | wxALL, 8);
        sizer->AddStretchSpacer();
        panel->SetSizer(sizer);
        return panel;
    }

    wxWindow * CreateDirectoryPage(wxWindow * parent)
    {
        wxPanel * panel = new wxPanel(parent);
        wxFlexGridSizer * grid = new wxFlexGridSizer(2, 8, 8);
        grid->AddGrowableCol(1, 1);

        m_RomDir = AddDirectoryPicker(panel, grid, "ROM directory", g_Settings->LoadStringVal(RomList_GameDir));
        m_ScreenshotDir = AddDirectoryPicker(panel, grid, "Screenshot directory", g_Settings->LoadStringVal(Directory_SnapShot));
        m_NativeSaveDir = AddDirectoryPicker(panel, grid, "Native save directory", g_Settings->LoadStringVal(Directory_NativeSave));
        m_StateSaveDir = AddDirectoryPicker(panel, grid, "State save directory", g_Settings->LoadStringVal(Directory_InstantSave));

        m_RomDirRecursive = new wxCheckBox(panel, wxID_ANY, "Scan ROM directory recursively");
        m_RomDirRecursive->SetValue(g_Settings->LoadBool(RomList_GameDirRecursive));

        wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, 0, wxEXPAND | wxALL, 8);
        sizer->Add(m_RomDirRecursive, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        sizer->AddStretchSpacer();
        panel->SetSizer(sizer);
        return panel;
    }

    wxTextCtrl * AddTextBox(wxWindow * parent, wxFlexGridSizer * grid, const char * label, const std::string & value)
    {
        grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        wxTextCtrl * control = new wxTextCtrl(parent, wxID_ANY, value);
        grid->Add(control, 1, wxEXPAND);
        return control;
    }

    wxComboBox * AddComboBox(wxWindow * parent, wxFlexGridSizer * grid, const char * label, const std::string & value, std::initializer_list<const char *> choices)
    {
        grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        wxArrayString items;
        for (const char * choice : choices)
        {
            items.Add(choice);
        }
        wxComboBox * control = new wxComboBox(parent, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, items, wxCB_DROPDOWN);
        grid->Add(control, 1, wxEXPAND);
        return control;
    }

    wxDirPickerCtrl * AddDirectoryPicker(wxWindow * parent, wxFlexGridSizer * grid, const char * label, const std::string & value)
    {
        grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        wxDirPickerCtrl * control = new wxDirPickerCtrl(parent, wxID_ANY, value, "Choose directory");
        grid->Add(control, 1, wxEXPAND);
        return control;
    }

    void SaveSelectedDirectory(SettingID selected, SettingID useSelected, wxDirPickerCtrl * picker)
    {
        const std::string path = picker->GetPath().ToStdString();
        if (!path.empty())
        {
            g_Settings->SaveString(selected, path);
            g_Settings->SaveBool(useSelected, true);
        }
    }

    void OnOk(wxCommandEvent &)
    {
        g_Settings->SaveBool(Setting_ForceInterpreterCPU, m_ForceInterpreter->GetValue());
        g_Settings->SaveBool(GameRunning_LimitFPS, m_LimitFps->GetValue());

        SaveSelectedDirectory(Directory_PluginSelected, Directory_PluginUseSelected, m_PluginDir);
        SaveSelectedDirectory(RomList_GameDirSelected, RomList_GameDirUseSelected, m_RomDir);
        SaveSelectedDirectory(Directory_SnapShotSelected, Directory_SnapShotUseSelected, m_ScreenshotDir);
        SaveSelectedDirectory(Directory_NativeSaveSelected, Directory_NativeSaveUseSelected, m_NativeSaveDir);
        SaveSelectedDirectory(Directory_InstantSaveSelected, Directory_InstantSaveUseSelected, m_StateSaveDir);
        g_Settings->SaveBool(RomList_GameDirRecursive, m_RomDirRecursive->GetValue());

        g_Settings->SaveString(Plugin_GFX_Current, m_GfxPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Game_Plugin_Gfx, m_GfxPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Plugin_AUDIO_Current, m_AudioPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Game_Plugin_Audio, m_AudioPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Plugin_CONT_Current, m_InputPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Game_Plugin_Controller, m_InputPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Plugin_RSP_Current, m_RspPlugin->GetValue().ToStdString());
        g_Settings->SaveString(Game_Plugin_RSP, m_RspPlugin->GetValue().ToStdString());

        EndModal(wxID_OK);
    }

    wxCheckBox * m_ForceInterpreter = nullptr;
    wxCheckBox * m_LimitFps = nullptr;
    wxCheckBox * m_RomDirRecursive = nullptr;
    wxDirPickerCtrl * m_PluginDir = nullptr;
    wxDirPickerCtrl * m_RomDir = nullptr;
    wxDirPickerCtrl * m_ScreenshotDir = nullptr;
    wxDirPickerCtrl * m_NativeSaveDir = nullptr;
    wxDirPickerCtrl * m_StateSaveDir = nullptr;
    wxComboBox * m_GfxPlugin = nullptr;
    wxTextCtrl * m_AudioPlugin = nullptr;
    wxTextCtrl * m_InputPlugin = nullptr;
    wxTextCtrl * m_RspPlugin = nullptr;
};

class Project64Frame :
    public wxFrame
{
public:
    Project64Frame(LinuxRenderWindow & mainWindow, LinuxRenderWindow & syncWindow) :
        wxFrame(nullptr, wxID_ANY, "Project64 Linux", wxDefaultPosition, wxSize(900, 620)),
        m_MainRenderWindow(mainWindow),
        m_SyncRenderWindow(syncWindow),
        m_Timer(this, ID_TIMER_SDL_EVENTS)
    {
        CreateStatusBar(2);
        SetStatusText("Ready", 0);
        SetStatusText("Interpreter core", 1);

        BuildMenuBar();
        BuildRomBrowserShell();
        BindCommands();

        m_Timer.Start(16);
    }

    bool OpenInitialRom(const stdstr & romFile, const stdstr & comboDisk)
    {
        if (!romFile.empty() && !comboDisk.empty())
        {
            stdstr romExt = CPath(romFile).GetExtension();
            stdstr diskExt = CPath(comboDisk).GetExtension();
            if (!IsDiskImage(romExt) && IsDiskImage(diskExt))
            {
                return RunDiskCombo(romFile.c_str(), comboDisk.c_str());
            }
        }
        else if (!romFile.empty())
        {
            return RunImage(romFile.c_str());
        }
        return false;
    }

private:
    void BuildMenuBar()
    {
        wxMenu * fileMenu = new wxMenu();
        fileMenu->Append(ID_FILE_OPEN_ROM, "&Open ROM...\tCtrl+O");
        fileMenu->Append(ID_FILE_OPEN_COMBO, "Open ROM + 64DD Disk...");
        fileMenu->Append(ID_FILE_ROM_INFO, "ROM &Information...");
        fileMenu->AppendSeparator();
        fileMenu->Append(ID_FILE_STARTEMULATION, "&Start Emulation");
        fileMenu->Append(ID_FILE_ENDEMULATION, "&End Emulation");
        fileMenu->AppendSeparator();
        fileMenu->Append(ID_FILE_ROMDIRECTORY, "Choose ROM Directory...");
        fileMenu->Append(ID_FILE_REFRESHROMLIST, "Refresh ROM List\tF5");
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_EXIT, "E&xit");

        wxMenu * systemMenu = new wxMenu();
        systemMenu->Append(ID_SYSTEM_RESET_SOFT, "Soft &Reset\tF1");
        systemMenu->Append(ID_SYSTEM_RESET_HARD, "Hard Reset");
        systemMenu->AppendCheckItem(ID_SYSTEM_PAUSE, "&Pause\tF2");
        systemMenu->AppendSeparator();
        systemMenu->Append(ID_SYSTEM_BITMAP, "Save Screenshot");
        systemMenu->AppendCheckItem(ID_SYSTEM_LIMITFPS, "Limit FPS");
        systemMenu->AppendSeparator();
        systemMenu->Append(ID_SYSTEM_LOAD, "&Load State\tF7");
        systemMenu->Append(ID_SYSTEM_SAVE, "&Save State\tF5");
        systemMenu->Append(ID_SYSTEM_SAVEAS, "Save State As...");
        systemMenu->Append(ID_SYSTEM_RESTORE, "Restore State");
        systemMenu->AppendSeparator();
        systemMenu->Append(ID_SYSTEM_CHEAT, "Cheats...");
        systemMenu->Append(ID_SYSTEM_ENHANCEMENT, "Enhancements...");
        systemMenu->Append(ID_SYSTEM_GSBUTTON, "GS Button");

        wxMenu * optionsMenu = new wxMenu();
        optionsMenu->Append(ID_OPTIONS_FULLSCREEN, "&Fullscreen\tAlt+Enter");
        optionsMenu->AppendCheckItem(ID_OPTIONS_ALWAYSONTOP, "Always on Top");
        optionsMenu->AppendSeparator();
        optionsMenu->Append(ID_OPTIONS_CONFIG_GFX, "Configure Graphics Plugin...");
        optionsMenu->Append(ID_OPTIONS_CONFIG_AUDIO, "Configure Audio Plugin...");
        optionsMenu->Append(ID_OPTIONS_CONFIG_CONT, "Configure Controller Plugin...");
        optionsMenu->Append(ID_OPTIONS_CONFIG_RSP, "Configure RSP Plugin...");
        optionsMenu->AppendSeparator();
        optionsMenu->Append(ID_OPTIONS_SETTINGS, "&Settings...");
        optionsMenu->AppendCheckItem(ID_OPTIONS_DISPLAY_FR, "Display Frame Rate");
        optionsMenu->Append(ID_OPTIONS_INCREASE_SPEED, "Increase Speed");
        optionsMenu->Append(ID_OPTIONS_DECREASE_SPEED, "Decrease Speed");

        wxMenu * debuggerMenu = new wxMenu();
        debuggerMenu->Append(ID_DEBUGGER_LOGOPTIONS, "Log Options...");
        debuggerMenu->AppendCheckItem(ID_DEBUGGER_GENERATELOG, "Generate Log");
        debuggerMenu->AppendSeparator();
        debuggerMenu->Append(ID_DEBUGGER_DUMPMEMORY, "Dump Memory...");
        debuggerMenu->Append(ID_DEBUGGER_SEARCHMEMORY, "Search Memory...");
        debuggerMenu->Append(ID_DEBUGGER_TLBENTRIES, "TLB Entries...");
        debuggerMenu->Append(ID_DEBUGGER_BREAKPOINTS, "Breakpoints...");
        debuggerMenu->Append(ID_DEBUGGER_MEMORY, "Memory View...");
        debuggerMenu->Append(ID_DEBUGGER_R4300REGISTERS, "R4300i Registers...");
        debuggerMenu->Append(ID_DEBUGGER_SCRIPTS, "Scripts...");
        debuggerMenu->Append(ID_DEBUGGER_SYMBOLS, "Symbols...");
        debuggerMenu->Append(ID_DEBUGGER_DMALOG, "DMA Log...");
        debuggerMenu->Append(ID_DEBUGGER_CPULOG, "CPU Log...");
        debuggerMenu->Append(ID_DEBUGGER_STACKTRACE, "Stack Trace...");
        debuggerMenu->Append(ID_DEBUGGER_STACKVIEW, "Stack View...");

        wxMenu * helpMenu = new wxMenu();
        helpMenu->Append(ID_HELP_WEBSITE, "Project64 Website");
        helpMenu->Append(ID_HELP_ABOUT, "&About Project64 Linux");

        wxMenuBar * menuBar = new wxMenuBar();
        menuBar->Append(fileMenu, "&File");
        menuBar->Append(systemMenu, "&System");
        menuBar->Append(optionsMenu, "&Options");
        menuBar->Append(debuggerMenu, "&Debugger");
        menuBar->Append(helpMenu, "&Help");
        SetMenuBar(menuBar);
    }

    void BuildRomBrowserShell()
    {
        wxPanel * panel = new wxPanel(this);
        wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText * title = new wxStaticText(panel, wxID_ANY, "ROM Browser");
        wxFont titleFont = title->GetFont();
        titleFont.SetPointSize(titleFont.GetPointSize() + 3);
        titleFont.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(titleFont);

        m_RomList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        m_RomList->AppendColumn("Name", wxLIST_FORMAT_LEFT, 240);
        m_RomList->AppendColumn("Status", wxLIST_FORMAT_LEFT, 140);
        m_RomList->AppendColumn("Path", wxLIST_FORMAT_LEFT, 480);

        sizer->Add(title, 0, wxALL, 8);
        sizer->Add(m_RomList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        panel->SetSizer(sizer);
        SetMinSize(wxSize(760, 460));
    }

    void BindCommands()
    {
        Bind(wxEVT_MENU, &Project64Frame::OnOpenRom, this, ID_FILE_OPEN_ROM);
        Bind(wxEVT_MENU, &Project64Frame::OnOpenCombo, this, ID_FILE_OPEN_COMBO);
        Bind(wxEVT_MENU, &Project64Frame::OnRomInfo, this, ID_FILE_ROM_INFO);
        Bind(wxEVT_MENU, &Project64Frame::OnEndEmulation, this, ID_FILE_ENDEMULATION);
        Bind(wxEVT_MENU, &Project64Frame::OnChooseRomDirectory, this, ID_FILE_ROMDIRECTORY);
        Bind(wxEVT_MENU, &Project64Frame::OnRefreshRomList, this, ID_FILE_REFRESHROMLIST);
        Bind(wxEVT_MENU, &Project64Frame::OnExit, this, wxID_EXIT);

        Bind(wxEVT_MENU, &Project64Frame::OnSoftReset, this, ID_SYSTEM_RESET_SOFT);
        Bind(wxEVT_MENU, &Project64Frame::OnHardReset, this, ID_SYSTEM_RESET_HARD);
        Bind(wxEVT_MENU, &Project64Frame::OnPause, this, ID_SYSTEM_PAUSE);
        Bind(wxEVT_MENU, &Project64Frame::OnScreenshot, this, ID_SYSTEM_BITMAP);
        Bind(wxEVT_MENU, &Project64Frame::OnLoadState, this, ID_SYSTEM_LOAD);
        Bind(wxEVT_MENU, &Project64Frame::OnSaveState, this, ID_SYSTEM_SAVE);
        Bind(wxEVT_MENU, &Project64Frame::OnGsButton, this, ID_SYSTEM_GSBUTTON);

        Bind(wxEVT_MENU, &Project64Frame::OnToggleFullscreen, this, ID_OPTIONS_FULLSCREEN);
        Bind(wxEVT_MENU, &Project64Frame::OnAlwaysOnTop, this, ID_OPTIONS_ALWAYSONTOP);
        Bind(wxEVT_MENU, &Project64Frame::OnPluginConfig, this, ID_OPTIONS_CONFIG_GFX, ID_OPTIONS_CONFIG_RSP);
        Bind(wxEVT_MENU, &Project64Frame::OnSettings, this, ID_OPTIONS_SETTINGS);
        Bind(wxEVT_MENU, &Project64Frame::OnNotYetPorted, this, ID_SYSTEM_SAVEAS, ID_SYSTEM_ENHANCEMENT);
        Bind(wxEVT_MENU, &Project64Frame::OnNotYetPorted, this, ID_DEBUGGER_LOGOPTIONS, ID_DEBUGGER_STACKVIEW);
        Bind(wxEVT_MENU, &Project64Frame::OnAbout, this, ID_HELP_ABOUT);
        Bind(wxEVT_MENU, &Project64Frame::OnWebsite, this, ID_HELP_WEBSITE);
        Bind(wxEVT_TIMER, &Project64Frame::OnSdlTimer, this, ID_TIMER_SDL_EVENTS);
        Bind(wxEVT_CLOSE_WINDOW, &Project64Frame::OnClose, this);
    }

    void OnOpenRom(wxCommandEvent &)
    {
        wxFileDialog dialog(
            this,
            "Open ROM",
            "",
            "",
            "N64 ROMs and disks (*.zip;*.7z;*.?64;*.rom;*.usa;*.jap;*.pal;*.bin;*.ndd;*.d64)|*.zip;*.7z;*.?64;*.rom;*.usa;*.jap;*.pal;*.bin;*.ndd;*.d64|All files (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
        {
            RunImage(dialog.GetPath().ToStdString().c_str());
        }
    }

    void OnOpenCombo(wxCommandEvent &)
    {
        wxFileDialog romDialog(this, "Open ROM", "", "", "N64 ROMs (*.zip;*.7z;*.?64;*.rom;*.usa;*.jap;*.pal;*.bin)|*.zip;*.7z;*.?64;*.rom;*.usa;*.jap;*.pal;*.bin|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (romDialog.ShowModal() != wxID_OK)
        {
            return;
        }

        wxFileDialog diskDialog(this, "Open 64DD Disk", "", "", "64DD disks (*.ndd;*.d64)|*.ndd;*.d64|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (diskDialog.ShowModal() == wxID_OK)
        {
            RunDiskCombo(romDialog.GetPath().ToStdString().c_str(), diskDialog.GetPath().ToStdString().c_str());
        }
    }

    void OnRomInfo(wxCommandEvent &)
    {
        ShowPortPending("ROM information dialog");
    }

    void OnEndEmulation(wxCommandEvent &)
    {
        CloseRom();
    }

    void OnChooseRomDirectory(wxCommandEvent &)
    {
        wxDirDialog dialog(this, "Choose ROM Directory");
        if (dialog.ShowModal() == wxID_OK)
        {
            m_RomDirectory = dialog.GetPath();
            RefreshRomList();
        }
    }

    void OnRefreshRomList(wxCommandEvent &)
    {
        RefreshRomList();
    }

    void OnExit(wxCommandEvent &)
    {
        Close(true);
    }

    void OnSoftReset(wxCommandEvent &)
    {
        QueueSystemEvent(SysEvent_ResetCPU_Soft);
    }

    void OnHardReset(wxCommandEvent &)
    {
        QueueSystemEvent(SysEvent_ResetCPU_Hard);
    }

    void OnPause(wxCommandEvent &)
    {
        if (g_BaseSystem == nullptr)
        {
            return;
        }
        bool paused = g_Settings->LoadBool(GameRunning_CPU_Paused);
        g_BaseSystem->ExternalEvent(paused ? SysEvent_ResumeCPU_FromMenu : SysEvent_PauseCPU_FromMenu);
        GetMenuBar()->Check(ID_SYSTEM_PAUSE, !paused);
    }

    void OnScreenshot(wxCommandEvent &)
    {
        if (g_Plugins != nullptr && g_Plugins->Gfx() != nullptr)
        {
            stdstr directory(g_Settings->LoadStringVal(Directory_SnapShot));
            g_Plugins->Gfx()->CaptureScreen(directory.c_str());
            SetStatusText("Screenshot requested", 0);
        }
    }

    void OnLoadState(wxCommandEvent &)
    {
        QueueSystemEvent(SysEvent_LoadMachineState);
    }

    void OnSaveState(wxCommandEvent &)
    {
        QueueSystemEvent(SysEvent_SaveMachineState);
    }

    void OnGsButton(wxCommandEvent &)
    {
        QueueSystemEvent(SysEvent_GSButtonPressed);
    }

    void OnToggleFullscreen(wxCommandEvent &)
    {
        if (g_BaseSystem != nullptr)
        {
            QueueSystemEvent(SysEvent_ChangingFullScreen);
        }
    }

    void OnAlwaysOnTop(wxCommandEvent & event)
    {
        long style = GetWindowStyle();
        if (event.IsChecked())
        {
            style |= wxSTAY_ON_TOP;
        }
        else
        {
            style &= ~wxSTAY_ON_TOP;
        }
        SetWindowStyle(style);
    }

    void OnPluginConfig(wxCommandEvent & event)
    {
        if (g_Plugins == nullptr)
        {
            return;
        }

        switch (event.GetId())
        {
        case ID_OPTIONS_CONFIG_GFX: g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_VIDEO); break;
        case ID_OPTIONS_CONFIG_AUDIO: g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_AUDIO); break;
        case ID_OPTIONS_CONFIG_CONT: g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_CONTROLLER); break;
        case ID_OPTIONS_CONFIG_RSP: g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_RSP); break;
        }
    }

    void OnSettings(wxCommandEvent &)
    {
        SettingsDialog dialog(this);
        if (dialog.ShowModal() == wxID_OK)
        {
            m_RomDirectory = g_Settings->LoadStringVal(RomList_GameDir);
            SetStatusText("Settings saved", 0);
        }
    }

    void OnNotYetPorted(wxCommandEvent & event)
    {
        wxString label = GetMenuBar()->GetLabel(event.GetId());
        label.Replace("&", "");
        label.Replace("...", "");
        ShowPortPending(label.ToStdString().c_str());
    }

    void OnAbout(wxCommandEvent &)
    {
        wxMessageBox("Project64 Linux\nNative wxWidgets UI port in progress.", "About Project64 Linux", wxOK | wxICON_INFORMATION, this);
    }

    void OnWebsite(wxCommandEvent &)
    {
        wxLaunchDefaultBrowser("https://www.pj64-emu.com/");
    }

    void OnSdlTimer(wxTimerEvent &)
    {
        if (!m_MainRenderWindow.PumpEvents())
        {
            Close(true);
        }
    }

    void OnClose(wxCloseEvent & event)
    {
        CloseRom();
        event.Skip();
    }

    bool RunImage(const char * path)
    {
        if (path == nullptr || *path == '\0')
        {
            return false;
        }

        stdstr ext = CPath(path).GetExtension();
        bool loaded = IsDiskImage(ext) ? CN64System::RunDiskImage(path) : CN64System::RunFileImage(path);
        if (loaded)
        {
            AddRomListEntry(path, "Running");
            SetStatusText(wxString::Format("Running: %s", wxString(CPath(path).GetNameExtension().c_str())), 0);
        }
        return loaded;
    }

    bool RunDiskCombo(const char * romPath, const char * diskPath)
    {
        bool loaded = CN64System::RunDiskComboImage(romPath, diskPath);
        if (loaded)
        {
            AddRomListEntry(romPath, "Running with disk");
            SetStatusText("Running ROM + disk combo", 0);
        }
        return loaded;
    }

    void CloseRom()
    {
        if (g_BaseSystem != nullptr)
        {
            g_BaseSystem->CloseCpu();
            SetStatusText("Emulation stopped", 0);
        }
    }

    void QueueSystemEvent(SystemEvent event)
    {
        if (g_BaseSystem != nullptr)
        {
            g_BaseSystem->ExternalEvent(event);
            SetStatusText(SystemEventName(event), 0);
        }
    }

    void RefreshRomList()
    {
        if (m_RomDirectory.empty())
        {
            ShowPortPending("ROM directory scanning");
            return;
        }

        m_RomList->DeleteAllItems();
        wxArrayString files;
        wxDir::GetAllFiles(m_RomDirectory, &files, "*.z64", wxDIR_FILES);
        for (size_t i = 0; i < files.size(); ++i)
        {
            AddRomListEntry(files[i].ToStdString().c_str(), "Available");
        }
        SetStatusText(wxString::Format("%zu ROMs listed", files.size()), 0);
    }

    void AddRomListEntry(const char * path, const char * status)
    {
        wxString fileName(CPath(path).GetNameExtension().c_str());
        long row = m_RomList->InsertItem(m_RomList->GetItemCount(), fileName);
        m_RomList->SetItem(row, 1, status);
        m_RomList->SetItem(row, 2, path);
    }

    void ShowPortPending(const char * feature)
    {
        wxMessageBox(wxString::Format("%s is part of the Windows UI port and is not wired yet.", feature), "Project64 Linux", wxOK | wxICON_INFORMATION, this);
    }

    LinuxRenderWindow & m_MainRenderWindow;
    LinuxRenderWindow & m_SyncRenderWindow;
    wxListCtrl * m_RomList = nullptr;
    wxString m_RomDirectory;
    wxTimer m_Timer;
};

class Project64LinuxApp :
    public wxApp
{
public:
    bool OnInit() override
    {
        CPath executablePath(CPath::MODULE_DIRECTORY);
        if (!AppInit(&m_Notification, executablePath, m_Argc, m_Argv))
        {
            AppCleanup();
            return false;
        }

        g_Plugins->SetRenderWindows(&m_MainRenderWindow, &m_SyncRenderWindow);
        g_Settings->SaveString(Directory_PluginSelected, ResolveLinuxPluginDirectory(executablePath, m_Argc, m_Argv));
        g_Settings->SaveBool(Directory_PluginUseSelected, true);
        g_Settings->SaveString(Plugin_AUDIO_Current, "libProject64-audio-linux.so");
        g_Settings->SaveString(Game_Plugin_Audio, "libProject64-audio-linux.so");
        g_Settings->SaveString(Plugin_CONT_Current, "libProject64-input-linux.so");
        g_Settings->SaveString(Game_Plugin_Controller, "libProject64-input-linux.so");
        g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);

        Project64Frame * frame = new Project64Frame(m_MainRenderWindow, m_SyncRenderWindow);
        frame->Show(true);
        SetTopWindow(frame);

        stdstr romFile = g_Settings->LoadStringVal(Cmd_RomFile);
        stdstr comboDisk = g_Settings->LoadStringVal(Cmd_ComboDiskFile);
        if (!romFile.empty())
        {
            CallAfter([frame, romFile, comboDisk]() {
                frame->OpenInitialRom(romFile, comboDisk);
            });
        }
        return true;
    }

    int OnExit() override
    {
        AppCleanup();
        return wxApp::OnExit();
    }

    void SetNativeArgv(int argc, char ** argv)
    {
        m_Argc = argc;
        m_Argv = argv;
    }

private:
    int m_Argc = 0;
    char ** m_Argv = nullptr;
    LinuxNotification m_Notification;
    LinuxRenderWindow m_MainRenderWindow;
    LinuxRenderWindow m_SyncRenderWindow;
};

wxIMPLEMENT_APP_NO_MAIN(Project64LinuxApp);

int main(int argc, char ** argv)
{
    Project64LinuxApp::SetInstance(new Project64LinuxApp());
    static_cast<Project64LinuxApp *>(wxApp::GetInstance())->SetNativeArgv(argc, argv);
    return wxEntry(argc, argv);
}
