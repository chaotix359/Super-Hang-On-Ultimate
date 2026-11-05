import sys, numpy as np
out = sys.argv[1]; acc = None
for f in sys.argv[2:]:
    d = open(f, 'rb').read()
    v = np.frombuffer(d[:0x180000], np.uint8); c = np.frombuffer(d[0x180000:], np.uint64)
    if acc is None: acc = [v.copy(), c.copy()]
    else: acc[0] |= v; acc[1] += c
open(out, 'wb').write(acc[0].tobytes() + acc[1].tobytes())
