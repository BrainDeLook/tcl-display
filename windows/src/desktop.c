#define WINVER 0x0A00
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <servprov.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <shellscalingapi.h>
#include <stdio.h>
#include <wchar.h>
#include "desktop.h"

#define LAYOUT_MAGIC 0x54434c49u
#define MAX_ITEMS 4096
#define NAME_CAPACITY 2048
typedef struct {
    UINT32 magic, version, count, icon_size;
    POINT spacing;
    DesktopTopology topology;
} LayoutHeader;
typedef struct { WCHAR name[NAME_CAPACITY]; POINT position; } LayoutItem;

typedef struct { const WCHAR *name; UINT32 scale; } ScaleLookup;
static BOOL CALLBACK monitor_scale(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc; (void)rect; ScaleLookup *lookup = (ScaleLookup *)data;
    MONITORINFOEXW info = {0}; info.cbSize = sizeof(info);
    DEVICE_SCALE_FACTOR scale;
    if (GetMonitorInfoW(monitor, (MONITORINFO *)&info) && !_wcsicmp(info.szDevice, lookup->name) &&
        SUCCEEDED(GetScaleFactorForMonitor(monitor, &scale))) lookup->scale = (UINT32)scale;
    return TRUE;
}
UINT32 desktop_display_scale(const WCHAR *source_name) {
    ScaleLookup lookup = {source_name, 0};
    EnumDisplayMonitors(NULL, NULL, monitor_scale, (LPARAM)&lookup);
    return lookup.scale;
}
/* Use the supported shell view API, not Explorer's private ListView messages. */
#ifdef TCL_DESKTOP_TEST
HRESULT desktop_view(IFolderView2 **out);
#else
static HRESULT desktop_view(IFolderView2 **out) {
    IShellWindows *windows = NULL; IDispatch *dispatch = NULL;
    IServiceProvider *provider = NULL; IShellBrowser *browser = NULL; IShellView *view = NULL;
    VARIANT location, empty; VariantInit(&location); VariantInit(&empty);
    V_VT(&location) = VT_I4; V_I4(&location) = CSIDL_DESKTOP;
    LONG window = 0;
    HRESULT hr = CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER,
        &IID_IShellWindows, (void **)&windows);
    if (SUCCEEDED(hr)) hr = IShellWindows_FindWindowSW(windows, &location, &empty,
        SWC_DESKTOP, &window, SWFO_NEEDDISPATCH, &dispatch);
    if (SUCCEEDED(hr) && !dispatch) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = IDispatch_QueryInterface(dispatch, &IID_IServiceProvider, (void **)&provider);
    if (SUCCEEDED(hr)) hr = IServiceProvider_QueryService(provider, &SID_STopLevelBrowser,
        &IID_IShellBrowser, (void **)&browser);
    if (SUCCEEDED(hr)) hr = IShellBrowser_QueryActiveShellView(browser, &view);
    if (SUCCEEDED(hr)) hr = IShellView_QueryInterface(view, &IID_IFolderView2, (void **)out);
    if (view) IShellView_Release(view);
    if (browser) IShellBrowser_Release(browser);
    if (provider) IServiceProvider_Release(provider);
    if (dispatch) IDispatch_Release(dispatch);
    if (windows) IShellWindows_Release(windows);
    return hr;
}
#endif
static int same_topology(const DesktopTopology *a, const DesktopTopology *b) {
    if (a->count != b->count || a->dpi != b->dpi) return 0;
    for (UINT32 i = 0; i < a->count; i++) {
        int matched = 0;
        for (UINT32 j = 0; j < b->count; j++) {
            const DesktopDisplay *x = &a->displays[i], *y = &b->displays[j];
            if (!_wcsicmp(x->device, y->device) && x->x == y->x && x->y == y->y &&
                x->width == y->width && x->height == y->height && x->scale == y->scale) { matched = 1; break; }
        }
        if (!matched) return 0;
    }
    return 1;
}
static HRESULT read_layout(const WCHAR *folder, const DesktopTopology *topology,
                           LayoutHeader *header, LayoutItem **items) {
    WCHAR path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\desktop-icons.bin", folder);
    FILE *file = _wfopen(path, L"rb");
    if (!file) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (fread(header, sizeof(*header), 1, file) != 1 || header->magic != LAYOUT_MAGIC ||
        header->version != 1 || header->count > MAX_ITEMS ||
        !header->topology.count || header->topology.count > DESKTOP_MAX_DISPLAYS) goto done;
    for (UINT32 i = 0; i < header->topology.count; i++)
        if (!wmemchr(header->topology.displays[i].device, 0, 1024)) goto done;
    if (!same_topology(&header->topology, topology)) { hr = HRESULT_FROM_WIN32(ERROR_BAD_CONFIGURATION); goto done; }
    *items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (header->count + 1) * sizeof(**items));
    if (!*items) { hr = E_OUTOFMEMORY; goto done; }
    if (fread(*items, sizeof(**items), header->count, file) != header->count || fgetc(file) != EOF) goto done;
    for (UINT32 i = 0; i < header->count; i++) {
        const LayoutItem *item = &(*items)[i];
        if (!wmemchr(item->name, 0, NAME_CAPACITY) || !item->name[0] ||
            item->position.x < -1000000 || item->position.x > 1000000 ||
            item->position.y < -1000000 || item->position.y > 1000000) goto done;
    }
    hr = S_OK;
done:
    fclose(file);
    if (FAILED(hr) && *items) { HeapFree(GetProcessHeap(), 0, *items); *items = NULL; }
    return hr;
}
static HRESULT view_settings(IFolderView2 *view, UINT32 *size, POINT *spacing) {
    DWORD flags = 0; FOLDERVIEWMODE mode = FVM_AUTO; int icon_size = 0;
    HRESULT hr = IFolderView2_GetCurrentFolderFlags(view, &flags);
    if (SUCCEEDED(hr) && (flags & FWF_AUTOARRANGE)) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (SUCCEEDED(hr)) hr = IFolderView2_GetViewModeAndIconSize(view, &mode, &icon_size);
    if (SUCCEEDED(hr)) hr = IFolderView2_GetSpacing(view, spacing);
    if (SUCCEEDED(hr)) *size = (UINT32)icon_size;
    return hr;
}
HRESULT desktop_save(const WCHAR *folder, const DesktopTopology *topology) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) return init;
    IFolderView2 *view = NULL; IShellFolder *shell_folder = NULL;
    LayoutItem *items = NULL; LayoutHeader header = {0};
    HRESULT hr = desktop_view(&view);
    if (SUCCEEDED(hr)) hr = view_settings(view, &header.icon_size, &header.spacing);
    if (SUCCEEDED(hr)) hr = IFolderView2_GetFolder(view, &IID_IShellFolder, (void **)&shell_folder);
    int count = 0;
    if (SUCCEEDED(hr)) hr = IFolderView2_ItemCount(view, SVGIO_ALLVIEW, &count);
    if (SUCCEEDED(hr) && (count < 0 || count > MAX_ITEMS)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(*items));
        if (!items) hr = E_OUTOFMEMORY;
    }
    for (int i = 0; SUCCEEDED(hr) && i < count; i++) {
        PITEMID_CHILD pidl = NULL; STRRET name;
        hr = IFolderView2_Item(view, i, &pidl);
        if (SUCCEEDED(hr)) hr = IShellFolder_GetDisplayNameOf(shell_folder, pidl, SHGDN_FORPARSING, &name);
        if (SUCCEEDED(hr)) hr = StrRetToBufW(&name, pidl, items[i].name, NAME_CAPACITY);
        if (SUCCEEDED(hr)) hr = IFolderView2_GetItemPosition(view, pidl, &items[i].position);
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(hr)) {
        header.magic = LAYOUT_MAGIC; header.version = 1; header.count = (UINT32)count; header.topology = *topology;
        WCHAR path[MAX_PATH], temp[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\desktop-icons.bin", folder);
        swprintf(temp, MAX_PATH, L"%ls\\desktop-icons.tmp", folder);
        FILE *file = _wfopen(temp, L"wb");
        if (!file) hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        else {
            int ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
                fwrite(items, sizeof(*items), count, file) == (size_t)count;
            if (fclose(file)) ok = 0;
            if (!ok || !MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
        if (FAILED(hr)) DeleteFileW(temp);
    }
    if (items) HeapFree(GetProcessHeap(), 0, items);
    if (shell_folder) IShellFolder_Release(shell_folder);
    if (view) IFolderView2_Release(view);
    CoUninitialize(); return hr;
}
HRESULT desktop_restore(const WCHAR *folder, const DesktopTopology *topology) {
    LayoutHeader header; LayoutItem *items = NULL;
    HRESULT hr = read_layout(folder, topology, &header, &items);
    if (FAILED(hr)) return hr;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) { HeapFree(GetProcessHeap(), 0, items); return init; }
    IFolderView2 *view = NULL; IShellFolder *shell_folder = NULL;
    PITEMID_CHILD *pidls = NULL; POINT *positions = NULL; UINT matched = 0;
    hr = desktop_view(&view);
    UINT32 size = 0; POINT spacing;
    if (SUCCEEDED(hr)) hr = view_settings(view, &size, &spacing);
    if (SUCCEEDED(hr) && (size != header.icon_size || spacing.x != header.spacing.x || spacing.y != header.spacing.y))
        hr = HRESULT_FROM_WIN32(ERROR_BAD_CONFIGURATION);
    if (SUCCEEDED(hr)) hr = IFolderView2_GetFolder(view, &IID_IShellFolder, (void **)&shell_folder);
    int count = 0;
    if (SUCCEEDED(hr)) hr = IFolderView2_ItemCount(view, SVGIO_ALLVIEW, &count);
    if (SUCCEEDED(hr) && (count < 0 || count > MAX_ITEMS)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        pidls = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(*pidls));
        positions = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(*positions));
        if (!pidls || !positions) hr = E_OUTOFMEMORY;
    }
    /* Enumerate current items: never create/move/rename desktop files. */
    for (int i = 0; SUCCEEDED(hr) && i < count; i++) {
        PITEMID_CHILD pidl = NULL; STRRET name; WCHAR current[NAME_CAPACITY];
        hr = IFolderView2_Item(view, i, &pidl);
        if (SUCCEEDED(hr)) hr = IShellFolder_GetDisplayNameOf(shell_folder, pidl, SHGDN_FORPARSING, &name);
        if (SUCCEEDED(hr)) hr = StrRetToBufW(&name, pidl, current, NAME_CAPACITY);
        if (SUCCEEDED(hr)) for (UINT32 saved = 0; saved < header.count; saved++) {
            if (!_wcsicmp(current, items[saved].name)) {
                pidls[matched] = pidl; positions[matched++] = items[saved].position; pidl = NULL; break;
            }
        }
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(hr) && matched)
        hr = IFolderView2_SelectAndPositionItems(view, matched, (PCUITEMID_CHILD_ARRAY)pidls, positions, SVSI_POSITIONITEM);
    for (UINT i = 0; SUCCEEDED(hr) && i < matched; i++) {
        POINT actual; hr = IFolderView2_GetItemPosition(view, pidls[i], &actual);
        if (SUCCEEDED(hr) && (actual.x != positions[i].x || actual.y != positions[i].y))
            hr = HRESULT_FROM_WIN32(ERROR_RETRY);
    }
    if (pidls) { for (UINT i = 0; i < matched; i++) CoTaskMemFree(pidls[i]); HeapFree(GetProcessHeap(), 0, pidls); }
    if (positions) HeapFree(GetProcessHeap(), 0, positions);
    if (shell_folder) IShellFolder_Release(shell_folder);
    if (view) IFolderView2_Release(view);
    HeapFree(GetProcessHeap(), 0, items); CoUninitialize(); return hr;
}
