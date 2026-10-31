"""Read-only: dump Fallout4.esm Commonwealth NAVM geometry for given exterior grid cells (plus every NAVM they link to,
recursively within a radius) into a simple binary file for the desktop nav benchmark.
usage: python -I navm_dump.py <Fallout4.esm> <out.bin> <gx> <gy> [<gx> <gy> ...] [--ring N]"""
import struct, sys, zlib

path, out = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
ring = 2
if '--ring' in args:
    k = args.index('--ring'); ring = int(args[k + 1]); args = args[:k] + args[k + 2:]
seeds = [(int(args[i]), int(args[i + 1])) for i in range(0, len(args), 2)]
want = set()
for (sx, sy) in seeds:
    for dx in range(-ring, ring + 1):
        for dy in range(-ring, ring + 1):
            want.add((sx + dx, sy + dy))
data = open(path, 'rb').read()
WORLD = 0x3C
meshes = {}   # fid -> dict

def parse_nvnm(d, fid):
    ver, crc, pw, pc = struct.unpack_from('<IIII', d, 0)
    gy, gx = struct.unpack_from('<hh', d, 12)
    nv = struct.unpack_from('<I', d, 16)[0]
    o = 20
    verts = [struct.unpack_from('<fff', d, o + 12 * k) for k in range(nv)]
    o += 12 * nv
    nt = struct.unpack_from('<I', d, o)[0]; o += 4
    return ver, pw, gx, gy, verts, nt, o

TRI = None
def parse_rest(d, nt, o, tri_size, order):
    tris = []
    for k in range(nt):
        v0, v1, v2, e0, e1, e2 = struct.unpack_from('<hhhhhh', d, o)
        h = struct.unpack_from('<f', d, o + 12)[0]
        if order == 0:   # height, flags u16, cover u16, unk u8
            fl, cv = struct.unpack_from('<HH', d, o + 16)
        else:            # height, unk u8, flags u16, cover u16
            fl, cv = struct.unpack_from('<HH', d, o + 17)
        tris.append((v0, v1, v2, e0, e1, e2, fl, cv))
        o += tri_size
    ne = struct.unpack_from('<I', d, o)[0]; o += 4
    edges = []
    for k in range(ne):
        typ, nav, tri, edg = struct.unpack_from('<IIhB', d, o)
        edges.append((typ, nav, tri, edg)); o += 11
    nd = struct.unpack_from('<I', d, o)[0]; o += 4
    doors = []
    for k in range(nd):
        tb, crc, door = struct.unpack_from('<hII', d, o); doors.append((tb, crc, door)); o += 10
    return tris, edges, doors, o

def plausible(verts, tris, edges):
    nv, nt, ne = len(verts), len(tris), len(edges)
    for t in tris:
        if not (0 <= t[0] < nv and 0 <= t[1] < nv and 0 <= t[2] < nv): return False
        for i in range(3):
            e = t[3 + i] & 0xFFFF
            if e == 0xFFFF: continue
            if t[6] & (1 << i):
                if e >= ne: return False
            elif e >= nt: return False
    return True

def nvnm_of(body):
    i = 0; big = None
    while i + 6 <= len(body):
        t = body[i:i + 4]; sz = struct.unpack_from('<H', body, i + 4)[0]; i += 6
        if t == b'XXXX':
            big = struct.unpack_from('<I', body, i)[0]; i += sz; continue
        if big is not None: sz = big; big = None
        if t == b'NVNM': return body[i:i + sz]
        i += sz
    return None

def walk(off, end):
    global TRI
    while off < end:
        typ = data[off:off + 4]
        size = struct.unpack_from('<I', data, off + 4)[0]
        if typ == b'GRUP':
            gtype = struct.unpack_from('<i', data, off + 12)[0]
            label = struct.unpack_from('<I', data, off + 8)[0]
            if gtype == 0 and data[off + 8:off + 12] not in (b'WRLD', b'CELL'):
                off += size; continue
            walk(off + 24, off + size); off += size
        else:
            flags, fid = struct.unpack_from('<II', data, off + 8)
            if typ == b'NAVM':
                body = data[off + 24: off + 24 + size]
                if flags & 0x40000: body = zlib.decompress(body[4:])
                d = nvnm_of(body)
                if d:
                    ver, pw, gx, gy, verts, nt, o = parse_nvnm(d, fid)
                    if pw == WORLD:
                        got = None
                        for (ts, order) in ([TRI] if TRI else [(21, 0), (21, 1), (20, 0), (22, 0), (24, 0)]):
                            try:
                                tris, edges, doors, oend = parse_rest(d, nt, o, ts, order)
                            except Exception:
                                continue
                            if plausible(verts, tris, edges):
                                got = (tris, edges, doors); TRI = TRI or (ts, order); break
                        if got:
                            meshes[fid] = dict(gx=gx, gy=gy, verts=verts, tris=got[0], edges=got[1], doors=got[2])
            off += 24 + size

walk(0, len(data))
print('triangle layout', TRI, 'world meshes', len(meshes))
sel = {f for f, m in meshes.items() if (m['gx'], m['gy']) in want}
print('selected', len(sel))
with open(out, 'wb') as f:
    f.write(struct.pack('<I', len(sel)))
    for fid in sorted(sel):
        m = meshes[fid]
        f.write(struct.pack('<IiiIII', fid, m['gx'], m['gy'], len(m['verts']), len(m['tris']), len(m['edges'])))
        for v in m['verts']: f.write(struct.pack('<fff', *v))
        for t in m['tris']: f.write(struct.pack('<HHHHHHI', t[0] & 0xFFFF, t[1] & 0xFFFF, t[2] & 0xFFFF, t[3] & 0xFFFF, t[4] & 0xFFFF, t[5] & 0xFFFF, t[6] | (t[7] << 16)))
        for e in m['edges']: f.write(struct.pack('<IIHB', e[0], e[1], e[2] & 0xFFFF, e[3]))
for (sx, sy) in seeds:
    ids = [ '%08X' % f for f in sel if (meshes[f]['gx'], meshes[f]['gy']) == (sx, sy)]
    print('seed', sx, sy, 'meshes', ids, [len(meshes[int(i,16)]['tris']) for i in ids])
