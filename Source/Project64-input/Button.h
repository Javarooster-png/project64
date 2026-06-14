#pragma once
#include <stdint.h>
#ifdef _WIN32
#include <guiddef.h>
#else
#include <Project64-linux/win_compat.h>
#endif

enum BtnType
{
    BTNTYPE_UNASSIGNED = 0,

    // Joystick / controller
    BTNTYPE_JOYBUTTON = 1,
    BTNTYPE_JOYAXE = 2,
    BTNTYPE_JOYPOV = 3,
    BTNTYPE_JOYSLIDER = 4,

    // Keyboard
    BTNTYPE_KEYBUTTON = 5,

    // Mouse
    BTNTYPE_MOUSEBUTTON = 6,
    BTNTYPE_MOUSEAXE = 7,
};

typedef struct _BUTTON
{
    uint16_t Offset;
    uint8_t AxisID;
    BtnType BtnType;
    GUID DeviceGuid;
    void * Device;
} BUTTON;
