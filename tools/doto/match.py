"""Pair Dishonored 2 functions, vtables and data with their Death of the Outsider counterparts.

Inputs are two pe_index.py pickles. Output is a pickle with:
  fn:    d2 function begin -> (doto begin, how)    how in {hash, exact, vtable, call, shape, ...}
  vt:    d2 vtable rva -> doto vtable rva            (RTTI class name + subobject offset)
  data:  d2 data rva -> (doto rva, votes, total)     (from aligned rip-relative references)
  cls:   class name -> (d2 vtables, doto vtables)

Usage: python tools/doto/match.py artifacts/doto-lab/d2.idx artifacts/doto-lab/doto.idx artifacts/doto-lab/map.pickle
"""
import collections
import pickle
import sys


def load(path):
    with open(path, 'rb') as f:
        return pickle.load(f)


class Side:
    def __init__(self, idx):
        self.idx = idx
        self.fns = {r[0]: r for r in idx['functions']}
        self.begins = sorted(self.fns)
        self.by_mh = collections.defaultdict(list)
        self.by_sh = collections.defaultdict(list)
        for r in idx['functions']:
            self.by_mh[r[2]].append(r[0])
            self.by_sh[(r[4], r[5])].append(r[0])
        tx = idx['sections'][0]
        self.text = (tx[1], tx[1] + tx[2])

    def in_text(self, rva):
        return self.text[0] <= rva < self.text[1]


def main():
    a = Side(load(sys.argv[1]))
    b = Side(load(sys.argv[2]))
    base_a, base_b = a.idx['image_base'], b.idx['image_base']
    fn = {}          # a begin -> b begin
    how = {}
    rev = {}

    def pair(x, y, why):
        if x in fn or y in rev:
            return fn.get(x) == y
        if x not in a.fns or y not in b.fns:
            return False
        fn[x] = y
        rev[y] = x
        how[x] = why
        return True

    # 1. unique masked hash (relocation-insensitive identical code)
    for h, xs in a.by_mh.items():
        ys = b.by_mh.get(h)
        if len(xs) == 1 and ys and len(ys) == 1:
            pair(xs[0], ys[0], 'hash')
    print('unique masked hash pairs', len(fn))

    # 2. RTTI vtables: same class name and subobject offset
    vt = {}
    cls = {}
    ra, rb = a.idx['rtti'], b.idx['rtti']
    for name, va in ra.items():
        vb = rb.get(name)
        if not vb:
            continue
        cls[name] = (va, vb)
        da = collections.defaultdict(list)
        db = collections.defaultdict(list)
        for row in va:
            da[row[1]].append(row)
        for row in vb:
            db[row[1]].append(row)
        for off, rows in da.items():
            other = db.get(off)
            if not other or len(rows) != 1 or len(other) != 1:
                continue
            x, y = rows[0], other[0]
            vt[x[0]] = y[0]
            # slots: pair index by index only when counts agree
            if len(x[3]) == len(y[3]):
                for sa, sb in zip(x[3], y[3]):
                    if sa in a.fns and sb in b.fns and a.fns[sa][5] > 3:
                        pair(sa, sb, 'vtable')
    print('vtables paired', len(vt), 'fn pairs now', len(fn))

    # 3. propagate through aligned call / data references until fixed point
    data_votes = collections.defaultdict(collections.Counter)
    done = set()
    changed = True
    rounds = 0
    while changed and rounds < 30:
        changed = False
        rounds += 1
        for x, y in list(fn.items()):
            if x in done:
                continue
            ra_, rb_ = a.fns[x], b.fns[y]
            same_shape = ra_[2] == rb_[2] or (ra_[4] == rb_[4] and ra_[5] == rb_[5])
            if not same_shape:
                continue
            done.add(x)
            if len(ra_[6]) == len(rb_[6]):
                for p, q in zip(ra_[6], rb_[6]):
                    p -= 0; q -= 0
                    data_votes[p][q] += 1
                    if a.in_text(p) and b.in_text(q) and p in a.fns and q in b.fns:
                        if pair(p, q, 'ref'):
                            changed = True
            if len(ra_[7]) == len(rb_[7]):
                for p, q in zip(ra_[7], rb_[7]):
                    if p in a.fns and q in b.fns and pair(p, q, 'call'):
                        changed = True
        # 4. shape matches (same mnemonic sequence), unique among the unmatched
        if not changed:
            for key, xs in a.by_sh.items():
                xs = [x for x in xs if x not in fn]
                ys = [y for y in b.by_sh.get(key, []) if y not in rev]
                if len(xs) == 1 and len(ys) == 1 and key[1] >= 6:
                    if pair(xs[0], ys[0], 'shape'):
                        changed = True
        print(f'round {rounds}: fn pairs {len(fn)}')

    # 5. neighbourhood: an unmatched function between two matched neighbours whose gap has the same count
    ia = a.begins
    ib_index = {v: i for i, v in enumerate(b.begins)}
    added = 0
    for i, x in enumerate(ia):
        if x in fn:
            continue
        # previous and next matched in a
        j = i - 1
        while j >= 0 and ia[j] not in fn:
            j -= 1
        k = i + 1
        while k < len(ia) and ia[k] not in fn:
            k += 1
        if j < 0 or k >= len(ia):
            continue
        pj, pk = ib_index[fn[ia[j]]], ib_index[fn[ia[k]]]
        if pk - pj == k - j:
            y = b.begins[pj + (i - j)]
            if y not in rev and a.fns[x][5] == b.fns[y][5] and a.fns[x][4] == b.fns[y][4]:
                pair(x, y, 'neighbour'); added += 1
            elif y not in rev and abs(a.fns[x][5] - b.fns[y][5]) <= max(2, a.fns[x][5] // 10):
                pair(x, y, 'neighbour-loose'); added += 1
    print('neighbourhood pairs', added, 'total', len(fn))

    data = {}
    for p, c in data_votes.items():
        q, n = c.most_common(1)[0]
        data[p] = (q, n, sum(c.values()))
    out = {'fn': fn, 'how': how, 'vt': vt, 'data': data, 'cls': {k: ([r[0] for r in v[0]], [r[0] for r in v[1]]) for k, v in cls.items()}}
    with open(sys.argv[3], 'wb') as f:
        pickle.dump(out, f, protocol=pickle.HIGHEST_PROTOCOL)
    c = collections.Counter(how.values())
    print('by method', dict(c))
    print('functions matched', len(fn), 'of', len(a.fns), f'({100*len(fn)/len(a.fns):.1f}%)')
    print('data rvas mapped', len(data))


if __name__ == '__main__':
    main()
