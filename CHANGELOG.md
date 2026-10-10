# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Added
- The pipeline, in tiberiansun-recomp's shape: `Setup.cmd` (finds the Steam
  install, copies it into `game\`, analyses, catalogs, lifts, builds),
  `run_lift.py` (the shared pcrecomp lift driver, at the Steam build's
  `RetailExe\1.10\cnc3game.dat`, refusing any other build by its PE
  timestamp), `build.cmd` and `CMakeLists.txt` for the 32-bit host.
- `src/runtime/host.c`, the host on pcrecomp's `native32`: maps the game at
  0x00400000, gives it the launcher's command line (`-config` and the
  SkuDef) and its own identity (module name, file name, command line),
  activates its manifest so `MSVCR80.dll` loads from WinSxS, sets the CRT's
  `_acmdln`, and reports faults, unlifted calls and watchdog stops.
  `--headless` keeps windows and message boxes off the screen. The game's
  lookups of its own exports (`GetModuleHandleExA`, `GetProcAddress`) answer
  with the guest image, and its `TerminateProcess` prints `[exit]` and
  flushes `--calltrace` first (docs/bringup.md, 3 and 4).
- `tools/conformance.py`: boot milestones and lift health against
  `conformance.json`.
- docs: `RECON.md` (the launcher, the exe, against Tiberian Sun),
  `bringup.md`.
