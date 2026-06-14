#pragma once

#include <stdint.h>

typedef void * HWND;
typedef void * HINSTANCE;
typedef void * HDC;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef uintptr_t UINT_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef int BOOL;
typedef uint8_t BYTE;
typedef int32_t LONG;
typedef LONG * LPLONG;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef WM_USER
#define WM_USER 0x0400
#endif
#ifndef LOWORD
#define LOWORD(l) ((uint16_t)((uintptr_t)(l) & 0xffff))
#endif

typedef struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;

inline bool operator==(const GUID & lhs, const GUID & rhs)
{
    return lhs.Data1 == rhs.Data1 &&
        lhs.Data2 == rhs.Data2 &&
        lhs.Data3 == rhs.Data3 &&
        lhs.Data4[0] == rhs.Data4[0] &&
        lhs.Data4[1] == rhs.Data4[1] &&
        lhs.Data4[2] == rhs.Data4[2] &&
        lhs.Data4[3] == rhs.Data4[3] &&
        lhs.Data4[4] == rhs.Data4[4] &&
        lhs.Data4[5] == rhs.Data4[5] &&
        lhs.Data4[6] == rhs.Data4[6] &&
        lhs.Data4[7] == rhs.Data4[7];
}

