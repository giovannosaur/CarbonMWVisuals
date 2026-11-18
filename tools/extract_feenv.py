"""extract_feenv.py -- extracts the NFS Most Wanted FE reflection cubemaps to DDS for CarbonMWVisuals.

Usage:
    python extract_feenv.py "<NFS Most Wanted folder>\\GLOBAL\\DYNTEX.BIN" "<NFS Carbon folder>\\scripts\\CarbonMWVisuals_FEEnv"

Output: 24 files <SET>_<FACE>.dds (SET = QRACE, CAREER, CSHOP, CARLOT; FACE = XPOS..ZNEG),
A8R8G8B8 256x256. The textures belong to EA: do not redistribute them, let every player extract
them from their own copy of NFS Most Wanted.
"""
import os
import struct
import sys

SETS = ['QRACE', 'CAREER', 'CSHOP', 'CARLOT']
FACES = ['XPOS', 'XNEG', 'YPOS', 'YNEG', 'ZPOS', 'ZNEG']


def bstringhash(s):
    x = 0xFFFFFFFF
    for c in s.encode():
        x = (x * 33 + c) & 0xFFFFFFFF
    return x


def jdlz(inp):
    if inp[:4] != b'JDLZ':
        raise ValueError('not a JDLZ block')
    out_size = struct.unpack_from('<I', inp, 8)[0]
    out = bytearray(out_size)
    f1 = f2 = 1
    ip, op = 16, 0
    while ip < len(inp) and op < out_size:
        if f1 == 1:
            f1 = inp[ip] | 0x100; ip += 1
        if f2 == 1:
            f2 = inp[ip] | 0x100; ip += 1
        if f1 & 1:
            if f2 & 1:
                ln = (inp[ip + 1] | ((inp[ip] & 0xF0) << 4)) + 3
                t = (inp[ip] & 0x0F) + 1
            else:
                t = (inp[ip + 1] | ((inp[ip] & 0xE0) << 3)) + 17
                ln = (inp[ip] & 0x1F) + 3
            ip += 2
            for _ in range(ln):
                out[op] = out[op - t]; op += 1
                if op >= out_size:
                    break
            f2 >>= 1
        else:
            out[op] = inp[ip]; op += 1; ip += 1
        f1 >>= 1
    return bytes(out)


def find_chunk(d, cid, o=0, end=None, depth=0):
    end = len(d) if end is None else end
    while o + 8 <= end:
        c, sz = struct.unpack_from('<II', d, o)
        if c == cid:
            return o + 8, sz
        if c & 0x80000000 and depth < 3:
            r = find_chunk(d, cid, o + 8, o + 8 + sz, depth + 1)
            if r:
                return r
        o += 8 + sz
    return None


def dds_argb(w, h, pix):
    pf = struct.pack('<8I', 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr = struct.pack('<7I', 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000, h, w, w * 4, 0, 1) + b'\0' * 44 + pf
    hdr += struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    return b'DDS ' + hdr + pix


def main():
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    src, outdir = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read()
    idx = find_chunk(d, 0x33310003)
    if not idx:
        sys.exit('texture index (0x33310003) not found -- not an NFS Most Wanted DYNTEX.BIN?')
    ent = {}
    for k in range(idx[1] // 24):
        h, off, cs, us, _, _ = struct.unpack_from('<6I', d, idx[0] + 24 * k)
        ent[h] = (off, cs, us)
    os.makedirs(outdir, exist_ok=True)
    n = 0
    for s in SETS:
        for f in FACES:
            name = '%s_%s' % (s, f)
            e = ent.get(bstringhash(name))
            if not e:
                print(name, 'missing'); continue
            off, cs, us = e
            raw = jdlz(d[off:off + cs + 64])
            if len(raw) < 256 * 256 * 4:
                print(name, 'unexpected size', len(raw)); continue
            open(os.path.join(outdir, name + '.dds'), 'wb').write(dds_argb(256, 256, raw[:256 * 256 * 4]))
            n += 1
    print('%d DDS files written to %s' % (n, outdir))


if __name__ == '__main__':
    main()
