# Contributing

Issues and pull requests are welcome.

## The one rule

**Nothing from the game goes in the repository.** No game files, no lifted C
(`src/recomp/gen/`), no catalog or RTTI dumps (`work/`), no disassembly
listings, nothing generated from `cnc3game.dat`. The tool ships; its output
is made on each person's machine from their own copy. `.gitignore` covers the
usual paths; check `git status` before you commit anyway. Screenshots of the
game running are fine.

## Where your code comes from

This repository is MIT. Code ported from, or written while reading, a
copyleft or unlicensed project makes it that project's licence, whatever the
variable names. The ones most likely to be open beside a C&C3 recomp are
**Wine** (LGPL; its Direct3D, kernel32 and msvcrt), **DXVK's** older forks,
the **OpenSAGE** and **Thyme** reimplementations (check each file's licence;
Thyme is GPL), and leaked SAGE source (never). Running any of them as a black
box and comparing output is fine; copying from them is not. If a fix came
from somewhere, say where in the pull request.

AI-assisted contributions are welcome, provided a human understood and
verified the change.

## Before a pull request

1. Build from a clean lift: `py -3 run_lift.py --all` (expected: `errors 0`),
   then `build.cmd`.
2. Run the conformance harness: `py -3 tools\conformance.py`. It must not
   regress against `conformance.json`.

## Where a fix belongs

- A bug in the lifter, the catalog or the native bridge belongs in
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), not here.
- A fix to the game itself is a patch in `run_lift.py`, with a comment that
  says what it fixes and why.
- A wall you got past goes in `docs/bringup.md`: what it looked like, what it
  was, what moved it.
