# Recon

The build: the Steam release of *Command & Conquer 3: Tiberium Wars*
(app 24790), `RetailExe\1.10\cnc3game.dat`.

## The launcher and the exe

`CNC3.exe` in the install root is only a launcher, as in every SAGE game. It
picks the newest `CNC3_<language>_<version>.SkuDef`, whose `set-exe` line
names the real game, and starts that with `-config "<the SkuDef>"`:

```
set-exe RetailExe\1.10\cnc3game.dat
add-config Lang-english\1.9\config.txt
add-config EnglishAudio\1.7\config.txt
add-config Movies\1.0\config.txt
add-config Core\1.9\config.txt
add-config SP\1.9\config.txt
add-config MP\1.9\config.txt
add-config Kane\1.0\config.txt
add-config RetailExe\1.10\config.txt
add-search-path big:
```

`RetailExe\` keeps every patch level, 1.0 to 1.10, each a complete PE. 1.10
is the one the Steam launcher runs, a 2025 rebuild; 1.9 (2012) is the last of
the original patches. The host builds the launcher's command line itself
(`src/runtime/host.c`), so `CNC3.exe` is never run.

| | `cnc3game.dat` 1.10 | 1.9 |
|---|---|---|
| PE timestamp | `0x67B6063A`, 2025-02-19 | `0x501755E8`, 2012-07-31 |
| Image | 0x00400000, size 0x00FE1000, no relocations | same base |
| Entry | 0x0040A2B1 | 0x0040A2AD |
| `.text` | 0x0062B75E bytes (6.2 MB) | 0x0062691F |
| `.rsrc` | 7.9 MB (45 icons) | 103 KB |
| Protection | none: the sections' entropy is ordinary code and data | none |
| Compiler | MSVC 2005 (imports `MSVCR80.dll`, VC80 CRT manifest) | same |

Sections: `.text`, `.rdata`, `.data`, `rts.ver` (0x200), `.tls` (9 bytes),
`.rsrc`.

The **TLS directory** has 8 bytes of template, an index at `0x00C28BF4` and
no callbacks. Windows sets up static TLS only for a process's own exe, not an
image the host maps, but nothing in the lifted code reads `fs:[2Ch]`: the
only TEB read is the CRT startup's `fs:[18h]` (0x0040A01C), for the stack
base as a lock owner, which native32's TIB has. So the directory is left
alone.

The **manifest** (resource 1) asks for `Microsoft.VC80.CRT` 8.0.50608.0.
`MSVCR80.dll` is in WinSxS only, so it loads through that manifest's
activation context (docs/bringup.md).

Imports: GDI32, USER32, KERNEL32, SHELL32, MSVCR80, ADVAPI32, DINPUT8,
`d3dx9_29.dll` (DirectX 9, June 2005 redistributable), DSOUND, WINMM, IMM32,
WININET, WS2_32, NETAPI32, SHLWAPI, ole32, OLEAUT32. `steam_api.dll` beside
the exe is not imported; the game loads it itself.

Exports: 494, nearly all of them the Apt UI runtime's C++ classes
(`AptValue`, `AptStage`, `EAStringC`), which the exe exports for its Flash
movies' script.

```
$ py -3 ..\tools\tools\cpp\rtti.py game\RetailExe\1.10\cnc3game.dat -o work\rtti.json --seeds work\rtti_seeds.json
[*] type descriptors : 3,746
[*] complete object locators : 5,115
[*] vtables          : 5,115
[*] classes          : 3,570
[*] virtual methods  : 12,224  (11,690 attributable to one class)
```

## Against Tiberian Sun

| | Tiberium Wars `cnc3game.dat` | Tiberian Sun `Game.exe` |
|---|---|---|
| Engine | SAGE (Generals' lineage), Direct3D 9 | Westwood 2.5D, DirectDraw |
| `.text` | 6.2 MB | 2.9 MB |
| RTTI classes | 3,570 | 807 |
| Virtual methods | 12,224 | 5,226 |
| CRT | `MSVCR80.dll`, from WinSxS | static |
| UI | Apt (Flash), from `.big` archives | the game's own gadgets |

Nothing tied to an address carries over from tiberiansun-recomp: the host's
shape, the setup and the tools do.

## Red Alert 3 and Kane's Wrath

Both are on the same SAGE engine, each its own exe: Kane's Wrath is
`RetailExe\1.3\cnc3ep1.dat` in its own install; Red Alert 3's launcher is `RA3.exe`
and its game `Data\RA3_1.13.game` (`RA3_english_1.13.SkuDef`). A recomp lifts one binary, so they are separate
lifts, in the way Tiberian Sun and Red Alert 2 are separate repos (ROADMAP).
