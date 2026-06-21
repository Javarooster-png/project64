#include "rsp.h"

EXPORT void CALL CloseDLL(void)
{
}

EXPORT void CALL DllAbout(p_void hParent)
{
    (void)hParent;
}

EXPORT void CALL DllConfig(p_void hParent)
{
    (void)hParent;
}

EXPORT void CALL DllTest(p_void hParent)
{
    (void)hParent;
}

EXPORT void CALL GetRspDebugInfo(RSPDEBUG_INFO * RSPDebugInfo)
{
    (void)RSPDebugInfo;
}

EXPORT void CALL InitiateRSPDebugger(DEBUG_INFO DebugInfo)
{
    (void)DebugInfo;
}

EXPORT void CALL RomOpen(void)
{
}

EXPORT void CALL EnableDebugging(int Enabled)
{
    (void)Enabled;
}

EXPORT void CALL PluginLoaded(void)
{
}
