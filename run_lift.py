#!/usr/bin/env python3
"""Command & Conquer 3: Tiberium Wars lift driver: work/functions.json -> src/recomp/gen/.

The same driver as tiberiansun-recomp's and redalert2-recomp's run_lift.py,
on pcrecomp's shared `tools/lift/generate.py` + `lift32`:

* **Closure-limited lifting** (`generate.closure`). Lift the call-graph closure
  from the OEP and give every other catalogued function a stub that aborts
  naming itself, so a run says exactly what to lift next. Bring-up uses --all;
  the closure is there for bisecting a bad lift.
* **Extents by walking the branches** (`generate.true_extent`), capped by
  reach rather than the catalog's clamp.

Every vtable slot RTTI names is injected as an entry (work/rtti_seeds.json):
MSVC's vtordisp adjustor thunks are reachable only through a vtable, so no
disassembler pass names them, and an unresolved slot answers eax = 0.

    py -3 run_lift.py --all
    py -3 run_lift.py --max 20000
    py -3 run_lift.py --roots 0x0040A2B1
"""
import argparse
import json
import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
# PCRECOMP picks another toolkit checkout, the same knob CMakeLists.txt has:
# the lifter and the runtime header must come from the same tree.
_TOOLS = os.path.join(os.environ.get('PCRECOMP', os.path.join(_HERE, '..', 'tools')), 'tools')
sys.path.insert(0, os.path.join(_TOOLS, 'lift'))
sys.path.insert(0, os.path.join(_TOOLS, 'pe'))

from capstone import Cs, CS_ARCH_X86, CS_MODE_32          # noqa: E402
from generate import (EXTENT_REACH, closure, find_splits, true_extent,  # noqa: E402
                      linear_disassemble_function, lift_function_linear, write_chunk)
from lift32 import Lifter                                  # noqa: E402
from pe_analyze import analyze_pe, build_iat_map           # noqa: E402

# The game is the launcher's: CNC3.exe reads CNC3_english_1.10.SkuDef and runs
# this file (docs/RECON.md).
EXE = os.path.join(_HERE, 'game', 'RetailExe', '1.10', 'cnc3game.dat')
EXE_STAMP = 0x67B6063A   # cnc3game.dat 1.10's PE timestamp, the Steam release (docs/RECON.md)
CATALOG = os.path.join(_HERE, 'work', 'functions.json')
SEEDS = os.path.join(_HERE, 'work', 'rtti_seeds.json')
OUT = os.path.join(_HERE, 'src', 'recomp', 'gen')
STATS = os.path.join(_HERE, 'work', 'lift_stats.json')

# Entries the catalog does not find, so a clean checkout lifts them too
# (docs/bringup.md, 6). MSVC this-adjustor thunks (`add ecx, N; jmp f`, or
# sub) sitting right after the function before them, reached only through a
# pointer pushed as an argument (the vector destructor iterator, 0x0040CBFE)
# or stored in data. An unresolved ICALL answers eax = 0 and carries on.
RUN_SEEDS = [0x00439455, 0x004BC497, 0x00776172, 0x0077AB43, 0x007EE3EE]

# Functions whose body is the host's instead of the lift: the host defines
# tw_hook_XXXXXXXX(void) and it runs like an import shim. src/runtime/host.c.
HOOKS = set()

# Fixes to the game itself: C run after the instruction at the address.
# The lift stays faithful; a patch is for a fix the original cannot have.
PATCHES = {}


def apply_patches(body, patches):
    """Add each patch's C after the lines that lift its instruction.

    Some instructions lift to a block over several lines (idiv's `{ ... }`):
    the patch goes after the block closes, not inside it."""
    for va, c in patches.items():
        tag = '/* 0x%08X:' % va
        k = body.find(tag)
        if k != -1:
            s = body.rfind('\n', 0, k) + 1
            e = body.index('\n', k)
            depth = body[s:e].count('{') - body[s:e].count('}')
            while depth > 0:
                s, e = e + 1, body.index('\n', e + 1)
                depth += body[s:e].count('{') - body[s:e].count('}')
            body = body[:e + 1] + '    ' + c + '\n' + body[e + 1:]
    return body


def exe_stamp(path):
    """The PE header's TimeDateStamp: which build of the game an exe is."""
    with open(path, 'rb') as f:
        head = f.read(4096)
    nt = int.from_bytes(head[0x3C:0x40], 'little')
    return int.from_bytes(head[nt + 8:nt + 12], 'little')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=EXE)
    ap.add_argument('--catalog', default=CATALOG)
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--roots', default='', help='comma-separated extra root VAs')
    ap.add_argument('--max', type=int, default=3000, help='closure size cap')
    ap.add_argument('--all', action='store_true', help='lift every function')
    ap.add_argument('--virtual', action='store_true',
                    help='root the closure at every RTTI vtable method too')
    ap.add_argument('--split', type=int, default=400, help='functions per .c file')
    # More entries a run found, on top of RUN_SEEDS (seed_from_log.py JSON).
    ap.add_argument('--seeds', default=os.path.join(_HERE, 'work', 'run_seeds.json'),
                    help='seed_from_log.py JSON of unresolved targets from runs')
    args = ap.parse_args()
    if not os.path.exists(args.catalog):
        sys.exit('no catalog at %s -- run disasm32.py first (README, Step by step)' % args.catalog)

    # The patches are at this build's addresses; another build lifts, but
    # they land in the wrong code, and it crashes in game.
    stamp = exe_stamp(args.exe)
    if stamp != EXE_STAMP:
        sys.exit('%s is not the build this project supports: its PE timestamp is 0x%08X, '
                 'the Steam release\'s is 0x%08X (docs/RECON.md). Another patch level '
                 '(RetailExe/1.0 to 1.9, a disc or EA App build) needs its own addresses; '
                 'use the Steam build\'s RetailExe/1.10/cnc3game.dat.' % (args.exe, stamp, EXE_STAMP))
    info = analyze_pe(args.exe)
    iat = build_iat_map(info)
    cs, ce = info.code_start, info.code_end
    print('[*] base=0x%08X code=0x%08X-0x%08X IAT=%d' % (info.image_base, cs, ce, len(iat)))

    cat = json.load(open(args.catalog))
    byaddr = {f['address']: f for f in cat['functions'] if cs <= f['address'] < ce}
    print('[*] catalog: %d functions inside .text' % len(byaddr))

    # Vtable slots. Bound each by the next known entry: a slot landing mid-code
    # handed `ce` makes the extent walk descend the whole of .text.
    want = {a for a in RUN_SEEDS if cs <= a < ce and a not in byaddr}
    for path in [SEEDS] + ([args.seeds] if args.seeds else []):
        if os.path.exists(path):
            want |= {e['address'] for e in json.load(open(path))
                     if cs <= e['address'] < ce and e['address'] not in byaddr}
    known = sorted(set(byaddr) | want)
    nxt = {a: (known[i + 1] if i + 1 < len(known) else ce) for i, a in enumerate(known)}
    for a in want:
        byaddr[a] = {'address': a, 'end': min(nxt[a], a + 0x100), 'calls_to': [],
                     'entry_kind': 'start'}
    print('[*] vtable slots not in the catalog: %d injected' % len(want))

    entry = info.image_base + info.entry_point_rva
    roots = [entry] + [int(x, 0) for x in args.roots.split(',') if x.strip()]
    if args.virtual and os.path.exists(SEEDS):
        roots += sorted(e['address'] for e in json.load(open(SEEDS)))
    if args.all:
        chosen = set(byaddr)
    else:
        chosen = set(closure(byaddr, roots, args.max))
    print('[*] lifting %d of %d functions (%s)'
          % (len(chosen), len(byaddr), 'all' if args.all else 'closure, cap %d' % args.max))

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    text = [s for s in info.sections if s.name == '.text'][0]
    code = open(args.exe, 'rb').read()[text.raw_offset:text.raw_offset + text.raw_size]

    # Known targets become RECOMP_CALL; anything else a decoded `call` names
    # becomes RECOMP_ICALL, which reports at run time instead of failing the build.
    # precise carry: adc/sbb take their carry from the flag state that set it.
    # The default reads a `_cf` that add/sub/cmp never write, which breaks
    # every 64-bit add and subtract. The Movies lost its menu video to it
    # (its docs/bringup.md); off by default in lift32 only because Fury3 leans
    # on the imprecision.
    patches = dict(PATCHES)
    print('[*] remaster patches: %d' % len(patches))
    lifter = Lifter(iat_map=iat, lifted=set(byaddr), precise_carry=True, precise_sbb=True)
    os.makedirs(args.out, exist_ok=True)
    for fn in os.listdir(args.out):                 # a smaller lift must not leave stale chunks
        if fn.startswith('recomp_') and fn.endswith('.c'):
            os.remove(os.path.join(args.out, fn))
    entries, chunk, idx, errors, dirty = [], [], 0, 0, 0
    ordered = sorted(byaddr)
    starts = {a for a in ordered if byaddr[a].get('entry_kind') != 'alias'}
    t_split = time.time()
    splits = find_splits(md, code, cs, ce, starts)
    starts -= splits          # still dispatchable; just not a wall inside its parent
    print('[*] split entries (the middle of the function before them): %d, %.0fs'
          % (len(splits), time.time() - t_split))
    t0 = time.time()

    def flush(force=False):
        nonlocal chunk, idx
        if chunk and (force or len(chunk) >= args.split):
            write_chunk(args.out, idx, chunk)
            idx += 1
            chunk = []

    def lift_one(addr, name, end, reached):
        nonlocal errors

        if addr in HOOKS:
            chunk.append(('extern void tw_hook_%08X(void);\nvoid %s(void) { tw_hook_%08X(); }\n'
                          % (addr, name, addr), addr, name))
            entries.append((addr, name))
            return
        try:
            lo = min(reached) if reached else addr   # a chunk can sit below the entry
            insns, leaders = (linear_disassemble_function(md, code, cs, lo, end, reached=reached)
                              if end > addr else ([], None))
            if leaders is not None:
                leaders.add(addr)
            body = (lift_function_linear(lifter, name, insns, leaders, addr) if insns
                    else 'void %s(void) { }\n' % name)
            body = apply_patches(body, {va: c for va, c in patches.items() if reached and va in reached})
        except Exception as e:                      # noqa: BLE001 -- counted, not hidden
            body = '/* ERROR %s: %s */\nvoid %s(void) { }\n' % (name, e, name)
            errors += 1
        chunk.append((body, addr, name))
        entries.append((addr, name))
        if len(chunk) >= args.split:
            flush()
            print('[*]   %d/%d (%d err)' % (len(entries), len(chosen), errors), flush=True)

    # A direct branch that leaves a body backward to an address nothing
    # catalogued is a tail call the catalog missed (__mtterm: jmp 0x00AD6FB7)
    # or a jump into shared code in a neighbour (hand-written x87 math:
    # __ffexpm1 jne 0x00AE01F0). A direct call can name one too: the catalog
    # dropped 0x00C10170, called directly, because a false start inside the
    # jump table before it decoded over it. Either way the target has to be dispatchable,
    # or the RECOMP_ITAIL cannot resolve. So each round's outside targets
    # become entries and are lifted in the next round, until none are new.
    todo, added = sorted(chosen), 0
    while todo:
        outside = set()
        for addr in todo:
            name = 'sub_%08X' % addr
            reached, behind, called = set(), set(), set()
            end, clean = true_extent(md, code, cs, addr, min(addr + EXTENT_REACH, ce), starts,
                                     reached=reached, behind=behind, called=called)
            outside |= {t for t in behind if t not in reached} | called
            dirty += not clean
            lift_one(addr, name, end, reached)
        todo = sorted(t for t in outside if cs <= t < ce and t not in byaddr)
        for t in todo:
            byaddr[t] = {'address': t, 'end': ce, 'calls_to': [], 'entry_kind': 'start'}
            chosen.add(t)
        added += len(todo)
    print('[*] branch and call targets outside the catalog, added as entries: %d' % added)

    stubs = [a for a in ordered if a not in chosen]
    for a in stubs:
        name = 'sub_%08X' % a
        chunk.append(('void %s(void) { RECOMP_NOT_LIFTED(0x%08Xu); }\n' % (name, a), a, name))
        entries.append((a, name))
        flush()
    flush(force=True)

    with open(os.path.join(args.out, 'recomp_funcs.h'), 'w', newline='\n') as f:
        f.write('/* cnc3game.dat - AUTO-GENERATED by run_lift.py */\n#pragma once\n'
                '#include <stdint.h>\n\n'
                'void recomp_not_lifted(uint32_t va);\n'
                '#define RECOMP_NOT_LIFTED(va) recomp_not_lifted(va)\n\n')
        for a, n in entries:
            f.write('void %s(void);\n' % n)
    with open(os.path.join(args.out, 'recomp_dispatch.c'), 'w', newline='\n') as f:
        f.write('/* cnc3game.dat - AUTO-GENERATED by run_lift.py */\n'
                '#include "recomp_types.h"\n#include "recomp_funcs.h"\n\n'
                'const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n')
        for a, n in sorted(entries):
            f.write('    { 0x%08Xu, %s },\n' % (a, n))
        f.write('};\nconst uint32_t recomp_dispatch_count = %d;\n'
                'const uint32_t tw_entry_va = 0x%08Xu;\n' % (len(entries), entry))

    # Readable names, and a header on every function saying what it is, from
    # what the binary itself says: RTTI's vtables, debug messages, vtable
    # stores (pcrecomp's tools/lift/name_lift.py). An older toolkit without it
    # leaves the address names; nothing else depends on them.
    try:
        import name_lift
    except ImportError:
        print('[*] names: no tools/lift/name_lift.py in this pcrecomp; functions keep their address names')
    else:
        rtti_path = os.path.join(os.path.dirname(SEEDS), 'rtti.json')
        rtti = json.load(open(rtti_path)) if os.path.exists(rtti_path) else {}
        named = name_lift.name_lift(args.out, name_lift.Image(args.exe), rtti, {})
        print('[*] names: %d of %d functions named from the binary\'s own evidence'
              % (sum(1 for f in named.values() if f.name), len(named)))

    lines = sum(sum(1 for _ in open(os.path.join(args.out, fn), encoding='utf-8', errors='replace'))
                for fn in os.listdir(args.out))
    stats = {'lifted': len(chosen), 'stubs': len(stubs), 'errors': errors,
             'no_terminator': dirty, 'files': idx, 'lines': lines}
    json.dump(stats, open(STATS, 'w'), indent=1)
    print('=' * 60)
    print('  lifted %d   not-lifted stubs %d   errors %d   no terminator %d'
          % (len(chosen), len(stubs), errors, dirty))
    print('  %s lines of C in %d files, %.1fs' % (format(lines, ','), idx, time.time() - t0))
    print('=' * 60)


if __name__ == '__main__':
    main()
