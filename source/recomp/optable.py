"""Replicates Musashi's m68ki_build_opcode_table: opcode -> (handler name, 68000 base cycles)."""
import re, os, pickle
HERE = os.path.dirname(os.path.abspath(__file__))
OPS = os.path.join(HERE, '..', 'src', 'third_party', 'musashi', 'm68kops.c')
def build():
    cache = os.path.join(HERE, 'optable.pkl')
    if os.path.exists(cache): return pickle.load(open(cache, 'rb'))
    src = open(OPS).read()
    start = src.index('m68k_opcode_handler_table[] =')
    ents = re.findall(r'\{(m68k_op_\w+)\s*,\s*0x([0-9a-f]+),\s*0x([0-9a-f]+),\s*\{\s*(\d+)', src[start:])
    name = ['m68k_op_illegal'] * 65536; cyc = [0] * 65536
    for e in ents:   # Musashi applies the table group by group in order; later entries override
        h, mask, match, c = e[0], int(e[1], 16), int(e[2], 16), int(e[3])
        # enumerate opcodes matching mask/match
        free = (~mask) & 0xffff
        sub = free
        while True:
            op = match | sub
            name[op] = h; cyc[op] = c
            if sub == 0: break
            sub = (sub - 1) & free
    pickle.dump((name, cyc), open(cache, 'wb'))
    return name, cyc
if __name__ == '__main__':
    n, c = build()
    print(n[0x4e75], c[0x4e75], n[0x3010], c[0x3010], n[0x7000], c[0x7000])
