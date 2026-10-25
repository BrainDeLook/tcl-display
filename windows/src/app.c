#define main core_main
#include "core.c"
#undef main
#include <commctrl.h>
#include <shellapi.h>

#define WM_AGENT (WM_APP + 1)
#define WM_TRAY (WM_APP + 2)
#define ID_MONITOR 101
#define ID_PC 102
#define ID_HA 103
#define ID_PORT 104
#define ID_KEY 105
#define ID_SHOW 106
#define ID_SAVE 107
#define ID_START 108
#define ID_STOP 109
#define ID_ON 110
#define ID_OFF 111
#define ID_REFRESH 112
#define ID_FIREWALL 113
#define ID_AUTO 114
#define ID_COPY 115
#define ID_FILES 116
#define ID_RUN 117
#define ID_TAB 118
#define ID_EXIT 119
#define ID_OPEN 120
#define ID_EXPORT 121
#define ID_WINDOWS 122
#define ID_TRAY_VISIBLE 123
#define ID_CLOSE_ACTION 124
#define ID_SAVE_BEHAVIOR 125
#define ID_QUIT_BUTTON 126
#define ID_PRIMARY_MODE 127
#ifdef TCL_TEST_BUILD
#define APP_MUTEX_NAME L"Local\\TCLDisplayAppBehaviorTest"
#define APP_CLASS_NAME L"TCLDisplayAppBehaviorTestWindow"
#else
#define APP_MUTEX_NAME L"Local\\TCLDisplayAppGUI"
#define APP_CLASS_NAME L"TCLDisplayAppWindow"
#endif

static HWND window, tab, monitor_box, pc_edit, ha_edit, port_edit, key_edit;
static HWND status_text, display_text, auto_box, run_box, yaml_edit;
static HWND controls[3][40];
static int ncontrols[3], current_page = 0, scale = 100, configured = 0, settings_dirty = 0;
static int show_tray_icon = 1, close_to_background = 1, tray_present = 0;
static UINT taskbar_created;
static HWND tray_box, close_box, primary_box;
static HFONT font, title_font;
static WCHAR monitor_ids[32][1024], pc_ip[80], ha_ip[80];
static HANDLE thread, stop_event;
static CRITICAL_SECTION display_lock;
static NOTIFYICONDATAW tray;
static HINSTANCE instance;
static WCHAR executable[MAX_PATH];

static int px(int value) { return MulDiv(value, scale, 100); }
static void message(const WCHAR *text, UINT flags) { MessageBoxW(window, text, L"TCL Display", flags); }
static void error_code(const WCHAR *title, LONG code) {
    WCHAR reason[512] = L"", text[1024];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, reason, 512, NULL);
    swprintf(text, 1024, L"%ls\n\nКод Windows: %ld\n%ls", title, code, reason);
    message(text, MB_ICONERROR);
}
static HWND control(int page, const WCHAR *cls, const WCHAR *text, DWORD style,
    int x, int y, int w, int h, int id) {
    HWND child = CreateWindowExW(!wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0,
        cls, text, WS_CHILD | (page == 0 ? WS_VISIBLE : 0) | style,
        px(x), px(y), px(w), px(h), window, (HMENU)(INT_PTR)id, instance, NULL);
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    if (page >= 0) controls[page][ncontrols[page]++] = child;
    return child;
}
static void page_select(int page) {
    current_page = page;
    if (page == 2 && primary_box) SendMessageW(primary_box, BM_SETCHECK, primary_mode_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    for (int p = 0; p < 3; p++) for (int i = 0; i < ncontrols[p]; i++)
        ShowWindow(controls[p][i], p == page ? SW_SHOW : SW_HIDE);
}
static void tray_apply(void) {
    if (!tray.hWnd) return;
    if (show_tray_icon && !tray_present) tray_present = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    else if (!show_tray_icon && tray_present) { Shell_NotifyIconW(NIM_DELETE, &tray); tray_present = 0; }
}
static void behavior_load(void) {
    show_tray_icon = GetPrivateProfileIntW(L"ui", L"show_tray_icon", 1, ini) != 0;
    close_to_background = GetPrivateProfileIntW(L"ui", L"close_to_background", 1, ini) != 0;
    SendMessageW(tray_box, BM_SETCHECK, show_tray_icon ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(close_box, CB_SETCURSEL, close_to_background ? 0 : 1, 0);
}
static void behavior_save(void) {
    int show = SendMessageW(tray_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int background = SendMessageW(close_box, CB_GETCURSEL, 0, 0) == 0;
    if (!WritePrivateProfileStringW(L"ui", L"show_tray_icon", show ? L"1" : L"0", ini) ||
        !WritePrivateProfileStringW(L"ui", L"close_to_background", background ? L"1" : L"0", ini)) {
        error_code(L"Не удалось сохранить поведение приложения.", GetLastError()); return;
    }
    show_tray_icon = show; close_to_background = background; tray_apply();
    SetWindowTextW(GetDlgItem(window, 240), L"Сохранено. Настройки уже применены и сохранятся после перезапуска.");
}
static int valid_ipv4(const WCHAR *value) {
    struct in_addr address;
    return InetPtonW(AF_INET, value, &address) == 1 && address.s_addr != 0 && address.s_addr != INADDR_BROADCAST;
}
static void guess_pc_ip(void) {
    wcscpy(pc_ip, L"WINDOWS_PC_IP");
    char host[256];
    if (gethostname(host, sizeof(host))) return;
    struct addrinfo hint = {0}, *addresses = NULL;
    hint.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hint, &addresses)) return;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        struct sockaddr_in *s = (struct sockaddr_in *)a->ai_addr;
        unsigned long value = ntohl(s->sin_addr.s_addr);
        if ((value >> 24) == 127 || (value >> 16) == 0xA9FE) continue;
        InetNtopW(AF_INET, &s->sin_addr, pc_ip, 80);
        if ((value >> 16) == 0xC0A8) break;
    }
    freeaddrinfo(addresses);
}
static void refresh_displays(void) {
    int previous = (int)SendMessageW(monitor_box, CB_GETCURSEL, 0, 0);
    WCHAR wanted[1024] = L"";
    if (previous >= 0 && previous < 32) wcscpy(wanted, monitor_ids[previous]);
    else wcscpy(wanted, selected);
    SendMessageW(monitor_box, CB_RESETCONTENT, 0, 0);
    Snapshot s = {0}; LONG e = query(&s);
    if (e) { release(&s); error_code(L"Не удалось прочитать дисплеи. Запусти приложение в локальной сессии Windows.", e); return; }
    int used = 0, preferred = -1;
    for (UINT32 i = 0; i < s.np && used < 32; i++) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME name;
        if (!s.paths[i].targetInfo.targetAvailable || !name_of(&s.paths[i], &name) || !name.monitorDevicePath[0]) continue;
        int duplicate = 0;
        for (int j = 0; j < used; j++) if (!_wcsicmp(monitor_ids[j], name.monitorDevicePath)) duplicate = 1;
        if (duplicate) continue;
        wcscpy(monitor_ids[used], name.monitorDevicePath);
        WCHAR label[256];
        swprintf(label, 256, L"%ls — %ls", name.monitorFriendlyDeviceName[0] ? name.monitorFriendlyDeviceName : L"Дисплей",
            (s.paths[i].flags & DISPLAYCONFIG_PATH_ACTIVE) ? L"подключён" : L"отключён");
        SendMessageW(monitor_box, CB_ADDSTRING, 0, (LPARAM)label);
        if (wanted[0] && !_wcsicmp(wanted, name.monitorDevicePath)) preferred = used;
        else if (!wanted[0] && wcsstr(name.monitorDevicePath, L"TCL")) preferred = used;
        used++;
    }
    if (wanted[0] && preferred < 0 && used < 32) {
        wcscpy(monitor_ids[used], wanted);
        SendMessageW(monitor_box, CB_ADDSTRING, 0, (LPARAM)L"Сохранённый дисплей — сейчас не обнаружен");
        preferred = used++;
    }
    if (preferred >= 0) SendMessageW(monitor_box, CB_SETCURSEL, preferred, 0);
    else if (used) SendMessageW(monitor_box, CB_SETCURSEL, 0, 0);
    release(&s);
}
static void update_display_status(void) {
    if (!configured) { SetWindowTextW(display_text, L"Сохрани настройки, чтобы управлять выбранным дисплеем."); return; }
    int enabled, available;
    EnterCriticalSection(&display_lock);
    LONG e = get_state(&enabled, &available);
    LeaveCriticalSection(&display_lock);
    SetWindowTextW(display_text, e ? L"Не удалось прочитать состояние дисплея." : enabled ?
        L"TCL подключён к рабочему столу Windows." : available ?
        L"TCL обнаружен, но отключён от рабочего стола." :
        L"TCL сейчас не обнаружен. Проверь питание и HDMI-кабель.");
}
static void update_controls(void) {
    int running = thread != NULL;
    int ids[] = {ID_MONITOR, ID_PC, ID_HA, ID_PORT, ID_KEY, ID_SAVE, ID_REFRESH};
    for (unsigned i = 0; i < sizeof(ids)/sizeof(ids[0]); i++) EnableWindow(GetDlgItem(window, ids[i]), !running);
    EnableWindow(GetDlgItem(window, ID_START), !running && configured);
    EnableWindow(GetDlgItem(window, ID_STOP), running);
    EnableWindow(GetDlgItem(window, ID_ON), configured && !settings_dirty);
    EnableWindow(GetDlgItem(window, ID_OFF), configured && !settings_dirty);
}
static void yaml_update(void) {
    WCHAR key[100], address[80], port_string[20], text[10000];
    GetWindowTextW(key_edit, key, 100);
    GetWindowTextW(pc_edit, address, 80);
    GetWindowTextW(port_edit, port_string, 20);
    swprintf(text, 10000, L"tcl-display://%ls:%ls?key=%ls", address, port_string, key);
    SetWindowTextW(yaml_edit, text);
}
static int export_yaml(int inform) {
    WCHAR text[10000], filename[MAX_PATH];
    GetWindowTextW(yaml_edit, text, 10000);
    char utf8[20000]; int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    swprintf(filename, MAX_PATH, L"%ls\\connection.txt", folder);
    FILE *file = _wfopen(filename, L"wb");
    if (!file) { message(L"Не удалось сохранить строку подключения. Перемести папку в доступное для записи место.", MB_ICONERROR); return 0; }
    fwrite(utf8, 1, length-1, file); fclose(file);
    if (inform) message(L"Строка подключения сохранена в connection.txt рядом с приложением.", MB_ICONINFORMATION);
    return 1;
}
static int autostart_set(int enable) {
    HKEY key; LONG e = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (e) { error_code(L"Не удалось изменить автозапуск.", e); return 0; }
    if (enable) {
        WCHAR value[MAX_PATH+40]; swprintf(value, MAX_PATH+40, L"\"%ls\" --tray", executable);
        e = RegSetValueExW(key, L"TCLDisplayApp", 0, REG_SZ, (BYTE *)value, (DWORD)((wcslen(value)+1)*sizeof(WCHAR)));
    } else {
        e = RegDeleteValueW(key, L"TCLDisplayApp");
        if (e == ERROR_FILE_NOT_FOUND) e = 0;
    }
    RegCloseKey(key);
    if (e) error_code(L"Не удалось изменить автозапуск.", e);
    return e == 0;
}
static int save_settings(void) {
    if (thread) { message(L"Сначала останови обработчик.", MB_ICONINFORMATION); return 0; }
    int index = (int)SendMessageW(monitor_box, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= 32) { message(L"Выбери телевизор в списке дисплеев.", MB_ICONWARNING); return 0; }
    WCHAR key[100], port_string[20];
    GetWindowTextW(pc_edit, pc_ip, 80); GetWindowTextW(ha_edit, ha_ip, 80);
    GetWindowTextW(key_edit, key, 100); GetWindowTextW(port_edit, port_string, 20);
    WCHAR *end = NULL; long desired_port = wcstol(port_string, &end, 10);
    if (!valid_ipv4(pc_ip) || (ha_ip[0] && !valid_ipv4(ha_ip))) {
        message(L"Укажи IPv4 компьютера в домашней сети. Для брандмауэра также нужен IPv4 Home Assistant.", MB_ICONWARNING); return 0;
    }
    if (!end || *end || desired_port < 1024 || desired_port > 65535) { message(L"Порт должен быть числом от 1024 до 65535.", MB_ICONWARNING); return 0; }
    if (wcslen(key) != 64 || wcsspn(key, L"0123456789abcdefABCDEF") != 64) { message(L"Ключ должен содержать 64 шестнадцатеричных символа. Сохрани предложенный ключ.", MB_ICONWARNING); return 0; }
    int ok = WritePrivateProfileStringW(L"agent", L"monitor", monitor_ids[index], ini) &&
        WritePrivateProfileStringW(L"agent", L"key", key, ini) &&
        WritePrivateProfileStringW(L"agent", L"port", port_string, ini) &&
        WritePrivateProfileStringW(L"ui", L"pc_ip", pc_ip, ini) &&
        WritePrivateProfileStringW(L"ui", L"ha_ip", ha_ip, ini) &&
        WritePrivateProfileStringW(L"ui", L"start_server", SendMessageW(run_box, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", ini);
    if (!ok) { error_code(L"Не удалось сохранить agent.ini.", GetLastError()); return 0; }
    if (!autostart_set(SendMessageW(auto_box, BM_GETCHECK, 0, 0) == BST_CHECKED)) return 0;
    configured = load_config();
    yaml_update(); if (!export_yaml(0)) return 0;
    settings_dirty = 0;
    update_controls(); update_display_status();
    SetWindowTextW(status_text, L"Настройки сохранены. Можно запускать обработчик.");
    return 1;
}
static DWORD WINAPI network_thread(LPVOID unused) {
    (void)unused;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&exclusive, sizeof(exclusive));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_port = htons((u_short)port);
#ifdef TCL_TEST_BUILD
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#else
    address.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
    if (listener == INVALID_SOCKET || bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 4)) {
        LONG e = WSAGetLastError(); if (listener != INVALID_SOCKET) closesocket(listener);
        PostMessageW(window, WM_AGENT, 2, e); return 1;
    }
    PostMessageW(window, WM_AGENT, 1, 0);
    while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
        fd_set reads; FD_ZERO(&reads); FD_SET(listener, &reads);
        struct timeval wait = {1,0};
        int result = select(0, &reads, NULL, NULL, &wait);
        if (result < 0) break;
        if (!result) continue;
        SOCKET client = accept(listener, NULL, NULL);
        if (client == INVALID_SOCKET) break;
        EnterCriticalSection(&display_lock); handle(client); LeaveCriticalSection(&display_lock);
        shutdown(client, SD_BOTH); closesocket(client);
        PostMessageW(window, WM_AGENT, 3, 0);
    }
    closesocket(listener); PostMessageW(window, WM_AGENT, 4, 0); return 0;
}
static void start_server(void) {
    if (thread) return;
    if ((!configured || settings_dirty) && !save_settings()) return;
    ResetEvent(stop_event);
    thread = CreateThread(NULL, 0, network_thread, NULL, 0, NULL);
    if (!thread) { error_code(L"Не удалось запустить обработчик.", GetLastError()); return; }
    SetWindowTextW(status_text, L"Запуск обработчика…"); update_controls();
}
static void stop_server(void) {
    if (!thread) return;
    SetEvent(stop_event);
    if (WaitForSingleObject(thread, 12000) != WAIT_OBJECT_0) {
        message(L"Обработчик завершает текущий запрос. Подожди несколько секунд и повтори остановку.", MB_ICONINFORMATION); return;
    }
    CloseHandle(thread); thread = NULL;
    SetWindowTextW(status_text, L"Обработчик остановлен."); update_controls();
}
static void toggle_display(int enable) {
    if (settings_dirty) { message(L"Сначала сохрани изменённые настройки.", MB_ICONINFORMATION); return; }
    if (!configured) { message(L"Сначала сохрани настройки.", MB_ICONINFORMATION); return; }
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    EnterCriticalSection(&display_lock); LONG e = change_display(enable); LeaveCriticalSection(&display_lock);
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    if (e) error_code(enable ? L"Не удалось подключить TCL. Выбери HDMI 3 на ТВ и повтори." : L"Не удалось отключить TCL.", e);
    update_display_status();
}
static DWORD run_netsh(WCHAR *arguments) {
    WCHAR system[MAX_PATH], cmd[4096]; GetSystemDirectoryW(system, MAX_PATH);
    swprintf(cmd, 4096, L"\"%ls\\netsh.exe\" %ls", system, arguments);
    STARTUPINFOW startup = {0}; PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, folder, &startup, &process)) return GetLastError();
    WaitForSingleObject(process.hProcess, INFINITE); DWORD code; GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess); CloseHandle(process.hThread); return code;
}
static int firewall_helper(void) {
    if (!load_config()) return 1;
    GetPrivateProfileStringW(L"ui", L"ha_ip", L"", ha_ip, 80, ini);
    if (!valid_ipv4(ha_ip)) return 1;
    WCHAR remove[] = L"advfirewall firewall delete rule name=\"TCL Display App - HA\"";
    run_netsh(remove);
    WCHAR arguments[2048];
    swprintf(arguments, 2048, L"advfirewall firewall add rule name=\"TCL Display App - HA\" dir=in action=allow program=\"%ls\" protocol=TCP localport=%d remoteip=%ls profile=private enable=yes", executable, port, ha_ip);
    DWORD e = run_netsh(arguments);
    MessageBoxW(NULL, e ? L"Не удалось добавить правило брандмауэра." : L"Доступ разрешён только с IPv4 Home Assistant, только для частной сети Windows.", L"TCL Display", e ? MB_ICONERROR : MB_ICONINFORMATION);
    return (int)e;
}
static void open_file(const WCHAR *name) {
    WCHAR file[MAX_PATH]; swprintf(file, MAX_PATH, L"%ls\\%ls", folder, name);
    ShellExecuteW(window, L"open", file, NULL, folder, SW_SHOWNORMAL);
}
static void copy_yaml(void) {
    WCHAR text[10000]; GetWindowTextW(yaml_edit, text, 10000);
    SIZE_T size = (wcslen(text)+1)*sizeof(WCHAR);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return;
    void *target = GlobalLock(memory); memcpy(target, text, size); GlobalUnlock(memory);
    if (OpenClipboard(window)) {
        EmptyClipboard(); if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        CloseClipboard(); SetWindowTextW(GetDlgItem(window, 220), L"Строка скопирована. В HA добавь интеграцию TCL Display и вставь её в мастер настройки.");
    } else GlobalFree(memory);
}
static void build_ui(void) {
    font = CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    title_font = CreateFontW(-px(23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HWND title = CreateWindowW(L"STATIC", L"TCL Display", WS_CHILD|WS_VISIBLE,px(24),px(18),px(700),px(35),window,NULL,instance,NULL);
    SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
    HWND subtitle = CreateWindowW(L"STATIC", L"Управление дисплеем Windows из Home Assistant",WS_CHILD|WS_VISIBLE,px(25),px(57),px(700),px(25),window,NULL,instance,NULL);
    SendMessageW(subtitle,WM_SETFONT,(WPARAM)font,TRUE);
    tab = CreateWindowW(WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,px(20),px(94),px(738),px(36),window,(HMENU)ID_TAB,instance,NULL);
    SendMessageW(tab,WM_SETFONT,(WPARAM)font,TRUE);
    TCITEMW item = {0}; item.mask=TCIF_TEXT; item.pszText=L"  Приложение  "; TabCtrl_InsertItem(tab,0,&item); item.pszText=L"  Настройка HA  "; TabCtrl_InsertItem(tab,1,&item); item.pszText=L"  Поведение  "; TabCtrl_InsertItem(tab,2,&item);
    control(0,L"STATIC",L"Дисплей телевизора",0,28,150,600,22,0);
    monitor_box=control(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,28,176,580,220,ID_MONITOR);
    control(0,L"BUTTON",L"Обновить",WS_TABSTOP,620,174,126,30,ID_REFRESH);
    display_text=control(0,L"STATIC",L"",0,28,214,718,32,0);
    control(0,L"STATIC",L"IPv4 этого ПК в домашней сети",0,28,254,340,22,0);
    control(0,L"STATIC",L"IPv4 Home Assistant",0,402,254,340,22,0);
    pc_edit=control(0,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,28,281,344,30,ID_PC);
    ha_edit=control(0,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,402,281,344,30,ID_HA);
    control(0,L"STATIC",L"Порт",0,28,327,100,22,0);
    control(0,L"STATIC",L"Ключ доступа",0,162,327,400,22,0);
    port_edit=control(0,L"EDIT",L"8766",ES_NUMBER|WS_TABSTOP,28,352,110,30,ID_PORT);
    key_edit=control(0,L"EDIT",L"",ES_AUTOHSCROLL|ES_PASSWORD|WS_TABSTOP,162,352,440,30,ID_KEY);
    control(0,L"BUTTON",L"Показать",BS_AUTOCHECKBOX|WS_TABSTOP,618,352,128,30,ID_SHOW);
    auto_box=control(0,L"BUTTON",L"Запускать вместе с Windows в фоне",BS_AUTOCHECKBOX|WS_TABSTOP,28,400,360,26,ID_AUTO);
    run_box=control(0,L"BUTTON",L"Включать обработчик при открытии",BS_AUTOCHECKBOX|WS_TABSTOP,402,400,344,26,ID_RUN);
    control(0,L"BUTTON",L"Сохранить настройки",BS_DEFPUSHBUTTON|WS_TABSTOP,28,448,220,36,ID_SAVE);
    control(0,L"BUTTON",L"Разрешить доступ HA…",WS_TABSTOP,262,448,240,36,ID_FIREWALL);
    control(0,L"BUTTON",L"Параметры экранов",WS_TABSTOP,516,448,230,36,ID_WINDOWS);
    control(0,L"BUTTON",L"Запустить обработчик",WS_TABSTOP,28,509,220,36,ID_START);
    control(0,L"BUTTON",L"Остановить",WS_TABSTOP,262,509,150,36,ID_STOP);
    control(0,L"BUTTON",L"Подключить TCL",WS_TABSTOP,28,566,220,36,ID_ON);
    control(0,L"BUTTON",L"Отключить TCL",WS_TABSTOP,262,566,220,36,ID_OFF);
    control(0,L"STATIC",L"Эти две кнопки меняют конфигурацию дисплеев Windows.",0,28,614,718,24,0);
    control(0,L"STATIC",L"Действие крестика и значок трея настраиваются на вкладке «Поведение».",0,28,646,718,24,0);

    control(1,L"STATIC",L"1. Установи интеграцию TCL Display через HACS и перезапусти HA.\r\n2. Сохрани настройки ПК, разреши доступ HA и запусти обработчик.\r\n3. Скопируй строку ниже в мастер добавления интеграции.\r\n    В HA появятся управление дисплеем и режим основного экрана.",0,28,148,718,90,0);
    yaml_edit=control(1,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,28,249,718,110,210);
    control(1,L"BUTTON",L"Копировать строку",WS_TABSTOP,28,379,200,34,ID_COPY);
    control(1,L"BUTTON",L"Сохранить в файл",WS_TABSTOP,242,379,200,34,ID_EXPORT);
    control(1,L"BUTTON",L"Открыть папку",WS_TABSTOP,456,379,200,34,ID_FILES);
    control(1,L"STATIC",L"В Home Assistant: Настройки → Устройства и службы → Добавить\r\nинтеграцию → TCL Display. Вставь строку подключения.\r\n\r\n«Дисплей Windows» подключает и отключает экран.\r\n«Делать ТВ основным» — отдельный режим: при подключении\r\nТВ станет основным, при отключении вернётся прежний экран.\r\nСвязь с HDMI и своим медиаплеером настрой отдельно в HA.\r\n\r\nСтрока содержит ключ доступа. Передавай её только своему HA.",0,28,448,718,190,220);
    control(1,L"BUTTON",L"Инструкция HA",WS_TABSTOP,28,672,200,32,230);
    control(1,L"BUTTON",L"Репозиторий HACS",WS_TABSTOP,242,672,200,32,231);
    control(2,L"STATIC",L"Значок в области уведомлений",0,28,153,700,26,0);
    tray_box=control(2,L"BUTTON",L"Показывать иконку в трее",BS_AUTOCHECKBOX|WS_TABSTOP,28,192,700,30,ID_TRAY_VISIBLE);
    control(2,L"STATIC",L"При нажатии крестика",0,28,250,700,26,0);
    close_box=control(2,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,28,286,700,180,ID_CLOSE_ACTION);
    SendMessageW(close_box,CB_ADDSTRING,0,(LPARAM)L"Продолжать работать в фоне");
    SendMessageW(close_box,CB_ADDSTRING,0,(LPARAM)L"Полностью закрывать приложение");
    control(2,L"STATIC",L"В фоновом режиме окно скрыто, а обработчик продолжает принимать\r\nкоманды Home Assistant. Значок в трее можно отключить независимо.\r\n\r\nЧтобы вернуть окно без значка, снова запусти TCL-Display.exe.\r\nВторой экземпляр не создаётся: откроются настройки работающего.\r\n\r\n«Завершить» внизу окна всегда полностью останавливает приложение.",0,28,340,718,160,0);
    control(2,L"BUTTON",L"Применить поведение",WS_TABSTOP,28,526,250,38,ID_SAVE_BEHAVIOR);
    control(2,L"STATIC",L"Эти настройки можно менять при работающем обработчике.",0,28,586,718,55,240);
    primary_box=control(2,L"BUTTON",L"При подключении делать ТВ основным",BS_AUTOCHECKBOX|WS_TABSTOP,28,640,718,26,ID_PRIMARY_MODE);
    control(2,L"STATIC",L"Режим сохраняется сразу; им также управляет отдельный переключатель HA.\r\nПри отключении ТВ вернётся прежний основной монитор.",0,28,674,718,44,0);
    status_text=CreateWindowW(L"STATIC",L"Обработчик остановлен.",WS_CHILD|WS_VISIBLE,px(28),px(730),px(556),px(40),window,NULL,instance,NULL);
    SendMessageW(status_text,WM_SETFONT,(WPARAM)font,TRUE);
    HWND quit=CreateWindowW(L"BUTTON",L"Завершить",WS_CHILD|WS_VISIBLE|WS_TABSTOP,px(610),px(730),px(136),px(32),window,(HMENU)ID_QUIT_BUTTON,instance,NULL);
    SendMessageW(quit,WM_SETFONT,(WPARAM)font,TRUE);
}
static void load_ui(void) {
    configured=load_config();
    if (!configured) {
        unsigned char bytes[32];
        if (BCryptGenRandom(NULL,bytes,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)) { message(L"Не удалось создать ключ доступа.",MB_ICONERROR); return; }
        for (int i=0;i<32;i++) sprintf(secret+2*i,"%02x",bytes[i]);
        port=8766;
    }
    WCHAR key[100], port_string[20]; MultiByteToWideChar(CP_UTF8,0,secret,-1,key,100);
    swprintf(port_string,20,L"%d",port);
    SetWindowTextW(key_edit,key); SetWindowTextW(port_edit,port_string);
    guess_pc_ip();
    WCHAR saved_ip[80]; GetPrivateProfileStringW(L"ui",L"pc_ip",pc_ip,saved_ip,80,ini); wcscpy(pc_ip,saved_ip);
    GetPrivateProfileStringW(L"ui",L"ha_ip",L"",ha_ip,80,ini);
    SetWindowTextW(pc_edit,pc_ip); SetWindowTextW(ha_edit,ha_ip);
    SendMessageW(run_box,BM_SETCHECK,GetPrivateProfileIntW(L"ui",L"start_server",1,ini)?BST_CHECKED:BST_UNCHECKED,0);
    HKEY reg; int auto_enabled=0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&reg)==0) {
        auto_enabled=RegQueryValueExW(reg,L"TCLDisplayApp",NULL,NULL,NULL,NULL)==0; RegCloseKey(reg);
    }
    SendMessageW(auto_box,BM_SETCHECK,auto_enabled?BST_CHECKED:BST_UNCHECKED,0);
    behavior_load();
    SendMessageW(primary_box, BM_SETCHECK, primary_mode_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    refresh_displays(); update_display_status(); yaml_update();
    settings_dirty = 0;
    update_controls();
}
static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if (taskbar_created && msg == taskbar_created) { tray_present=0;tray_apply();return 0; }
    switch(msg) {
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w,GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)w,GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CREATE: window=hwnd; build_ui(); load_ui(); return 0;
    case WM_NOTIFY:
        if (((NMHDR *)l)->idFrom==ID_TAB && ((NMHDR *)l)->code==TCN_SELCHANGE) page_select(TabCtrl_GetCurSel(tab));
        return 0;
    case WM_COMMAND: {
        int id=LOWORD(w);
        if ((id==ID_TRAY_VISIBLE && HIWORD(w)==BN_CLICKED) ||
            (id==ID_CLOSE_ACTION && HIWORD(w)==CBN_SELCHANGE))
            SetWindowTextW(GetDlgItem(hwnd,240),L"Есть изменения. Нажми «Применить поведение».");
        if ((HIWORD(w)==EN_CHANGE && (id==ID_PC||id==ID_HA||id==ID_PORT||id==ID_KEY)) ||
            (id==ID_MONITOR&&HIWORD(w)==CBN_SELCHANGE)) { settings_dirty=1;update_controls(); }
        if (HIWORD(w)==EN_CHANGE && yaml_edit && (id==ID_PC||id==ID_PORT||id==ID_KEY)) yaml_update();
        if (HIWORD(w)!=BN_CLICKED) return 0;
        switch(id) {
        case ID_SAVE: save_settings(); break;
        case ID_PRIMARY_MODE: {
            EnterCriticalSection(&display_lock);
            BOOL ok = WritePrivateProfileStringW(L"display", L"make_primary",
                SendMessageW(primary_box, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", ini);
            LeaveCriticalSection(&display_lock);
            if (!ok) { error_code(L"Не удалось сохранить режим основного экрана.", GetLastError());
                SendMessageW(primary_box, BM_SETCHECK, primary_mode_enabled() ? BST_CHECKED : BST_UNCHECKED, 0); }
            break;
        }
        case ID_SAVE_BEHAVIOR: behavior_save(); break;
        case ID_AUTO: case ID_RUN: settings_dirty=1; update_controls(); break;
        case ID_REFRESH: refresh_displays(); update_display_status(); break;
        case ID_SHOW: SendMessageW(key_edit,EM_SETPASSWORDCHAR,SendMessageW(GetDlgItem(hwnd,ID_SHOW),BM_GETCHECK,0,0)==BST_CHECKED?0:L'●',0); InvalidateRect(key_edit,NULL,TRUE); break;
        case ID_START: start_server(); break;
        case ID_STOP: stop_server(); break;
        case ID_ON: toggle_display(1); break;
        case ID_OFF: toggle_display(0); break;
        case ID_COPY: copy_yaml(); break;
        case ID_EXPORT: yaml_update(); export_yaml(1); break;
        case ID_FILES: ShellExecuteW(hwnd,L"open",folder,NULL,NULL,SW_SHOWNORMAL); break;
        case ID_WINDOWS: ShellExecuteW(hwnd,L"open",L"ms-settings:display",NULL,NULL,SW_SHOWNORMAL); break;
        case ID_FIREWALL:
            if (thread) { message(L"Останови обработчик, сохрани IPv4 Home Assistant и повтори.",MB_ICONINFORMATION); break; }
            if (!save_settings()) break;
            if (!valid_ipv4(ha_ip)) { message(L"Укажи IPv4 Home Assistant и сохрани настройки.",MB_ICONWARNING); break; }
            { HINSTANCE result=ShellExecuteW(hwnd,L"runas",executable,L"--firewall",folder,SW_SHOWNORMAL);
              if ((INT_PTR)result<=32) message(L"Настройка брандмауэра не выполнена. Для неё нужно подтвердить запрос Windows.",MB_ICONWARNING); }
            break;
        case 230: open_file(L"НАСТРОЙКА-HA.txt"); break;
        case 231: ShellExecuteW(hwnd,L"open",L"https://github.com/BrainDeLook/tcl-display",NULL,NULL,SW_SHOWNORMAL); break;
        case ID_OPEN: ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd); break;
        case ID_EXIT: case ID_QUIT_BUTTON: stop_server(); if (!thread) DestroyWindow(hwnd); break;
        }
        return 0;
    }
    case WM_AGENT:
        if (w==1) { WCHAR text[256]; swprintf(text,256,L"Обработчик работает · порт %d · ключ доступа включён",port); SetWindowTextW(status_text,text); }
        if (w==2) { if(thread){WaitForSingleObject(thread,1000);CloseHandle(thread);thread=NULL;} update_controls();SetWindowTextW(status_text,L"Не удалось запустить обработчик.");error_code(L"Не удалось открыть порт. Возможно, он уже занят.",(LONG)l); }
        if (w==3) update_display_status();
        return 0;
    case WM_TRAY:
        if (l==WM_LBUTTONDBLCLK) { ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd); }
        if (l==WM_RBUTTONUP) {
            HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,ID_OPEN,L"Открыть настройки");AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,ID_EXIT,L"Выход");
            POINT point;GetCursorPos(&point);SetForegroundWindow(hwnd);TrackPopupMenu(menu,TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,NULL);DestroyMenu(menu);
        }
        return 0;
    case WM_CLOSE:
        if(close_to_background) ShowWindow(hwnd,SW_HIDE);
        else { stop_server();if(!thread)DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY: if(tray_present)Shell_NotifyIconW(NIM_DELETE,&tray); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
int WINAPI WinMain(HINSTANCE inst,HINSTANCE previous,LPSTR command,int show) {
    (void)previous; (void)show;
    instance=inst; paths_init();GetModuleFileNameW(NULL,executable,MAX_PATH);
    if (!strcmp(command,"--firewall")) return firewall_helper();
    if (!strcmp(command,"--dry-run")) { dry=1;return serve(); }
    HANDLE single=CreateMutexW(NULL,FALSE,APP_MUTEX_NAME);
    if (!single||GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(APP_CLASS_NAME,NULL); if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);} if(single)CloseHandle(single); return 0;
    }
    SetProcessDPIAware(); HDC dc=GetDC(NULL);scale=MulDiv(GetDeviceCaps(dc,LOGPIXELSX),100,96);ReleaseDC(NULL,dc);
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_TAB_CLASSES};InitCommonControlsEx(&common);
    InitializeCriticalSection(&display_lock);stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    WSADATA winsock; if(WSAStartup(MAKEWORD(2,2),&winsock)) return 1;
    WNDCLASSW cls={0};cls.lpfnWndProc=procedure;cls.hInstance=inst;cls.lpszClassName=APP_CLASS_NAME;cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);cls.hIcon=LoadIconW(NULL,IDI_APPLICATION);RegisterClassW(&cls);
    RECT rect={0,0,px(780),px(784)};AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    window=CreateWindowW(cls.lpszClassName,L"TCL Display 1.5 — настройки",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,inst,NULL);
    if(!window)return 1;
    tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=cls.hIcon;wcscpy(tray.szTip,L"TCL Display — управление из Home Assistant");
    taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");tray_apply();
    if(strcmp(command,"--tray")||!configured) ShowWindow(window,SW_SHOW);
    if(configured&&SendMessageW(run_box,BM_GETCHECK,0,0)==BST_CHECKED)start_server();
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(thread)stop_server();CloseHandle(stop_event);DeleteCriticalSection(&display_lock);WSACleanup();CloseHandle(single);DeleteObject(font);DeleteObject(title_font);return 0;
}
