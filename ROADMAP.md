# Roadmap

The same path [tiberiansun-recomp](https://github.com/sp00nznet/tiberiansun-recomp)
and [redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp) took,
in order; each is done when its check is in the conformance harness or the
docs.

1. **Boot.** The lifted exe runs from its entry point through the CRT's
   startup into WinMain, reads its SkuDef and config files, and opens its
   window (docs/bringup.md).
2. **Direct3D 9 up headless.** The device created on a window that never
   reaches the screen, and frames counted.
3. **`--record out.mp4`.** Frames read back from the D3D9 back buffer and
   piped to ffmpeg, the house headless mode (REPO_RULES section 10).
4. **The shell map and main menu** (Apt, from the `.big` archives).
5. **A skirmish**, then the campaigns' first missions, each a scripted case.
6. **Kane's Wrath**: its own exe (`cnc3ep1.dat` 1.3), lifted by the same
   driver with its own addresses, in this repo as Yuri's Revenge is in
   redalert2-recomp's.
7. **Setup.cmd end to end** from a clean folder.

## Deferred

- **The presenter, high resolution, widescreen.** The game already runs at
  any resolution in a window (`-win -xres -yres`); what a remaster adds here
  is decided once it plays.
- **Linux natively** on pcrecomp's `win32hle`: needs a Direct3D 9 layer
  there first.
- **Online play** (GameSpy is gone); LAN is in scope once a skirmish plays.

## Out of scope

- **Red Alert 3.** Same SAGE engine, different exe: its own repo, as Red
  Alert 2 is to Tiberian Sun. What this repo finds about the engine (the
  launcher, the VC80 CRT, static TLS) carries over.
- Any patch level but the Steam build's 1.10 (docs/RECON.md).
