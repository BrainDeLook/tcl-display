/* Synthetic topology: never calls real display-changing APIs. */
#define WINVER 0x0A00
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
static int tv_enabled, other_enabled = 1, applies;
static LONG WINAPI fake_sizes(UINT32 flags, UINT32 *np, UINT32 *nm) {
    (void)flags; *np=2; *nm=2; return 0;
}
static LONG WINAPI fake_query(UINT32 flags, UINT32 *np, DISPLAYCONFIG_PATH_INFO *p,
                             UINT32 *nm, DISPLAYCONFIG_MODE_INFO *m, DISPLAYCONFIG_TOPOLOGY_ID *top) {
    (void)flags;(void)top;*np=2;*nm=2;
    ZeroMemory(p,2*sizeof(*p));ZeroMemory(m,2*sizeof(*m));
    for(UINT32 i=0;i<2;i++) {
        p[i].sourceInfo.id=i;p[i].targetInfo.id=i;
        p[i].sourceInfo.modeInfoIdx=i;p[i].targetInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        p[i].targetInfo.targetAvailable=TRUE;
        p[i].flags=(i==0?other_enabled:tv_enabled)?DISPLAYCONFIG_PATH_ACTIVE:0;
        m[i].infoType=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;m[i].id=i;
        m[i].sourceMode.width=1920;m[i].sourceMode.height=1080;
    }
    return 0;
}
static LONG WINAPI fake_info(DISPLAYCONFIG_DEVICE_INFO_HEADER *h) {
    if(h->type==DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME *n=(void*)h;
        wcscpy(n->monitorDevicePath,h->id==1?L"test-tv":L"test-monitor");return 0;
    }
    if(h->type==DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE) {
        DISPLAYCONFIG_TARGET_PREFERRED_MODE *p=(void*)h;p->width=1920;p->height=1080;return 0;
    }
    return ERROR_INVALID_PARAMETER;
}
static LONG WINAPI fake_set(UINT32 np, DISPLAYCONFIG_PATH_INFO *p, UINT32 nm,
                           DISPLAYCONFIG_MODE_INFO *m, UINT32 flags) {
    (void)nm;(void)m;
    int next=0;
    assert(flags & SDC_USE_SUPPLIED_DISPLAY_CONFIG);
    assert(np>=1 && p[0].targetInfo.id==0); /* Keep the existing desktop. */
    for(UINT32 i=0;i<np;i++) if(p[i].targetInfo.id==1) {
        /* Same rejection as the real driver for the former 0/1 bug. */
        if(p[i].targetInfo.refreshRate.Numerator==0 && p[i].targetInfo.refreshRate.Denominator!=0)
            return ERROR_INVALID_PARAMETER;
        assert(p[i].targetInfo.scanLineOrdering==DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED);
        next=1;
    }
    if(flags & SDC_APPLY){tv_enabled=next;applies++;}
    return 0;
}
#define GetDisplayConfigBufferSizes fake_sizes
#define QueryDisplayConfig fake_query
#define DisplayConfigGetDeviceInfo fake_info
#define SetDisplayConfig fake_set
#define main unused_console_main
#include "../src/core.c"
#undef main
int main(void) {
    wcscpy(selected,L"test-tv");
    assert(change_display(1)==0 && tv_enabled && applies==1);
    assert(change_display(1)==0 && applies==1); /* Idempotent. */
    assert(change_display(0)==0 && !tv_enabled && applies==2);
    other_enabled=0;tv_enabled=1;
    assert(change_display(0)==ERROR_ACCESS_DENIED && applies==2);
    puts("Native display tests passed: enable, idempotency, disable, last-display guard.");
    return 0;
}
