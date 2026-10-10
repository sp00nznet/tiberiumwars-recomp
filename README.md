# Tiberium Wars — Static Recompilation

Static recompilation of **Command & Conquer 3: Tiberium Wars** (EA Los
Angeles, 2007) from the shipping Win32 binary, `cnc3game.dat`, to native C,
on the [pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain. It is set
up the way [tiberiansun-recomp](https://github.com/sp00nznet/tiberiansun-recomp)
and [redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp) are:
the same setup, lift driver, host shape, conformance harness and flags, and
the same house style as every pcrecomp game.

## Status: **bring-up.** The whole exe lifts with 0 lift errors and boots through the C runtime to its window; it does not play yet. pcrecomp's lift32 has no SSE, and this exe has 92,018 SSE instructions ([bringup.md](docs/bringup.md), 5).

| Stage | State |
|---|---|
| The build | the Steam release: `RetailExe\1.10\cnc3game.dat`, 2025-02-19, no DRM ([RECON.md](docs/RECON.md)) |
| RTTI class recovery | 3,570 classes, 5,115 vtables, 12,224 virtual methods |
| Function catalog (`disasm32`) | 69,622 functions, 98.9% of `.text` |
| Lift (`run_lift.py --all`) | 69,676 functions, 7.5M lines of C, **0 lift errors**; 14,590 named from the binary's own evidence |
| Host (`build/tw.exe`, 32-bit, pcrecomp `native32`) | the CRT startup, WinMain, the game's window, `steam_api` initialised; then exits during start-up ([bringup.md](docs/bringup.md)) |
| SSE | **92,018 instructions not lifted** (`mulss`, `addss`, `movaps`...): lift32 has no SSE yet |
| Conformance (`tools/conformance.py`) | **3/3** boot milestones to the window; lift 0 errors |
| Kane's Wrath | not started (ROADMAP) |

## Screenshots

None yet: nothing is drawn. The first ones come with `--record`
(ROADMAP 3).

## Getting Started

You need **your own copy of Tiberium Wars**: the Steam build (the folder
holding `CNC3.exe` and `RetailExe\`). Nothing from the game is in this
repository and nothing is downloaded for you. The lifted C is generated on
your machine from your copy and is never distributed.

### Quick start

1. Download this repository and unzip it somewhere with 12 GB free.
2. Double-click **`Setup.cmd`**.

It checks for Python 3.10+, the `pefile` and `capstone` packages, the pcrecomp
toolkit, Visual Studio 2022 with the C++ x86 tools, CMake and Ninja, and
**asks** before installing anything. It finds the game in your Steam library or
asks for the folder, copies it into `game\`, builds the function catalog,
lifts and builds. A rerun skips finished steps. If it stops, it says why in one
sentence; the details are in `setup.log`.

It ends with `Tiberium Wars (recomp).cmd` in this folder.

### Step by step

Prerequisites: Windows 10/11, **Python 3.10+** (`py -3 --version`), **git**,
**Visual Studio 2022 or 2026** with *Desktop development with C++* including
the x86 tools, **CMake 3.20+** and **Ninja**, the **DirectX June 2010
runtime** (for `d3dx9_29.dll`; Steam installs it with the game), and the
pcrecomp toolkit cloned **beside** this repository as `tools`:

```
some-folder\
  tools\    <- git clone https://github.com/sp00nznet/pcrecomp tools
  tw\       <- this repository
```

1. Python packages:
   ```
   py -3 -m pip install --user pefile capstone
   ```
2. Copy your install into `game\`:
   ```
   robocopy "<Steam>\steamapps\common\Command and Conquer 3 Tiberium Wars" game /E
   ```
3. Headers, imports and C++ classes (seconds):
   ```
   py -3 ..\tools\tools\pe\pe_analyze.py game\RetailExe\1.10\cnc3game.dat --json work\pe_analysis.json
   py -3 ..\tools\tools\cpp\rtti.py game\RetailExe\1.10\cnc3game.dat -o work\rtti.json --seeds work\rtti_seeds.json
   ```
   Expected from `rtti.py`: `classes : 3,570` and `virtual methods : 12,224`.
4. The function catalog (about 30 minutes):
   ```
   py -3 ..\tools\tools\disasm\disasm32.py game\RetailExe\1.10\cnc3game.dat -o work\functions.json --seed-functions work\rtti_seeds.json
   ```
   Expected: `Functions: 69622` and `(98.9% of code range)`.
5. Lift (a few minutes):
   ```
   py -3 run_lift.py --all
   ```
   Expected: `lifted 69676   not-lifted stubs 0   errors 0   no terminator 900`.
   Needs a pcrecomp with `repz ret` lifted (bringup.md, 4).
6. Build:
   ```
   build.cmd
   ```
7. Check it (a dry run maps and binds the game without running it):
   ```
   build\tw.exe
   ```

Trip-ups: `python` that opens the Microsoft Store is the Store's alias, use
`py -3`; a PATH change needs a new window. `build.cmd` uses the 64-bit-hosted
x86 compiler on purpose: the 32-bit one runs out of heap on the lifted C.

## Usage

```
build\tw.exe --run                        # run the game
build\tw.exe --run --headless --watchdog 60   # no window, stop after 60 s with a report
build\tw.exe --run --gameargs "-win -xres 1280 -yres 720"   # the game's own switches
build\tw.exe --run --probe 0x00401000     # report indirect calls to a function
py -3 tools\conformance.py                # boot milestones and lift health
```

`--game DIR` runs another copy of the install. A fault, a call to a function
that was not lifted, or the watchdog ends the run with a report naming the
lifted function, the last native call and the guest stack.

## Building from source

As Step by step, steps 5 and 6. The lifted C lands in `src/recomp/gen/`
(gitignored); `CMakeLists.txt` takes the toolkit from `../tools`, or
`-DPCRECOMP=<path>`, and `run_lift.py` from `%PCRECOMP%` if set. On the lab:
`netlab build tiberiumwars-recomp`.

## License

MIT for what is ours: the setup, the host, the tools and the docs
([LICENSE](LICENSE)). Command & Conquer 3 is © Electronic Arts, and none of
it is here. The lifted C is a derivative of your copy of the game and is
never committed or distributed.
