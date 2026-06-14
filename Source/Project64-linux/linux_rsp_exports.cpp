#include <Project64-plugin-spec/Rsp.h>
#include <Project64-rsp-core/RSPInfo.h>
#include <Project64-rsp-core/cpu/RSPCpu.h>
#include <stdio.h>

EXPORT void CALL CloseDLL(void)
{
    FreeRSP();
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
    PluginInfo->Version = RSP_SPECS_VERSION;
    PluginInfo->Type = PLUGIN_TYPE_RSP;
    snprintf(PluginInfo->Name, sizeof(PluginInfo->Name), "Project64 RSP Linux");
    PluginInfo->Reserved1 = false;
    PluginInfo->Reserved2 = true;
}

EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, uint32_t * CycleCount)
{
    InitilizeRSP(Rsp_Info);
    if (CycleCount != nullptr)
    {
        *CycleCount = 0;
    }
}

EXPORT void CALL InitiateRSPDebugger(DEBUG_INFO)
{
}

EXPORT void CALL RomOpen(void)
{
    RspRomOpened();
}

EXPORT void CALL RomClosed(void)
{
    RspRomClosed();
}

EXPORT void CALL PluginLoaded(void)
{
    RspPluginLoaded();
}

extern "C" void UseUnregisteredSetting(int)
{
}
