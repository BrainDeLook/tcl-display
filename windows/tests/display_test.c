/* Synthetic topology; no real display-changing API is called. */
#define WINVER 0x0A00
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
static int on[3]={1,1,0}, connected[3]={1,1,1}, applies, reject_validation;
static int discover_late, delayed_checks, mismatched_mode, delay_after_apply;
static UINT32 widths[3]={1920,1920,2560}, heights[3]={1080,1080,1440}, hz[3]={60,144,120};
static int sleeps;
static void fake_sleep(DWORD ms){(void)ms;sleeps++;}
static POINTL position[3]={{-1920,240},{0,0},{1920,0}};
static const WCHAR *names[3]={L"test-monitor-a",L"test-monitor-b",L"test-tv"};
static LONG WINAPI fake_sizes(UINT32 flags, UINT32 *np, UINT32 *nm) {
    (void)flags;*np=3;*nm=6;return 0;
}
static LONG WINAPI fake_query(UINT32 flags, UINT32 *np, DISPLAYCONFIG_PATH_INFO *p,
                             UINT32 *nm, DISPLAYCONFIG_MODE_INFO *m, DISPLAYCONFIG_TOPOLOGY_ID *top) {
    (void)flags;(void)top;*np=3;*nm=0;
    ZeroMemory(p,3*sizeof(*p));ZeroMemory(m,6*sizeof(*m));
    for(UINT32 i=0;i<3;i++) {
        p[i].sourceInfo.id=i;p[i].targetInfo.id=i;
        p[i].targetInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        p[i].targetInfo.targetAvailable=connected[i];
        if(i==2 && discover_late>0){p[i].targetInfo.targetAvailable=FALSE;discover_late--;}
        p[i].sourceInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        if(on[i]) {
            p[i].flags=DISPLAYCONFIG_PATH_ACTIVE;p[i].sourceInfo.modeInfoIdx=*nm;
            m[*nm].infoType=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;m[*nm].id=i;
            m[*nm].sourceMode.width=widths[i];m[*nm].sourceMode.height=heights[i];
            m[*nm].sourceMode.position=position[i];(*nm)++;
            p[i].targetInfo.modeInfoIdx=*nm;
            m[*nm].infoType=DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;m[*nm].id=i;
            m[*nm].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator=hz[i];
            m[*nm].targetMode.targetVideoSignalInfo.vSyncFreq.Denominator=1;
            m[*nm].targetMode.targetVideoSignalInfo.activeSize.cx=widths[i];
            m[*nm].targetMode.targetVideoSignalInfo.activeSize.cy=heights[i];(*nm)++;
        }
    }
    if(delayed_checks>0){delayed_checks--;return ERROR_GEN_FAILURE;}
    return 0;
}
static LONG WINAPI fake_info(DISPLAYCONFIG_DEVICE_INFO_HEADER *h) {
    if(h->type==DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME *n=(void*)h; wcscpy(n->viewGdiDeviceName,L"test-gdi");return 0;
    }
    if(h->type==DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME *n=(void*)h;
        wcscpy(n->monitorDevicePath,names[h->id]);return 0;
    }
    if(h->type==DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE) {
        DISPLAYCONFIG_TARGET_PREFERRED_MODE *p=(void*)h;p->width=3840;p->height=2160;return 0;
    }
    return ERROR_INVALID_PARAMETER;
}
static LONG WINAPI fake_set(UINT32 np, DISPLAYCONFIG_PATH_INFO *p, UINT32 nm,
                           DISPLAYCONFIG_MODE_INFO *m, UINT32 flags) {
    assert(flags & SDC_USE_SUPPLIED_DISPLAY_CONFIG);
    if((flags & SDC_VALIDATE) && reject_validation)return ERROR_BAD_CONFIGURATION;
    int next[3]={0};POINTL next_pos[3]={{0}};
    for(UINT32 i=0;i<np;i++) {
        UINT32 id=p[i].targetInfo.id, idx=p[i].sourceInfo.modeInfoIdx;
        assert(idx<nm && m[idx].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
        if((flags & SDC_APPLY) && p[i].targetInfo.modeInfoIdx<nm) {
            if(id==2) assert(!(flags & SDC_ALLOW_CHANGES));
            hz[id]=m[p[i].targetInfo.modeInfoIdx].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator;
        }
        if(p[i].targetInfo.refreshRate.Numerator==0 && p[i].targetInfo.refreshRate.Denominator!=0)
            return ERROR_INVALID_PARAMETER;
        next[id]=1;next_pos[id]=m[idx].sourceMode.position;
        if(flags & SDC_APPLY){widths[id]=m[idx].sourceMode.width;heights[id]=m[idx].sourceMode.height;}

    }
    if(flags & SDC_APPLY){for(int i=0;i<3;i++){on[i]=next[i];position[i]=next_pos[i];}applies++;
        if(mismatched_mode && on[2]) hz[2]=60;
        if(delay_after_apply) delayed_checks=2;
    }
    return 0;
}
#define GetDisplayConfigBufferSizes fake_sizes
#define QueryDisplayConfig fake_query
#define DisplayConfigGetDeviceInfo fake_info
#define SetDisplayConfig fake_set
#define Sleep fake_sleep
#define main unused_console_main
#include "../src/core.c"
#undef main
static int icons_saves, icons_restores, icons_error;
UINT32 desktop_display_scale(const WCHAR *name){(void)name;return 100;}
HRESULT desktop_save(const WCHAR *folder, const DesktopTopology *topology) {
    (void)folder; assert(topology->count);icons_saves++;
    /* Capture before set_origin mutates source positions. */
    int primary_count=0;
    for(UINT32 i=0;i<topology->count;i++)if(!topology->displays[i].x && !topology->displays[i].y)primary_count++;
    assert(primary_count==1);
    return icons_error ? E_FAIL : S_OK;
}
HRESULT desktop_restore(const WCHAR *folder, const DesktopTopology *topology) {
    (void)folder;assert(topology->count);icons_restores++;return S_OK;
}
static void primary(int id) {assert(on[id] && position[id].x==0 && position[id].y==0);}
static void mode(int enabled) {assert(WritePrivateProfileStringW(L"display",L"make_primary",enabled?L"1":L"0",ini));}
int main(void) {
    WCHAR temp[MAX_PATH], saved[1024];GetTempPathW(MAX_PATH,temp);assert(GetTempFileNameW(temp,L"tcl",0,ini));
    wcscpy(selected,L"test-tv");
    /* Capture real user mode, rather than the TV's EDID-preferred 4K mode. */
    on[2]=1;assert(capture_profile()==0);on[2]=0;
    DisplayProfile profile={0};assert(read_profile(&profile));
    assert(profile.source.width==2560 && profile.target.targetVideoSignalInfo.vSyncFreq.Numerator==120);
    /* Default off preserves the existing primary. */
    assert(change_display(1)==0 && on[2]);primary(1);
    assert(widths[2]==2560 && heights[2]==1440 && hz[2]==120);
    assert(change_display(0)==0 && !on[2]);primary(1);
    mode(1);
    assert(change_display(1)==0 && on[2]);primary(2);
    assert(icons_saves==2 && icons_pending());
    assert(position[0].x==-3840 && position[0].y==240); /* Relative geometry preserved. */
    saved_primary(saved);assert(!wcscmp(saved,names[1])); /* Primary was not the first path. */
    int before=applies;
    assert(change_display(1)==0 && applies==before);assert(icons_saves==2); /* Repeated on does not overwrite saved primary. */
    saved_primary(saved);assert(!wcscmp(saved,names[1]));
    /* State is read from disk, not transient globals: survives app restart. */
    WritePrivateProfileStringW(NULL,NULL,NULL,ini);
    assert(change_display(0)==0 && !on[2]);primary(1);
    assert(icons_restores==4 && !icons_pending());
    assert(position[0].x==-1920 && position[0].y==240);
    saved_primary(saved);assert(!saved[0]);
    /* TV already enabled in the old mode; another on still makes it primary. */
    mode(0);assert(change_display(1)==0);primary(1);
    mode(1);assert(change_display(1)==0);primary(2);
    /* Disabling the preference does not forfeit the pending restoration. */
    mode(0);primary(2);assert(change_display(0)==0);primary(1);
    /* A failed snapshot prevents the primary change before APPLY. */
    mode(1);icons_error=1;before=applies;
    assert(change_display(1)==ERROR_GEN_FAILURE && applies==before && !on[2]);
    assert(!strcmp(last_error,"desktop_icons_capture_failed"));icons_error=0;
    assert(WritePrivateProfileStringW(L"display",L"restore_icons",L"0",ini));
    int saved_calls=icons_saves;assert(change_display(1)==0);assert(icons_saves==saved_calls);
    assert(change_display(0)==0);assert(WritePrivateProfileStringW(L"display",L"restore_icons",L"1",ini));
    /* An absent previous monitor must not be activated. */
    mode(1);assert(change_display(1)==0);on[1]=0;connected[1]=0;
    assert(change_display(0)==0 && !on[1] && !on[2]);primary(0);
    /* Last-display guard and validation failure cannot apply a topology. */
    on[0]=0;on[2]=1;position[2].x=position[2].y=0;before=applies;
    assert(change_display(0)==ERROR_ACCESS_DENIED && applies==before);
    on[0]=1;on[2]=0;position[0].x=position[0].y=0;
    reject_validation=1;assert(change_display(1)==ERROR_BAD_CONFIGURATION && applies==before);
    reject_validation=0;discover_late=2;sleeps=0;delay_after_apply=1;
    assert(change_display(1)==0 && sleeps>=4);assert(hz[2]==120);delay_after_apply=0;
    assert(change_display(0)==0);
    mismatched_mode=1;sleeps=0;
    assert(change_display(1)==ERROR_GEN_FAILURE);
    assert(!strcmp(last_error,"display_mode_not_restored") && sleeps==12);
    mismatched_mode=0;
    /* Broken/foreign profiles never get used for another monitor. */
    wcscpy(selected,L"test-monitor-a");assert(!read_profile(&profile));wcscpy(selected,L"test-tv");
    assert(WritePrivateProfileStringW(L"display_profile",L"mode_v1",L"corrupt",ini));assert(!read_profile(&profile));
    DeleteFileW(ini);
    puts("Native tests passed: saved 1440p120 timing, mode mismatch, discovery wait, async confirmation, primary restore, guards.");
    return 0;
}
