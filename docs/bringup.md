# Bring-up

How far the lifted game gets, and each wall on the way, newest last. Every
entry is something the next reader would otherwise spend a day on.

## 1. Running the game without its launcher

`CNC3.exe` is a launcher; the game is `RetailExe\1.10\cnc3game.dat`, started
with `-config "<install>\CNC3_english_1.10.SkuDef"` (docs/RECON.md). The host
builds that command line itself and hands it out three ways, because the
game reads it through all three:

- the `GetCommandLineA` / `GetCommandLineW` imports, shimmed;
- `MSVCR80.dll`'s `_acmdln`, which the CRT's WinMain startup parses. The CRT
  is native code and captured the *host's* command line when it loaded, so
  the host writes the guest's into `_acmdln` after binding
  (`set_crt_cmdline`);
- `GetModuleFileNameA/W` for the exe's own path, which the game uses to find
  `RetailExe\1.10\` (its `data\cursors`, `steam_api.dll`).

The working directory is the install root: the SkuDef's paths are relative
to it.

## 2. MSVCR80 is only in WinSxS

The exe imports `MSVCR80.dll` and carries a manifest naming
`Microsoft.VC80.CRT`. There is no `msvcr80.dll` in `SysWOW64`; Windows
finds it through the activation context it builds from a process exe's
manifest. An image the host maps gets no such context, and the bind fails
with the DLL not found. `activate_guest_manifest` builds the context from
the exe's own manifest resource (`CreateActCtxA`, resource 1) and activates
it before `native32_bind`, so `LoadLibrary("MSVCR80.dll")` resolves exactly
as it does for the real game.

## 3. The game asks for its own exports

Before any static constructor runs, `sub_00402E21` (an `_initterm` entry)
fills a table of function pointers by looking up the exe's own exports: once
in the process exe (`GetModuleHandleExA(0, NULL)`) and once in the module
holding its own code (`GetModuleHandleExA(FROM_ADDRESS, 0x00402E21)`), then
`GetProcAddress` on each. Under the host the first is `tw.exe` and the second
is no module at all, so every pointer stayed 0 and the next `_initterm` entry
tail-jumped through `[0x00BE1D20]` to address 0:

```
=== fault 0xC0000005 at 0x013FEAB0, thread 17508 ===
  in no module (heap or generated code)
  execute of 0x013FEAB0
  in lifted sub_0040A5BC, last native call MSVCR80.dll!_initterm
last indirect calls (newest first):
  0x00000000  from 0x00402980
```

The host answers both lookups with the guest image (0x00400000), and
`GetProcAddress` on it reads the guest's export table (`native32_export`).

## 4. `repz ret`: every /GS check failed (pcrecomp)

Next it ended with `TerminateProcess(0xC0000409)` from `__report_gsfailure`,
eleven calls into the startup, in a function whose cookie was intact
(`--watch 0x0040A338` showed ecx equal to the cookie). MSVC 2005's
`__security_check_cookie` is

```
0x40a338 cmp ecx, dword ptr [0xb7c590]
0x40a33e jne 0x40a342
0x40a340 repz ret
0x40a342 jmp 0x40a6bb        ; __report_gsfailure
```

and lift32 left `repz ret` (F3 C3, AMD's branch-prediction idiom)
unimplemented, so the success path fell through into the failure. Fixed in
pcrecomp's lift32 and in generate.py's extent walk (branch
`fix/lift32-repz-ret`, `tools/lift/test_repz_ret.py`). Any VC 2005+ game
built with `/GS` hits it; Tiberian Sun and Red Alert 2 are VC6 and never did.

The host's `TerminateProcess` shim prints `[exit]` and flushes `--calltrace`
first, which is how this one was found: the game's own exits otherwise end
the process before the trace is written.

## 5. SSE, xadd/cmpxchg, and the thread name (pcrecomp)

The exe is compiled for SSE, and pcrecomp's lift32 had no SSE at all:
**212,000 instructions** lifted as `UNIMPLEMENTED` no-ops, every float
multiply, add and compare in the game:

```
 119824 movss   28334 mulss   17055 addss   12931 subss   10651 movaps
   6563 comiss   4323 cvtsi2ss  3285 xorps   1530 divss   1490 fcompi
```

Fixed in pcrecomp (#79, `tools/lift/lift32_sse.py`), with `fcomi`, `lahf` and
`bsf`/`bsr`; `lock xadd` / `lock cmpxchg` (21 sites, reference counts) in #80.
After both, what is left unlifted is data decoded as code (`aaa`, `iretd`).

The run still ended, 136 under git bash. That is only the low byte: the real
exit code (from PowerShell) was **0x406D1388**, MSVC's SetThreadName
exception. The game names its first worker thread inside
`__try { RaiseException } __except {}`, but the guest's `__try` is on the
guest SEH chain, which Windows never walks, so it went unhandled. native32
now returns from that one code, which is what the empty `__except` does
(pcrecomp #83).

## 6. Headless that does not touch the screens, and the missing thunks

The first headless runs only hid the window: the game's Direct3D 9 device
was exclusive fullscreen, and every run switched the real monitors' mode.
Headless now passes the game its own `-win`, and before any guest code runs
the host patches `IDirect3D9`'s shared vtable so every `CreateDevice` and
`Reset` is windowed whatever the game asks for (`headless_d3d9` in
`host.c`). Runs on a workstation also go through offstage
(REPO_RULES section 13), and the physical displays' modes are compared
before and after:

```
offstage: \\.\DISPLAY10 1920x1080@60 at (5760, 0), dxgi adapter 0 output 2 (pool slot 0)
[headless] CreateDevice 800x600 -> 0x00000000
physical displays unchanged: NVIDIA GeForce RTX 5070 2560x1440@60
```

The next stop was unresolved indirect calls from the vector destructor
iterator at 0x0040CBFE:

```
   1104 ICALL: unresolved VA 0x00439455 from 0x0040CBFE
    294 ICALL: unresolved VA 0x007EE3EE from 0x0040CBFE
```

Both are this-adjustor thunks (`add ecx, 8; jmp f`) right after the function
before them, reached only through a pointer pushed as an argument. Five such
thunks are referenced anywhere; all five are in `RUN_SEEDS`.

## 7. Where it is now

The CRT, WinMain, the window, `steam_api`, the Direct3D 9 device (800x600,
windowed) and two minutes of start-up with no fault, no unresolved call and
328 million indirect calls; but **no frame presented** yet:

```
[watchdog] 120 s: in sub_0075A51E, last native call (native), 328237597 indirect calls, 0 frames presented
```

Next: whether that is slow loading or a wait that never ends (the main
thread was last seen in `WaitForSingleObject`), with a per-thread profile.
