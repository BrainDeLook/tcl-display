#define TCL_DESKTOP_TEST
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/desktop.c"
#include <assert.h>

static POINT points[3]={{50,80},{200,120},{700,250}};
static const WCHAR *item_names[3]={L"C:\\Desktop\\Папка",L"C:\\Desktop\\файл.txt",L"C:\\Desktop\\new.lnk"};
static int item_count=2, calls, fail_read, fail_position, auto_arrange;
static ULONG STDMETHODCALLTYPE fake_release_view(IFolderView2 *view){(void)view;return 1;}
static ULONG STDMETHODCALLTYPE fake_release_folder(IShellFolder *folder){(void)folder;return 1;}
static IShellFolder folder;
static HRESULT STDMETHODCALLTYPE fake_folder(IFolderView2 *view, REFIID iid, void **out) {
    (void)view;(void)iid;*out=&folder;return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_flags(IFolderView2 *view,DWORD *flags) {
    (void)view;*flags=auto_arrange?FWF_AUTOARRANGE:FWF_SNAPTOGRID;return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_size(IFolderView2 *view,FOLDERVIEWMODE *mode,int *size) {
    (void)view;*mode=FVM_ICON;*size=32;return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_spacing(IFolderView2 *view,POINT *spacing) {
    (void)view;spacing->x=75;spacing->y=75;return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_count(IFolderView2 *view,UINT flags,int *count) {
    (void)view;(void)flags;*count=item_count;return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_item(IFolderView2 *view,int index,PITEMID_CHILD *out) {
    (void)view;*out=CoTaskMemAlloc(8);ZeroMemory(*out,8);
    (*out)->mkid.cb=3;(*out)->mkid.abID[0]=(BYTE)index;return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_name(IShellFolder *shell,PCUITEMID_CHILD pidl,SHGDNF flags,STRRET *out) {
    (void)shell;(void)flags;int i=pidl->mkid.abID[0];
    out->uType=STRRET_WSTR;size_t bytes=(wcslen(item_names[i])+1)*sizeof(WCHAR);
    out->pOleStr=CoTaskMemAlloc(bytes);memcpy(out->pOleStr,item_names[i],bytes);return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_point(IFolderView2 *view,PCUITEMID_CHILD pidl,POINT *point) {
    (void)view;if(fail_read)return E_FAIL;*point=points[pidl->mkid.abID[0]];return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_position(IFolderView2 *view,UINT count,PCUITEMID_CHILD_ARRAY pidls,POINT *saved,DWORD flags) {
    (void)view;assert(flags==SVSI_POSITIONITEM);calls++;
    for(UINT i=0;i<count;i++) points[pidls[i]->mkid.abID[0]]=saved[i];
    if(fail_position)points[0].x++;return S_OK;
}
static IFolderView2Vtbl view_methods={
    .Release=fake_release_view,.GetFolder=fake_folder,.GetCurrentFolderFlags=fake_flags,
    .GetViewModeAndIconSize=fake_size,.GetSpacing=fake_spacing,.ItemCount=fake_count,
    .Item=fake_item,.GetItemPosition=fake_point,.SelectAndPositionItems=fake_position
};
static IShellFolderVtbl folder_methods={.Release=fake_release_folder,.GetDisplayNameOf=fake_name};
static IFolderView2 view={&view_methods};
HRESULT desktop_view(IFolderView2 **out){*out=&view;return S_OK;}

int main(void) {
    folder.lpVtbl=&folder_methods;
    WCHAR root[MAX_PATH],dir[MAX_PATH],file[MAX_PATH];GetTempPathW(MAX_PATH,root);
    assert(GetTempFileNameW(root,L"tci",0,dir));assert(DeleteFileW(dir));assert(CreateDirectoryW(dir,NULL));
    DesktopTopology topology={0};topology.count=2;topology.dpi=96;
    wcscpy(topology.displays[0].device,L"monitor-a");topology.displays[0].width=1920;topology.displays[0].height=1080;topology.displays[0].scale=100;
    topology.displays[1]=topology.displays[0];wcscpy(topology.displays[1].device,L"monitor-b");topology.displays[1].x=-1920;
    assert(desktop_save(dir,&topology)==S_OK);
    points[0].x=900;points[1].y=900;item_count=3;
    assert(desktop_restore(dir,&topology)==S_OK);
    assert(points[0].x==50 && points[1].y==120 && points[2].x==700); /* Existing files/folders; new item untouched. */
    points[0].x=175;points[1].y=325;assert(desktop_save(dir,&topology)==S_OK);
    points[0].x=999;points[1].y=999;assert(desktop_restore(dir,&topology)==S_OK);
    assert(points[0].x==175 && points[1].y==325); /* Fresh per-cycle capture. */
    fail_read=1;assert(FAILED(desktop_save(dir,&topology)));fail_read=0;
    points[0].x=999;assert(desktop_restore(dir,&topology)==S_OK && points[0].x==175); /* Failed capture preserves backup. */
    int before=calls;topology.displays[0].scale=125;
    assert(desktop_restore(dir,&topology)==HRESULT_FROM_WIN32(ERROR_BAD_CONFIGURATION) && calls==before);
    topology.displays[0].scale=100;topology.displays[0].x=1;
    assert(desktop_restore(dir,&topology)==HRESULT_FROM_WIN32(ERROR_BAD_CONFIGURATION) && calls==before);
    topology.displays[0].x=0;auto_arrange=1;
    assert(desktop_restore(dir,&topology)==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && calls==before);
    auto_arrange=0;fail_position=1;assert(desktop_restore(dir,&topology)==HRESULT_FROM_WIN32(ERROR_RETRY));fail_position=0;
    swprintf(file,MAX_PATH,L"%ls\\desktop-icons.bin",dir);
    FILE *f=_wfopen(file,L"wb");assert(f);fwrite("invalid",1,7,f);fclose(f);
    assert(desktop_restore(dir,&topology)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    DeleteFileW(file);RemoveDirectoryW(dir);
    puts("Desktop tests passed: current files/folders, new-item preservation, fresh capture, atomic backup, topology/scale/auto-arrange guards, verification.");
    return 0;
}
