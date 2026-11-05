#!/usr/bin/env python3
"""Copy a generated function into recomp/overrides/<NAME>.c as the starting point for hand-written C.
usage: extract_override.py src/gen/main_rc.c M_002A0C"""
import sys, re, os
gen, name = sys.argv[1], sys.argv[2]
src = open(gen).read()
m = re.search(r'^void %s\(uint32_t e\) \{.*?^\}\n' % name, src, re.S | re.M)
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overrides', name + '.c')
if os.path.exists(out): sys.exit('%s exists - not overwriting' % out)
open(out, 'w').write('/* Hand-maintained override of %s (generated code copied by extract_override.py) */\n#include "rc_runtime.h"\n#include "rc_overrides.h"\n\n' % name + m.group(0))
print('wrote', out)
