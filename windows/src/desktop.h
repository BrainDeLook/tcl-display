#ifndef TCL_DESKTOP_H
#define TCL_DESKTOP_H
#include <windows.h>
#define DESKTOP_MAX_DISPLAYS 32
typedef struct {
    WCHAR device[1024];
    LONG x, y;
    UINT32 width, height, scale;
} DesktopDisplay;
typedef struct {
    UINT32 count, dpi;
    DesktopDisplay displays[DESKTOP_MAX_DISPLAYS];
} DesktopTopology;
UINT32 desktop_display_scale(const WCHAR *source_name);
HRESULT desktop_save(const WCHAR *folder, const DesktopTopology *topology);
HRESULT desktop_restore(const WCHAR *folder, const DesktopTopology *topology);
#endif
