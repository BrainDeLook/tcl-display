#define WINVER 0x0A00
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Native Windows display receiver. No arbitrary command execution. */
static WCHAR folder[MAX_PATH], ini[MAX_PATH], selected[1024];
static char secret[65];
static int port = 8766, dry = 0;
static const char *last_error = "none";
typedef struct {
    DISPLAYCONFIG_PATH_INFO *paths;
    DISPLAYCONFIG_MODE_INFO *modes;
    UINT32 np, nm;
} Snapshot;

static void release(Snapshot *s) {
    if (s->paths) HeapFree(GetProcessHeap(), 0, s->paths);
    if (s->modes) HeapFree(GetProcessHeap(), 0, s->modes);
    ZeroMemory(s, sizeof(*s));
}
static LONG query(Snapshot *s) {
    LONG e = ERROR_INSUFFICIENT_BUFFER;
    for (int tries = 0; tries < 5 && e == ERROR_INSUFFICIENT_BUFFER; tries++) {
        release(s);
        e = GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &s->np, &s->nm);
        if (e) break;
        s->paths = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            (s->np + 2) * sizeof(*s->paths));
        s->modes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            (s->nm + 2) * sizeof(*s->modes));
        if (!s->paths || !s->modes) { e = ERROR_NOT_ENOUGH_MEMORY; break; }
        e = QueryDisplayConfig(QDC_ALL_PATHS, &s->np, s->paths, &s->nm, s->modes, NULL);
    }
    return e;
}
static int name_of(DISPLAYCONFIG_PATH_INFO *p, DISPLAYCONFIG_TARGET_DEVICE_NAME *n) {
    ZeroMemory(n, sizeof(*n));
    n->header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    n->header.size = sizeof(*n);
    n->header.adapterId = p->targetInfo.adapterId;
    n->header.id = p->targetInfo.id;
    return DisplayConfigGetDeviceInfo(&n->header) == ERROR_SUCCESS;
}
static int same_adapter(LUID a, LUID b) {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}
static int is_selected(DISPLAYCONFIG_PATH_INFO *p) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME n;
    return name_of(p, &n) && !_wcsicmp(n.monitorDevicePath, selected);
}
static int source_used(Snapshot *s, DISPLAYCONFIG_PATH_INFO *candidate) {
    for (UINT32 i = 0; i < s->np; i++) {
        DISPLAYCONFIG_PATH_INFO *p = &s->paths[i];
        if ((p->flags & DISPLAYCONFIG_PATH_ACTIVE) &&
            p->sourceInfo.id == candidate->sourceInfo.id &&
            same_adapter(p->sourceInfo.adapterId, candidate->sourceInfo.adapterId)) return 1;
    }
    return 0;
}
static LONG get_state(int *enabled, int *available) {
    Snapshot s = {0};
    LONG e = query(&s);
    *enabled = *available = 0;
    if (!e) for (UINT32 i = 0; i < s.np; i++) {
        if (is_selected(&s.paths[i])) {
            if (s.paths[i].targetInfo.targetAvailable) *available = 1;
            if (s.paths[i].flags & DISPLAYCONFIG_PATH_ACTIVE) *enabled = 1;
        }
    }
    release(&s);
    return e;
}
static LONG change_display(int enable) {
    Snapshot s = {0};
    DISPLAYCONFIG_PATH_INFO *active = NULL;
    LONG e = query(&s);
    if (e) { last_error = "query_failed"; release(&s); return e; }
    active = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (s.np + 1) * sizeof(*active));
    if (!active) { release(&s); return ERROR_NOT_ENOUGH_MEMORY; }
    UINT32 count = 0;
    int found = 0;
    LONG right = 0;
    for (UINT32 i = 0; i < s.np; i++) {
        DISPLAYCONFIG_PATH_INFO p = s.paths[i];
        if (!(p.flags & DISPLAYCONFIG_PATH_ACTIVE)) continue;
        int target = is_selected(&p);
        if (target) found = 1;
        if (enable || !target) active[count++] = p;
        if (p.sourceInfo.modeInfoIdx < s.nm) {
            DISPLAYCONFIG_SOURCE_MODE m = s.modes[p.sourceInfo.modeInfoIdx].sourceMode;
            LONG edge = m.position.x + (LONG)m.width;
            if (edge > right) right = edge;
        }
    }
    if ((enable && found) || (!enable && !found)) { e = 0; goto done; }
    if (!enable && count == 0) { last_error = "refuse_last_display"; e = ERROR_ACCESS_DENIED; goto done; }
    if (enable) {
        int candidate = -1;
        for (UINT32 i = 0; i < s.np; i++) {
            if (s.paths[i].targetInfo.targetAvailable && is_selected(&s.paths[i]) &&
                !source_used(&s, &s.paths[i])) { candidate = (int)i; break; }
        }
        if (candidate < 0) { last_error = "target_not_available_or_no_free_source"; e = ERROR_NOT_FOUND; goto done; }
        DISPLAYCONFIG_PATH_INFO p = s.paths[candidate];
        p.flags = DISPLAYCONFIG_PATH_ACTIVE;
        p.sourceInfo.statusFlags = 0;
        p.targetInfo.statusFlags = 0;
        p.sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        p.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        p.targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        p.targetInfo.scaling = DISPLAYCONFIG_SCALING_PREFERRED;
        p.targetInfo.refreshRate.Numerator = 0;
        p.targetInfo.refreshRate.Denominator = 1;
        p.targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
        DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred = {0};
        preferred.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE;
        preferred.header.size = sizeof(preferred);
        preferred.header.adapterId = p.targetInfo.adapterId;
        preferred.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&preferred.header) == 0 && preferred.width && preferred.height) {
            DISPLAYCONFIG_MODE_INFO m = {0};
            m.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
            m.id = p.sourceInfo.id;
            m.adapterId = p.sourceInfo.adapterId;
            m.sourceMode.width = preferred.width;
            m.sourceMode.height = preferred.height;
            m.sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
            m.sourceMode.position.x = right;
            m.sourceMode.position.y = 0;
            p.sourceInfo.modeInfoIdx = s.nm;
            s.modes[s.nm++] = m;
        }
        active[count++] = p;
    }
    UINT32 flags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
    if (enable) flags |= SDC_ALLOW_CHANGES;
    e = SetDisplayConfig(count, active, s.nm, s.modes, flags | SDC_VALIDATE);
    if (e) { last_error = "validation_failed"; goto done; }
    e = SetDisplayConfig(count, active, s.nm, s.modes, flags | SDC_APPLY | SDC_SAVE_TO_DATABASE);
    if (e) { last_error = "apply_failed"; goto done; }
    /* Never report success merely because SetDisplayConfig returned zero. */
    {
        int enabled = 0, available = 0;
        e = get_state(&enabled, &available);
        if (!e && enabled != enable) { e = ERROR_GEN_FAILURE; last_error = "verification_failed"; }
    }
done:
    if (!e) last_error = "none";
    HeapFree(GetProcessHeap(), 0, active);
    release(&s);
    return e;
}
static void print_w(const WCHAR *w) {
    char text[4096];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, text, sizeof(text), NULL, NULL);
    printf("%s", text);
}
static void paths_init(void) {
    GetModuleFileNameW(NULL, folder, MAX_PATH);
    WCHAR *slash = wcsrchr(folder, L'\\');
    if (slash) *slash = 0;
    swprintf(ini, MAX_PATH, L"%ls\\agent.ini", folder);
}
static int load_config(void) {
    WCHAR key[100];
    GetPrivateProfileStringW(L"agent", L"monitor", L"", selected, 1024, ini);
    GetPrivateProfileStringW(L"agent", L"key", L"", key, 100, ini);
    port = GetPrivateProfileIntW(L"agent", L"port", 8766, ini);
    WideCharToMultiByte(CP_UTF8, 0, key, -1, secret, sizeof(secret), NULL, NULL);
    return wcslen(selected) > 0 && strlen(secret) == 64 && port > 0 && port < 65536;
}
static int setup(int just_list) {
    Snapshot s = {0};
    LONG e = query(&s);
    if (e) { printf("Display enumeration failed: %ld. Run in your local Windows session.\n", e); release(&s); return 1; }
    WCHAR ids[32][1024];
    int used = 0;
    puts("Найденные дисплеи (номера в этом списке не равны номерам в настройках Windows):");
    for (UINT32 i = 0; i < s.np && used < 32; i++) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME n;
        if (!s.paths[i].targetInfo.targetAvailable || !name_of(&s.paths[i], &n) || !n.monitorDevicePath[0]) continue;
        int duplicate = 0;
        for (int j = 0; j < used; j++) if (!_wcsicmp(ids[j], n.monitorDevicePath)) duplicate = 1;
        if (duplicate) continue;
        wcscpy(ids[used], n.monitorDevicePath);
        printf("\n%d. ", ++used); print_w(n.monitorFriendlyDeviceName);
        printf(" [%s]\n   ", (s.paths[i].flags & DISPLAYCONFIG_PATH_ACTIVE) ? "подключён" : "отключён");
        print_w(n.monitorDevicePath); puts("");
    }
    release(&s);
    if (just_list) return 0;
    if (!used) { puts("No connected displays found. Select a PC HDMI input on the TV and retry."); return 1; }
    char line[80];
    printf("\nВыбери TCL по названию из этого списка (1-%d): ", used);
    if (!fgets(line, sizeof(line), stdin)) return 1;
    int choice = atoi(line);
    if (choice < 1 || choice > used) { puts("Invalid choice."); return 1; }
    if (load_config()) {
        puts("Существующий ключ и порт сохранены.");
    } else {
        unsigned char bytes[32];
        if (BCryptGenRandom(NULL, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) return 1;
        for (int i = 0; i < 32; i++) sprintf(secret + 2*i, "%02x", bytes[i]);
        port = 8766;
    }
    WCHAR key[100], port_text[16];
    MultiByteToWideChar(CP_UTF8, 0, secret, -1, key, 100);
    swprintf(port_text, 16, L"%d", port);
    if (!WritePrivateProfileStringW(L"agent", L"monitor", ids[choice-1], ini) ||
        !WritePrivateProfileStringW(L"agent", L"key", key, ini) ||
        !WritePrivateProfileStringW(L"agent", L"port", port_text, ini)) {
        puts("Cannot write agent.ini. Move this folder to a writable location."); return 1;
    }
    WCHAR yaml[MAX_PATH]; swprintf(yaml, MAX_PATH, L"%ls\\home-assistant.generated.yaml", folder);
    FILE *f = _wfopen(yaml, L"wb");
    if (!f) return 1;
    fprintf(f, "# Replace WINDOWS_PC_IP with your PC's LAN IPv4 address.\n"
        "# Merge into configuration.yaml; do not duplicate an existing rest_command key.\n"
        "rest_command:\n");
    const char *actions[] = {"on", "off"};
    for (int i = 0; i < 2; i++) fprintf(f,
        "  tcl_display_%s:\n    url: \"http://WINDOWS_PC_IP:8766/%s\"\n"
        "    method: POST\n    headers:\n      X-TCL-Key: \"%s\"\n    timeout: 15\n", actions[i], actions[i], secret);
    fclose(f);
    puts("\nГотово: сохранены agent.ini и home-assistant.generated.yaml. Дисплеи не переключались.");
    puts("Далее запусти Start.vbs. Для доступа из HA запусти Allow-HA.cmd от администратора.");
    return 0;
}
static int authorized(char *request) {
    char *line = strstr(request, "\r\n");
    while (line && line[2]) {
        line += 2;
        char *end = strstr(line, "\r\n");
        if (!end || end == line) break;
        if (!_strnicmp(line, "X-TCL-Key:", 10)) {
            char *v = line + 10;
            while (*v == ' ' || *v == '\t') v++;
            if (end-v != 64) return 0;
            unsigned int difference = 0;
            for (int i = 0; i < 64; i++) difference |= (unsigned char)v[i] ^ (unsigned char)secret[i];
            return difference == 0;
        }
        line = end;
    }
    return 0;
}
static void reply(SOCKET client, int status, const char *body) {
    char message[2048];
    const char *label = status == 200 ? "OK" : status == 401 ? "Unauthorized" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Error";
    int length = snprintf(message, sizeof(message),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n%s",
        status, label, (unsigned)strlen(body), body);
    int sent = 0;
    while (sent < length) { int n = send(client, message+sent, length-sent, 0); if (n <= 0) break; sent += n; }
}
static void handle(SOCKET client) {
    DWORD timeout = 3000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    char request[8192]; int total = 0;
    request[0] = 0;
    while (!strstr(request, "\r\n\r\n") && total < (int)sizeof(request)-1) {
        int n = recv(client, request+total, sizeof(request)-1-total, 0);
        if (n <= 0) return;
        total += n; request[total] = 0;
    }
    if (!strstr(request, "\r\n\r\n")) { reply(client, 400, "{\"error\":\"headers_too_large\"}"); return; }
    if (!authorized(request)) { reply(client, 401, "{\"error\":\"unauthorized\"}"); return; }
    char method[16], path[128], version[16];
    if (sscanf(request, "%15s %127s %15s", method, path, version) != 3) { reply(client, 400, "{\"error\":\"bad_request\"}"); return; }
    int cmd = !strcmp(path, "/on") ? 1 : !strcmp(path, "/off") ? 0 : -1;
    if (cmd >= 0 && strcmp(method, "POST")) { reply(client, 405, "{\"error\":\"use_post\"}"); return; }
    if (cmd < 0 && (strcmp(path, "/status") || strcmp(method, "GET"))) { reply(client, 404, "{\"error\":\"not_found\"}"); return; }
    if (dry) { reply(client, 200, "{\"ok\":true,\"dry_run\":true,\"display_changed\":false}"); return; }
    LONG e = cmd >= 0 ? change_display(cmd) : 0;
    int enabled = 0, available = 0;
    if (!e) e = get_state(&enabled, &available);
    char body[512];
    snprintf(body, sizeof(body), "{\"ok\":%s,\"enabled\":%s,\"available\":%s,\"win32_error\":%ld,\"detail\":\"%s\"}",
        e ? "false" : "true", enabled ? "true" : "false", available ? "true" : "false", e, e ? last_error : "none");
    reply(client, e ? 503 : 200, body);
}
static int serve(void) {
    if (!load_config() && !dry) { puts("Run Setup.cmd first."); return 1; }
    if (dry) {
        /* Test mode is loopback-only; display-changing APIs are never called. */
        strcpy(secret, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        port = 18766;
    }
    HANDLE lock = CreateMutexW(NULL, FALSE, dry ? L"Local\\TCLDisplayAgentDryRun" : L"Local\\TCLDisplayAgent");
    if (!lock || GetLastError() == ERROR_ALREADY_EXISTS) { if (lock) CloseHandle(lock); return 1; }
    WSADATA data;
    if (WSAStartup(MAKEWORD(2,2), &data)) return 1;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&exclusive, sizeof(exclusive));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((u_short)port);
    address.sin_addr.s_addr = htonl(dry ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 4)) {
        printf("Cannot listen on port %d, Winsock error %d\n", port, WSAGetLastError());
        closesocket(listener); WSACleanup(); CloseHandle(lock); return 1;
    }
    printf("TCL Display Agent listening on %d%s\n", port, dry ? " (dry run)" : "");
    fflush(stdout);
    /* Release a hidden console as well: no persistent conhost process is needed. */
    FreeConsole();
    for (;;) {
        SOCKET client = accept(listener, NULL, NULL);
        if (client == INVALID_SOCKET) break;
        handle(client);
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
    closesocket(listener); WSACleanup(); CloseHandle(lock); return 0;
}
int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    paths_init();
    if (argc > 1 && !strcmp(argv[1], "list")) return setup(1);
    if (argc > 1 && !strcmp(argv[1], "setup")) return setup(0);
    if (argc > 1 && !strcmp(argv[1], "--dry-run")) dry = 1;
    if (argc > 1 && (!strcmp(argv[1], "serve") || dry)) return serve();
    puts("TCL Display Agent 1.0\nCommands: setup | list | serve | --dry-run");
    return 0;
}
