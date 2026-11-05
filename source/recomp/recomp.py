#!/usr/bin/env python3
"""
Super Hang-On 68000 -> C static recompiler.

Each 68000 subroutine becomes a C function; branches become gotos, calls become C calls with
return-address checking. Instruction semantics are provided by Musashi's per-opcode handlers,
called with the opcode fixed at compile time, so behaviour (flags, timing) is identical to the
interpreter. Hand-written C can replace any function (see overrides/).

usage: recomp.py CPU(main|sub) ROM.bin DASM.txt COVERAGE.bin OUT.c [OVERRIDES_DIR]
"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import optable

CPU, ROMF, DASMF, COVF, OUTF = sys.argv[1:6]
OVR = sys.argv[6] if len(sys.argv) > 6 else None
P = 'M' if CPU == 'main' else 'S'
CPUIDX = 0 if CPU == 'main' else 1
CODE_END = 0x40000

rom = open(ROMF, 'rb').read()
names, cycles = optable.build()

# ---------------------------------------------------------------- disassembly table
dasm = {}
for line in open(DASMF):
    a, n, op, txt = line.rstrip('\n').split(' ', 3)
    dasm[int(a, 16)] = (int(n), int(op, 16), txt)

def w16(a): return struct.unpack('>H', rom[a:a + 2])[0]
def s16(v): return v - 0x10000 if v & 0x8000 else v
def s8(v): return v - 0x100 if v & 0x80 else v

# ---------------------------------------------------------------- coverage
visited, dyn = set(), set()
if COVF and os.path.exists(COVF):
    c = open(COVF, 'rb').read()
    vis = c[0:0xC0000]; dt = c[0xC0000:0x180000]
    base = CPUIDX * 0x60000
    for a in range(0, 0x60000, 2):
        if vis[base + a]: visited.add(a)
        if dt[base + a]: dyn.add(a)

# ---------------------------------------------------------------- instruction classification
EXC_PREFIX = ('m68k_op_div', 'm68k_op_chk', 'm68k_op_trap', 'm68k_op_illegal', 'm68k_op_1010', 'm68k_op_1111',
              'm68k_op_stop', 'm68k_op_reset')
EXC_SR = re.compile(r'm68k_op_(move_16_tos|andi_16_tos|ori_16_tos|eori_16_tos|move_32_tou|move_32_fru)')

def classify(a):
    """returns (kind, target) ; kinds: normal, bcc, bra, bsr, dbcc, jmp, jmpd, jsr, jsrd, ret, rte, exc, trap, bad"""
    if a not in dasm or a >= CODE_END or a & 1: return ('bad', None)
    n, op, txt = dasm[a]
    h = names[op]
    if h == 'm68k_op_illegal' and op != 0x4afc: return ('bad', None)
    if op in (0x4e75,): return ('ret', None)
    if op in (0x4e73, 0x4e77): return ('rte', None)
    if (op & 0xf000) == 0x6000:
        d = op & 0xff
        if d == 0: t = a + 2 + s16(w16(a + 2))
        else: t = a + 2 + s8(d)
        cc = (op >> 8) & 0xf
        return ({0: 'bra', 1: 'bsr'}.get(cc, 'bcc'), t)
    if (op & 0xf0f8) == 0x50c8:
        return ('dbcc', a + 2 + s16(w16(a + 2)))
    if (op & 0xffc0) in (0x4ec0, 0x4e80):
        mode, reg = (op >> 3) & 7, op & 7
        t = None
        if mode == 7 and reg == 0: t = s16(w16(a + 2)) & 0xffffff
        elif mode == 7 and reg == 1: t = struct.unpack('>I', rom[a + 2:a + 6])[0] & 0xffffff
        elif mode == 7 and reg == 2: t = a + 2 + s16(w16(a + 2))
        isj = (op & 0xffc0) == 0x4ec0
        if t is None: return ('jmpd' if isj else 'jsrd', None)
        return ('jmp' if isj else 'jsr', t)
    if (op & 0xfff0) == 0x4e40 or op == 0x4e76: return ('trap', None)
    if h.startswith(EXC_PREFIX) or EXC_SR.match(h): return ('exc', None)
    return ('normal', None)

def valid(t): return t is not None and t % 2 == 0 and 0 <= t < CODE_END and classify(t)[0] != 'bad'

# ---------------------------------------------------------------- discovery
func_entries = set()
vec = struct.unpack('>64I', rom[:256])
func_entries.add(vec[1])
for i in range(2, 64):
    if valid(vec[i]): func_entries.add(vec[i])
# Indirect-jump targets and return sites seen at run time are NOT made entries up front: any
# instruction inside a function is enterable through the dispatch table. Only executed code that
# static traversal never reaches becomes a new entry (below).

def succs(a):
    k, t = classify(a)
    n = dasm[a][0]
    if k in ('normal', 'exc', 'trap'): return [a + n]
    if k in ('bcc', 'dbcc'): return [x for x in (t, a + n) if valid(x)]
    if k in ('bra', 'jmp'): return [t] if valid(t) else []
    if k in ('bsr', 'jsr', 'jsrd'): return [a + n]
    return []

def call_targets(a):
    k, t = classify(a)
    return [t] if k in ('bsr', 'jsr') and valid(t) else []

functions = {}       # entry -> sorted list of instruction addresses
changed = True
while changed:
    changed = False
    for e in sorted(func_entries):
        if e in functions: continue
        body, work = set(), [e]
        while work:
            a = work.pop()
            if a in body or not valid(a): continue
            if a != e and a in func_entries: continue      # tail transfer to another function
            body.add(a)
            for c in call_targets(a):
                if c not in func_entries: func_entries.add(c); changed = True
            work.extend(succs(a))
        functions[e] = sorted(body)
        changed = True
    if any(e not in functions for e in func_entries): continue   # finish pending entries first
    # dynamically executed addresses not yet covered become entries
    covered = set().union(*functions.values()) if functions else set()
    for a in sorted(visited):
        if a not in covered and valid(a) and a not in func_entries:
            func_entries.add(a); changed = True
            break

owner = {}
for e in sorted(functions):
    for a in functions[e]:
        owner.setdefault(a, e)
for e in functions: owner[e] = e

# ---------------------------------------------------------------- overrides
overrides = set()
if OVR and os.path.isdir(OVR):
    for fn in os.listdir(OVR):
        m = re.match(r'%s_([0-9A-F]{6})\.c$' % P, fn)
        if m: overrides.add(int(m.group(1), 16))

# ---------------------------------------------------------------- emission
def fname(e): return '%s_%06X' % (P, e)
out = []
emit = out.append
emit('/* Generated by recomp.py from the %s 68000 program - do not edit; put changes in overrides/ */' % CPU)
emit('#include "rc_runtime.h"')
for e in sorted(functions): emit('void %s(uint32_t e);' % fname(e))
emit('')

for e in sorted(functions):
    body = functions[e]
    if e in overrides:
        emit('/* %s is implemented by hand in overrides/%s.c */' % (fname(e), fname(e)))
        continue
    inbody = set(body)
    emit('void %s(uint32_t e) {' % fname(e))
    emit('    switch (e) {')
    for a in body: emit('    case 0x%06X: goto L_%06X;' % (a, a))
    emit('    default: rc_bad_entry(e); return;')
    emit('    }')
    for a in body:
        n, op, txt = dasm[a]
        k, t = classify(a)
        h, cyc = names[op], cycles[op]
        nxt = a + n
        emit('L_%06X: /* %s */' % (a, txt.replace('*/', '* /')))
        emit('    OP(0x%06X, 0x%04X, %s, %d);' % (nxt if False else a + 2, op, h, cyc))
        def go(target):
            if target in inbody: return 'goto L_%06X;' % target
            return 'TAIL(%s, 0x%06X);' % (fname(owner[target]), target) if target in owner else 'JUMP_DYN();'
        if k == 'normal':
            emit('    STEP();')
            if nxt not in inbody: emit('    ' + go(nxt) if nxt in owner else '    JUMP_DYN();')
        elif k in ('exc', 'trap'):
            emit('    EXC_CHECK(0x%06X);' % nxt)
            if nxt not in inbody: emit('    ' + go(nxt) if nxt in owner else '    JUMP_DYN();')
        elif k in ('bcc', 'dbcc'):
            emit('    STEP();')
            if valid(t): emit('    if (REG_PC == 0x%06X) { %s }' % (t, go(t)))
            else: emit('    if (REG_PC != 0x%06X) { JUMP_DYN(); }' % nxt)
            if nxt not in inbody: emit('    ' + (go(nxt) if nxt in owner else 'JUMP_DYN();'))
        elif k in ('bra', 'jmp'):
            emit('    STEP();')
            emit('    ' + (go(t) if valid(t) else 'JUMP_DYN();'))
        elif k in ('bsr', 'jsr'):
            emit('    STEP();')
            if valid(t): emit('    CALL(%s, 0x%06X, 0x%06X);' % (fname(owner[t]), t, nxt))
            else: emit('    CALL_DYN(0x%06X);' % nxt)
            if nxt not in inbody: emit('    ' + (go(nxt) if nxt in owner else 'JUMP_DYN();'))
        elif k == 'jsrd':
            emit('    STEP();')
            emit('    CALL_DYN(0x%06X);' % nxt)
            if nxt not in inbody: emit('    ' + (go(nxt) if nxt in owner else 'JUMP_DYN();'))
        elif k == 'jmpd':
            emit('    STEP();')
            emit('    JUMP_DYN();')
        elif k in ('ret', 'rte'):
            emit('    STEP();')
            emit('    return;')
        else:
            emit('    STEP(); JUMP_DYN();')
    emit('}')
    emit('')

# dispatch table: every decoded instruction address -> owning function
emit('const rc_fn %s_table[0x%X] = {' % (P, CODE_END // 2))
for a in sorted(owner):
    emit('    [0x%X] = %s,' % (a // 2, fname(owner[a])))
emit('};')
open(OUTF, 'w').write('\n'.join(out) + '\n')
hdr = os.path.splitext(OUTF)[0] + '.h'
with open(hdr, 'w') as h:
    h.write('/* Generated prototypes for the recompiled %s CPU functions */\n#pragma once\n#include <stdint.h>\n' % CPU)
    for e in sorted(functions): h.write('void %s(uint32_t e);\n' % fname(e))
ninstr = len(owner)
print('%s: %d functions, %d instructions, %d covered-by-coverage of %d visited, %d overrides' %
      (CPU, len(functions), ninstr, len(visited & set(owner)), len(visited), len(overrides)))
