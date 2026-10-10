/*
 * Command & Conquer 3: Tiberium Wars - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header), shaped like tiberiansun-recomp's. What
 * is here is only what is specific to this game: where the image goes, the
 * guest's identity and command line, the VC80 CRT it imports, and the fault
 * report. docs/bringup.md has the reasoning.
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so 0x00400000..0x013E1000 is
 * free when main() maps the image.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"

extern const uint32_t tw_entry_va;    /* recomp_dispatch.c */

#define TW_IMAGE_BASE 0x00400000u
#define TW_EXE_STAMP 0x67B6063Au     /* cnc3game.dat 1.10's PE timestamp: the build lifted (run_lift.py) */
#define TW_EXE "RetailExe\\1.10\\cnc3game.dat"
#define TW_SKUDEF "CNC3_english_1.10.SkuDef"

static DWORD g_watchdog_s;
static int   g_headless;

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
static const char* gstr(uint32_t va) { return va ? (const char*)(uintptr_t)va : "(null)"; }

/* ---- headless ------------------------------------------------------------
 * Nothing reaches the screen (REPO_RULES section 13): message boxes print and
 * answer, and a top-level window is layered at alpha 0, click-through and
 * never activated, as tiberiansun-recomp's headless mode does. */
static uint32_t mb_answer(uint32_t type) {
    uint32_t buttons = type & MB_TYPEMASK;
    return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? IDNO : IDOK;
}

static void shim_MessageBoxA(void) {
    g_eax = mb_answer(ARG(3));
    fprintf(stderr, "[messagebox] type 0x%X -> %u: %s: %s\n", ARG(3), g_eax, gstr(ARG(2)), gstr(ARG(1)));
    g_esp += 4 + 4 * 4;
}

static void shim_MessageBoxW(void) {
    g_eax = mb_answer(ARG(3));
    fprintf(stderr, "[messagebox] type 0x%X -> %u: %ls: %ls\n", ARG(3), g_eax,
            ARG(2) ? (const wchar_t*)(uintptr_t)ARG(2) : L"(null)",
            ARG(1) ? (const wchar_t*)(uintptr_t)ARG(1) : L"(null)");
    g_esp += 4 + 4 * 4;
}

#define HL_EXSTYLE (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)

static void shim_CreateWindowExW(void) {
    uint32_t style = ARG(3);
    HWND parent = (HWND)(uintptr_t)ARG(8);
    HWND h = CreateWindowExW(parent ? ARG(0) : ARG(0) | HL_EXSTYLE, (LPCWSTR)(uintptr_t)ARG(1),
                             (LPCWSTR)(uintptr_t)ARG(2), style, (int)ARG(4), (int)ARG(5), (int)ARG(6),
                             (int)ARG(7), parent, (HMENU)(uintptr_t)ARG(9),
                             (HINSTANCE)(uintptr_t)ARG(10), (LPVOID)(uintptr_t)ARG(11));
    if (h && !parent) SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    fprintf(stderr, "[headless] CreateWindowExW(%ls) style 0x%08X -> %p\n",
            ARG(2) ? (const wchar_t*)(uintptr_t)ARG(2) : L"", style, (void*)h);
    g_eax = (uint32_t)(uintptr_t)h;
    g_esp += 4 + 12 * 4;
}

/* ---- the guest's identity ------------------------------------------------
 * The guest is RetailExe\1.10\cnc3game.dat, not this host. Its hInstance
 * comes from GetModuleHandle(NULL), and it finds its files relative to
 * GetModuleFileName. Its command line is the launcher's: CNC3.exe starts it
 * with -config and the SkuDef, which lists the data's config.txt files. */
static char g_guest_exe[MAX_PATH], g_guest_cmdline[2 * MAX_PATH + 64];
static wchar_t g_guest_exe_w[MAX_PATH], g_guest_cmdline_w[2 * MAX_PATH + 64];

static void shim_GetModuleHandleA(void) {
    g_eax = ARG(0) ? (uint32_t)(uintptr_t)GetModuleHandleA((LPCSTR)(uintptr_t)ARG(0)) : TW_IMAGE_BASE;
    g_esp += 4 + 1 * 4;
}

/* The game looks up its own exports at startup (sub_00402E21): from the
 * process exe (GetModuleHandleExA with no name) and from the module holding
 * its own code (FROM_ADDRESS 0x00402E21), then GetProcAddress on each. Both
 * mean the guest image, which Windows does not know as a module, so they are
 * answered here from its export table (docs/bringup.md, 3). */
static void shim_GetModuleHandleExA(void) {
    uint32_t flags = ARG(0), name = ARG(1), *out = (uint32_t*)(uintptr_t)ARG(2);
    if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) ? native32_in_guest(name) : !name) {
        *out = TW_IMAGE_BASE;
        g_eax = 1;
    } else {
        g_eax = GetModuleHandleExA(flags, (LPCSTR)(uintptr_t)name, (HMODULE*)out);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetProcAddress(void) {
    uint32_t h = ARG(0), name = ARG(1);
    g_eax = h == TW_IMAGE_BASE ? native32_export(h, (const char*)(uintptr_t)name)
                               : (uint32_t)(uintptr_t)GetProcAddress((HMODULE)(uintptr_t)h, (LPCSTR)(uintptr_t)name);
    g_esp += 4 + 2 * 4;
}

static void shim_FreeLibrary(void) {
    g_eax = ARG(0) == TW_IMAGE_BASE ? 1 : FreeLibrary((HMODULE)(uintptr_t)ARG(0));
    g_esp += 4 + 1 * 4;
}

/* The game's own exits (a /GS failure, a fatal error) end in TerminateProcess:
 * say so, and flush --calltrace, whose newest entries are the ones that matter. */
static void shim_TerminateProcess(void) {
    fprintf(stderr, "\n[exit] TerminateProcess(0x%X) from sub_%08X\n", ARG(1), g_cur_func);
    native32_dump_icalls(8);
    recomp_trace_flush();
    TerminateProcess((HANDLE)(uintptr_t)ARG(0), ARG(1));
    g_eax = 1;
    g_esp += 4 + 2 * 4;
}

static uint32_t copy_out(void* out, const void* src, uint32_t n, uint32_t size, int wide) {
    if (!size) return n;
    uint32_t k = n < size ? n : size - 1;
    memcpy(out, src, (size_t)k << wide);
    if (wide) ((wchar_t*)out)[k] = 0; else ((char*)out)[k] = 0;
    return k;
}

static void shim_GetModuleFileNameA(void) {
    uint32_t h = ARG(0);
    if (h == 0 || h == TW_IMAGE_BASE)
        g_eax = copy_out((void*)(uintptr_t)ARG(1), g_guest_exe, (uint32_t)strlen(g_guest_exe), ARG(2), 0);
    else
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)h, (char*)(uintptr_t)ARG(1), ARG(2));
    g_esp += 4 + 3 * 4;
}

static void shim_GetModuleFileNameW(void) {
    uint32_t h = ARG(0);
    if (h == 0 || h == TW_IMAGE_BASE)
        g_eax = copy_out((void*)(uintptr_t)ARG(1), g_guest_exe_w, (uint32_t)wcslen(g_guest_exe_w), ARG(2), 1);
    else
        g_eax = GetModuleFileNameW((HMODULE)(uintptr_t)h, (wchar_t*)(uintptr_t)ARG(1), ARG(2));
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) { g_eax = (uint32_t)(uintptr_t)g_guest_cmdline; g_esp += 4; }
static void shim_GetCommandLineW(void) { g_eax = (uint32_t)(uintptr_t)g_guest_cmdline_w; g_esp += 4; }

#define GUEST_SHIMS \
    { "GetModuleHandleA", shim_GetModuleHandleA }, \
    { "GetModuleHandleExA", shim_GetModuleHandleExA }, \
    { "GetProcAddress", shim_GetProcAddress }, \
    { "FreeLibrary", shim_FreeLibrary }, \
    { "TerminateProcess", shim_TerminateProcess }, \
    { "GetModuleFileNameA", shim_GetModuleFileNameA }, \
    { "GetModuleFileNameW", shim_GetModuleFileNameW }, \
    { "GetCommandLineA", shim_GetCommandLineA }, \
    { "GetCommandLineW", shim_GetCommandLineW }

static native32_shim_t g_shims[] = { GUEST_SHIMS };
static native32_shim_t g_headless_shims[] = {
    GUEST_SHIMS,
    { "MessageBoxA", shim_MessageBoxA },
    { "MessageBoxW", shim_MessageBoxW },
    { "CreateWindowExW", shim_CreateWindowExW },
};

/* ---- the VC80 CRT ----------------------------------------------------------
 * The game imports MSVCR80.dll, which lives only in WinSxS: LoadLibrary finds
 * it through the activation context of the exe's own manifest (resource 1),
 * which Windows builds for a process's exe and not for an image we map. So
 * the guest's manifest is activated here, before its imports bind, and stays
 * active for the run. */
static int activate_guest_manifest(const char* exe) {
    ACTCTXA ac = { sizeof ac };
    ULONG_PTR cookie;
    ac.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    ac.lpSource = exe;
    ac.lpResourceName = MAKEINTRESOURCEA(1);
    HANDLE h = CreateActCtxA(&ac);
    if (h == INVALID_HANDLE_VALUE || !ActivateActCtx(h, &cookie)) {
        fprintf(stderr, "cannot activate %s's manifest (error %lu): the Visual C++ 2005 runtime "
                "(MSVCR80) is needed\n", exe, GetLastError());
        return 1;
    }
    return 0;
}

/* MSVCR80 runs natively and read the process's command line when it loaded:
 * the host's. Its WinMain startup takes the command line from _acmdln, so the
 * guest's goes there before the entry point runs. */
static void set_crt_cmdline(void) {
    HMODULE crt = GetModuleHandleA("MSVCR80.dll");
    char** acmdln = crt ? (char**)GetProcAddress(crt, "_acmdln") : NULL;
    if (acmdln) *acmdln = g_guest_cmdline;
    else fprintf(stderr, "warning: MSVCR80's _acmdln not found; the game sees the host's command line\n");
}

/* ---- diagnostics ---------------------------------------------------------
 * --probe VA (repeatable): report indirect calls to VA -- a virtual method or
 * callback -- with `this` and the first arguments, the first five calls.
 * RECOMP_ICALL asks this hook before the dispatch table. */
#define MAX_PROBES 8
static uint32_t g_probe[MAX_PROBES];
static int g_nprobe;
static volatile LONG g_probe_hits[MAX_PROBES];

recomp_func_t recomp_lookup_manual(uint32_t va) {
    for (int i = 0; i < g_nprobe; i++)
        if (g_probe[i] == va && InterlockedIncrement(&g_probe_hits[i]) <= 5)
            fprintf(stderr, "[probe] sub_%08X from sub_%08X  ecx=%08X edx=%08X  args %08X %08X %08X %08X\n",
                    va, g_cur_func, g_ecx, g_edx, MEM32(g_esp), MEM32(g_esp + 4), MEM32(g_esp + 8),
                    MEM32(g_esp + 12));
    return NULL;
}

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr,
        "\n[not-lifted] sub_%08X  (called from 0x%08X)\n"
        "  Widen the closure:  py -3 run_lift.py --roots 0x%08X  (or --max N, or --all)\n",
        va, g_cur_func, va);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

/* After native32's own handler, so callbacks are resolved first and only real
 * faults get here. Written with WriteFile from a static buffer, not stdio: a
 * fault while another thread holds stderr's lock otherwise reports nothing. */
static char g_crash_buf[16384];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode,
               r->ExceptionAddress, GetCurrentThreadId());
    {
        HMODULE m = NULL;
        char path[MAX_PATH] = "";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)r->ExceptionAddress, &m) && GetModuleFileNameA(m, path, sizeof path))
            crash_emit("  in %s at +0x%lX\n", path, (unsigned long)((uintptr_t)r->ExceptionAddress - (uintptr_t)m));
        else
            crash_emit("  in no module (heap or generated code)\n");
    }
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X%s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at,
                   native32_in_guest(at) ? " (inside the guest image)" : at < 0x10000 ? " (null/low)" : "");
    }
    crash_emit("  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 40 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
    crash_emit("guest stack at %08X:", g_esp);
    for (int k = 0; k < 64; k++) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)(uintptr_t)(g_esp + 4 * k), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) break;
        crash_emit("%s%08X", k % 8 ? " " : "\n  ", MEM32(g_esp + 4 * k));
    }
    crash_emit("\n");
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    recomp_trace_flush();
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    recomp_trace_flush();
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

int main(int argc, char** argv) {
    const char* game = "game";
    const char* extra = "";             /* --gameargs "...": appended to the guest's command line */
    char exe_full[MAX_PATH], game_full[MAX_PATH], exe_dir[MAX_PATH];
    int run = 0;
    /* Unbuffered: the log is the evidence, and ExitProcess flushes nothing
     * with the C runtime linked in (/MT). */
    setvbuf(stderr, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--gameargs") && i + 1 < argc) extra = argv[++i];
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc && g_nprobe < MAX_PROBES)
            g_probe[g_nprobe++] = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else {
            printf("usage: tw [--run] [--headless] [--game DIR] [--gameargs \"-win -xres 1280 ...\"]\n"
                   "          [--watchdog S] [--probe VA] [--native-trace] [--callbacks]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(game, MAX_PATH, game_full, NULL);
    _snprintf(exe_full, sizeof exe_full - 1, "%s\\" TW_EXE, game_full);
    strcpy_s(g_guest_exe, sizeof g_guest_exe, exe_full);
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1, "\"%s\" -config \"%s\\" TW_SKUDEF "\"%s%s",
              g_guest_exe, game_full, *extra ? " " : "", extra);
    MultiByteToWideChar(CP_ACP, 0, g_guest_exe, -1, g_guest_exe_w, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, g_guest_cmdline, -1, g_guest_cmdline_w,
                        sizeof g_guest_cmdline_w / sizeof g_guest_cmdline_w[0]);
    /* steam_api.dll sits beside the game exe. */
    strcpy_s(exe_dir, sizeof exe_dir, exe_full);
    *strrchr(exe_dir, '\\') = 0;
    SetDllDirectoryA(exe_dir);

    native32_init();
    AddVectoredExceptionHandler(0, crash);
    printf("Tiberium Wars recomp host\n  lifted functions in dispatch: %u\n", recomp_dispatch_count);

    {   /* The lifted C is one build's: another crashes in game. */
        uint8_t head[4096] = { 0 };
        FILE* f = fopen(exe_full, "rb");
        if (f) { fread(head, 1, sizeof head, f); fclose(f); }
        uint32_t nt = *(const uint32_t*)(head + 0x3C);
        uint32_t stamp = nt < sizeof head - 12 ? *(const uint32_t*)(head + nt + 8) : 0;
        if (stamp != TW_EXE_STAMP) {
            fprintf(stderr, "%s is not the build this host was lifted from (PE timestamp 0x%08X, "
                    "wanted 0x%08X). Lift and build from the Steam release (README, Getting Started).\n",
                    exe_full, stamp, TW_EXE_STAMP);
            return 1;
        }
    }
    if (activate_guest_manifest(exe_full)) return 1;
    uint32_t span = native32_map(exe_full, TW_IMAGE_BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, TW_IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X-0x%08X\n", TW_EXE, TW_IMAGE_BASE, TW_IMAGE_BASE + span);
    native32_shim_t* shims = g_headless ? g_headless_shims : g_shims;
    int nshims = g_headless ? (int)(sizeof g_headless_shims / sizeof g_headless_shims[0])
                            : (int)(sizeof g_shims / sizeof g_shims[0]);
    if (native32_bind(TW_IMAGE_BASE, shims, nshims)) return 1;
    set_crt_cmdline();
    printf("  guest command line %s\n", g_guest_cmdline);

    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", tw_entry_va);
        return 0;
    }
    /* The SkuDef's paths are relative to the game folder. */
    if (!SetCurrentDirectoryA(game_full)) { fprintf(stderr, "cannot enter %s\n", game_full); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    printf("  entering 0x%08X\n\n", tw_entry_va);
    fflush(stdout);
    native32_call_guest(tw_entry_va, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}
