"""Decode Dishonored 2 .md6anim (magic '80AE') into LOCAL joint transforms.

Format (little endian header, big-endian packed quats), reverse engineered:
  0x04 f32 duration, 0x08 f32 fps(30), 0x0e u16 joints (20 body / 69 arm), 0x10 u16 frames,
  0x12 u16 block count, 0x16 u16 nA (constant-rot joints), 0x18 u16 nB (constant-trans joints),
  0x1e u16 nC (animated-rot joints), 0x20 u16 nD (animated-trans joints).
  0x60: joint-index lists A,B,C,D (u16, each followed by pad entries == joint count; end aligned 16).
  S = aligned end of lists: static data
     A rot quats (6 B each), align16, B trans (3xf32 each), align16,
     then +0x00 identity quat, +0x10 zero vec, +0x20 (3,0,0,0), +0x30 block table:
     nblocks x (u32 size, u32 offset), then u16 pairs (first_frame, K) per block (last K=0).
  Block (offset from table): 32 B header, first-frame values (C rot 6 B each, D trans 12 B each) for
     frame f0, small pad, bitmask (MSB first) of (nC+nD) tracks x K bits (bit i = key at frame f0+1+i),
     then rot streams (popcount*6 B per C track), then trans streams (popcount*12 B per D track).
  Quat (6 B, big-endian 48 bit): [1 spare][c0:15][c1:15][c2:15][idx:2]; comp=(c/32767-0.5)*sqrt2,
     idx = dropped (largest) component, recovered as sqrt(1-sum), the 3 stored are the rest in xyzw order.
  Joints not in any list keep the skeleton bind pose. Between keys: linear/nlerp.
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

PACK = Path(__file__).resolve().parents[1] / 'artifacts/fullbody-assets/pack'
R2 = math.sqrt(2.0)


def unpack_quat(b):
    v = int.from_bytes(b, 'big')
    idx = v & 3
    c = [((v >> s) & 0x7FFF) for s in (32, 17, 2)]
    q = [(x / 32767.0 - 0.5) * R2 for x in c]
    w = math.sqrt(max(0.0, 1.0 - sum(x * x for x in q)))
    out = []
    it = iter(q)
    for i in range(4):
        out.append(w if i == idx else next(it))
    return out


def _lists(d, counts, nj):
    o = 0x60
    res = []
    for n in counts:
        res.append(list(struct.unpack_from('<%dH' % n, d, o)) if n else [])
        o += 2 * n
        while n and struct.unpack_from('<H', d, o)[0] == nj:  # padding entries == joint count
            o += 2
    return res, (o + 15) // 16 * 16


def parse(d):
    assert d[:4] == b'80AE', 'bad magic'
    fps = struct.unpack_from('<f', d, 8)[0]
    nj, nf, nblk = struct.unpack_from('<3H', d, 0xE)
    nA, nB = struct.unpack_from('<2H', d, 0x16)
    nC = struct.unpack_from('<H', d, 0x1E)[0]
    nD = struct.unpack_from('<H', d, 0x20)[0]
    (A, B, C, Dl), o = _lists(d, (nA, nB, nC, nD), nj)
    a16 = lambda x: (x + 15) // 16 * 16
    rotA = [unpack_quat(d[o + 6 * i:o + 6 * i + 6]) for i in range(nA)]
    o = a16(o + 6 * nA)
    transB = [list(struct.unpack_from('<3f', d, o + 12 * i)) for i in range(nB)]
    o = a16(o + 12 * nB)
    t = o + 0x30
    ents = [struct.unpack_from('<II', d, t + 8 * i) for i in range(nblk)]
    u = struct.unpack_from('<%dH' % (2 * nblk), d, t + 8 * nblk)
    blocks = [(u[2 * i], u[2 * i + 1], ents[i][1]) for i in range(nblk)]  # (f0, K, offset)
    return dict(fps=fps, nj=nj, nf=nf, A=A, B=B, C=C, D=Dl, rotA=rotA, transB=transB, blocks=blocks)


def _block(d, p, blk, nxt):
    """returns per-track key dict lists: rot_keys[i]=[(frame,quat)], trans_keys[i]=[(frame,vec)]"""
    f0, K, off = blk
    nC, nD = len(p['C']), len(p['D'])
    q = off + 32
    rk = [[(f0, unpack_quat(d[q + 6 * i:q + 6 * i + 6]))] for i in range(nC)]
    q += 6 * nC
    tk = [[(f0, list(struct.unpack_from('<3f', d, q + 12 * i)))] for i in range(nD)]
    q += 12 * nD
    ntr = nC + nD
    if K == 0 or ntr == 0:
        return rk, tk
    nbytes = (ntr * K + 7) // 8
    for al in (4, 16, 8, 2, 1):
        s = (q + al - 1) // al * al
        masks = []
        bits = int.from_bytes(d[s:s + nbytes], 'big')
        for i in range(ntr):
            m = (bits >> (nbytes * 8 - (i + 1) * K)) & ((1 << K) - 1)
            masks.append([f0 + 1 + k for k in range(K) if (m >> (K - 1 - k)) & 1])
        end = s + nbytes + sum(len(m) for m in masks[:nC]) * 6 + sum(len(m) for m in masks[nC:]) * 12
        if nxt is None or (end + 15) // 16 * 16 == nxt:
            break
    else:
        raise ValueError('block layout mismatch at %#x' % off)
    r = s + nbytes
    for i in range(nC):
        for f in masks[i]:
            rk[i].append((f, unpack_quat(d[r:r + 6]))); r += 6
    for i in range(nD):
        for f in masks[nC + i]:
            tk[i].append((f, list(struct.unpack_from('<3f', d, r)))); r += 12
    return rk, tk


def _interp(keys, nf, quat):
    out = np.zeros((nf, 4 if quat else 3))
    ks = [k[0] for k in keys]
    vs = np.array([k[1] for k in keys], float)
    for f in range(nf):
        j = np.searchsorted(ks, f, side='right') - 1
        if j < 0:
            out[f] = vs[0]; continue
        if j >= len(ks) - 1:
            out[f] = vs[-1]; continue
        t = (f - ks[j]) / (ks[j + 1] - ks[j])
        a, b = vs[j], vs[j + 1]
        if quat and a @ b < 0:
            b = -b
        v = a * (1 - t) + b * t
        out[f] = v / np.linalg.norm(v) if quat else v
    return out


def skeleton_for(path, nj):
    s = str(path).replace('\\', '/').lower()
    name = 'skeleton_body.json' if nj == 20 else ('skeleton_left_arm.json' if 'leftarm' in s else 'skeleton_right_arm.json')
    return name, json.load(open(PACK / name))


def decode(path, bind=True):
    """-> dict(frame_rate, skeleton, frames=[[ (quat xyzw, trans xyz, scale) per joint ]], rot_joints, trans_joints)"""
    d = Path(path).read_bytes()
    p = parse(d)
    nf, nj = p['nf'], p['nj']
    sk_name, sk = skeleton_for(path, nj)
    q = np.array([sk['local_bind'][j]['rot_xyzw'] for j in range(nj)], float)
    t = np.array([sk['local_bind'][j]['trans'] for j in range(nj)], float)
    s = np.array([sk['local_bind'][j]['scale'] for j in range(nj)], float)
    Q = np.repeat(q[None], nf, 0); T = np.repeat(t[None], nf, 0)
    for j, v in zip(p['A'], p['rotA']):
        Q[:, j] = v
    for j, v in zip(p['B'], p['transB']):
        T[:, j] = v
    blks = p['blocks']
    rkeys = [[] for _ in p['C']]; tkeys = [[] for _ in p['D']]
    for bi, blk in enumerate(blks):
        nxt = blks[bi + 1][2] if bi + 1 < len(blks) else None
        if nxt is None:  # last block: ends at footer string (unknown) -> accept
            pass
        rk, tk = _block(d, p, blk, nxt)
        for i in range(len(rk)): rkeys[i] += rk[i]
        for i in range(len(tk)): tkeys[i] += tk[i]
    for j, k in zip(p['C'], rkeys):
        Q[:, j] = _interp(k, nf, True)
    for j, k in zip(p['D'], tkeys):
        T[:, j] = _interp(k, nf, False)
    frames = [[(Q[f, j], T[f, j], s[j]) for j in range(nj)] for f in range(nf)]
    return dict(frame_rate=p['fps'], skeleton=sk_name, frames=frames, Q=Q, T=T, S=s,
                rot_joints=sorted(p['A'] + p['C']), trans_joints=sorted(p['B'] + p['D']),
                animated_rot=p['C'], animated_trans=p['D'], blocks=p['blocks'])


def main(argv):
    for path in argv:
        d = Path(path).read_bytes(); p = parse(d)
        print(path, 'joints', p['nj'], 'frames', p['nf'], 'fps %.2f' % p['fps'], 'blocks', p['blocks'])
        print('  constRot', p['A'], '\n  constTrans', p['B'], '\n  animRot', p['C'], '\n  animTrans', p['D'])


if __name__ == '__main__':
    main(sys.argv[1:])
