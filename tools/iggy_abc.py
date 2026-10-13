"""Minimal AVM2 (ActionScript 3) ABC parser for the game's Iggy movies: finds the ABC block, names methods, disassembles bodies.
Movies come from the user's install (extract ui/*.iggy first); offsets printed are file offsets, as used by menu patches.
python tools/iggy_abc.py <movie.iggy> list <substring>    # methods whose Class.name contains substring: (code offset, length, max_stack)
python tools/iggy_abc.py <movie.iggy> dis <Class.method>  # disassemble
python tools/iggy_abc.py <movie.iggy> refs <name>         # methods whose code references this name"""
import struct, sys

class R:
    def __init__(s, d, p): s.d, s.p = d, p
    def u8(s): v = s.d[s.p]; s.p += 1; return v
    def u16(s): v = struct.unpack_from('<H', s.d, s.p)[0]; s.p += 2; return v
    def s24(s): v = int.from_bytes(s.d[s.p:s.p+3], 'little', signed=True); s.p += 3; return v
    def u30(s):
        v = 0
        for i in range(5):
            b = s.d[s.p]; s.p += 1; v |= (b & 0x7f) << (7 * i)
            if not b & 0x80: break
        return v
    def d64(s): s.p += 8

def parse(d, p):
    r = R(d, p); r.u16(); r.u16()
    n = r.u30(); [r.u30() for _ in range(max(n - 1, 0))]
    n = r.u30(); [r.u30() for _ in range(max(n - 1, 0))]
    n = r.u30(); [r.d64() for _ in range(max(n - 1, 0))]
    n = r.u30(); strings = ['']
    for _ in range(max(n - 1, 0)):
        l = r.u30(); strings.append(d[r.p:r.p+l].decode('utf-8', 'replace')); r.p += l
    if n > 200000: raise ValueError
    n = r.u30(); ns = [0]
    for _ in range(max(n - 1, 0)): r.u8(); ns.append(r.u30())
    n = r.u30()
    for _ in range(max(n - 1, 0)):
        c = r.u30(); [r.u30() for _ in range(c)]
    n = r.u30(); mn = [None]
    for _ in range(max(n - 1, 0)):
        k = r.u8()
        if k in (7, 0x0d): r.u30(); mn.append(strings[r.u30()])
        elif k in (0x0f, 0x10): mn.append(strings[r.u30()])
        elif k in (0x11, 0x12): mn.append('<rtqnl>')
        elif k in (0x09, 0x0e): mn.append(strings[r.u30()]); r.u30()
        elif k in (0x1b, 0x1c): r.u30(); mn.append('<mnl>')
        elif k == 0x1d: r.u30(); c = r.u30(); [r.u30() for _ in range(c)]; mn.append('<generic>')
        else: raise ValueError('mn kind %x' % k)
    n = r.u30(); methods = []
    for _ in range(n):
        pc = r.u30(); r.u30(); [r.u30() for _ in range(pc)]; nm = r.u30(); fl = r.u8()
        if fl & 8:
            oc = r.u30(); [(r.u30(), r.u8()) for _ in range(oc)]
        if fl & 0x80: [r.u30() for _ in range(pc)]
        methods.append(strings[nm] if nm < len(strings) else '?')
    n = r.u30()
    for _ in range(n):
        r.u30(); ic = r.u30(); [(r.u30(), r.u30()) for _ in range(ic)]
    def traits(owner, names):
        for _ in range(r.u30()):
            name = mn[r.u30()]; kb = r.u8(); k = kb & 15
            if k in (0, 6): r.u30(); r.u30(); vi = r.u30(); (r.u8() if vi else None)
            elif k in (1, 2, 3): r.u30(); m = r.u30(); names[m] = '%s.%s%s' % (owner, {2: 'get ', 3: 'set '}.get(k, ''), name)
            elif k in (4, 5): r.u30(); m = r.u30(); names[m] = '%s.%s' % (owner, name)
            if kb & 0x40: [r.u30() for _ in range(r.u30())]
    n = r.u30(); names = {}; cls = []
    for _ in range(n):
        name = mn[r.u30()]; r.u30(); fl = r.u8()
        if fl & 8: r.u30()
        [r.u30() for _ in range(r.u30())]
        names[r.u30()] = name + '.<init>'; traits(name, names); cls.append(name)
    for i in range(n): names[r.u30()] = cls[i] + '.<cinit>'; traits(cls[i], names)
    for _ in range(r.u30()): r.u30(); traits('script', names)
    bodies = {}
    for _ in range(r.u30()):
        m = r.u30(); ms = r.u30(); r.u30(); r.u30(); r.u30(); cl = r.u30(); bodies[m] = (r.p, cl, ms); r.p += cl
        for _ in range(r.u30()): [r.u30() for _ in range(5)]
        traits('act', {})
    return dict(strings=strings, mn=mn, names=names, bodies=bodies, end=r.p)

def find(d):
    p = 0
    while True:
        p = d.find(b'\x10\x00\x2e\x00', p)
        if p < 0: raise SystemExit('no abc')
        try:
            a = parse(d, p)
            if len(a['bodies']) > 50: a['start'] = p; return a
        except Exception: pass
        p += 1

# opcode -> operand kinds: u=u30 (mn for m), s=s24, b=u8
OPS = {0x24: ('pushbyte', 'b'), 0x25: ('pushshort', 'u'), 0x2c: ('pushstring', 'S'), 0x2d: ('pushint', 'u'), 0x2f: ('pushdouble', 'u'),
       0x60: ('getlex', 'm'), 0x66: ('getproperty', 'm'), 0x61: ('setproperty', 'm'), 0x68: ('initproperty', 'm'), 0x5d: ('findpropstrict', 'm'),
       0x5e: ('findproperty', 'm'), 0x46: ('callproperty', 'mu'), 0x4f: ('callpropvoid', 'mu'), 0x4a: ('constructprop', 'mu'), 0x45: ('callsuper', 'mu'),
       0x4e: ('callsupervoid', 'mu'), 0x4c: ('callproplex', 'mu'), 0x62: ('getlocal', 'u'), 0x63: ('setlocal', 'u'), 0x6c: ('getslot', 'u'), 0x6d: ('setslot', 'u'),
       0x41: ('call', 'u'), 0x42: ('construct', 'u'), 0x49: ('constructsuper', 'u'), 0x55: ('newobject', 'u'), 0x56: ('newarray', 'u'), 0x58: ('newclass', 'u'),
       0x40: ('newfunction', 'u'), 0x80: ('coerce', 'm'), 0x86: ('astype', 'm'), 0xb2: ('istype', 'm'), 0x6a: ('deleteproperty', 'm'), 0x59: ('getdescendants', 'm'),
       0x32: ('hasnext2', 'uu'), 0xc2: ('inclocal_i', 'u'), 0xc3: ('declocal_i', 'u'), 0x92: ('inclocal', 'u'), 0x94: ('declocal', 'u'), 0x08: ('kill', 'u'),
       0x65: ('getscopeobject', 'b'), 0x04: ('getsuper', 'm'), 0x05: ('setsuper', 'm'), 0x5a: ('newcatch', 'u'), 0xf0: ('debugline', 'u'), 0xf1: ('debugfile', 'u'),
       0xef: ('debug', 'bubu'), 0x31: ('pushnamespace', 'u'), 0x43: ('callmethod', 'uu'), 0x44: ('callstatic', 'uu'), 0x06: ('dxns', 'u'), 0x53: ('applytype', 'u')}
for o, n in ((0x0c, 'ifnlt'), (0x0d, 'ifnle'), (0x0e, 'ifngt'), (0x0f, 'ifnge'), (0x10, 'jump'), (0x11, 'iftrue'), (0x12, 'iffalse'), (0x13, 'ifeq'), (0x14, 'ifne'),
             (0x15, 'iflt'), (0x16, 'ifle'), (0x17, 'ifgt'), (0x18, 'ifge'), (0x19, 'ifstricteq'), (0x1a, 'ifstrictne')):
    OPS[o] = (n, 's')
SIMPLE = {0x02: 'nop', 0x03: 'throw', 0x09: 'label', 0x1c: 'pushwith', 0x1d: 'popscope', 0x1e: 'nextname', 0x20: 'pushnull', 0x21: 'pushundefined', 0x23: 'nextvalue',
          0x26: 'pushtrue', 0x27: 'pushfalse', 0x28: 'pushnan', 0x29: 'pop', 0x2a: 'dup', 0x2b: 'swap', 0x30: 'pushscope', 0x47: 'returnvoid', 0x48: 'returnvalue',
          0x57: 'newactivation', 0x64: 'getglobalscope', 0x70: 'convert_s', 0x73: 'convert_i', 0x74: 'convert_u', 0x75: 'convert_d', 0x76: 'convert_b', 0x78: 'checkfilter',
          0x82: 'coerce_a', 0x85: 'coerce_s', 0x87: 'astypelate', 0x90: 'negate', 0x91: 'increment', 0x93: 'decrement', 0x95: 'typeof', 0x96: 'not', 0x97: 'bitnot',
          0xa0: 'add', 0xa1: 'subtract', 0xa2: 'multiply', 0xa3: 'divide', 0xa4: 'modulo', 0xa5: 'lshift', 0xa6: 'rshift', 0xa7: 'urshift', 0xa8: 'bitand', 0xa9: 'bitor',
          0xaa: 'bitxor', 0xab: 'equals', 0xac: 'strictequals', 0xad: 'lessthan', 0xae: 'lessequals', 0xaf: 'greaterthan', 0xb0: 'greaterequals', 0xb1: 'instanceof',
          0xb3: 'in', 0xc0: 'increment_i', 0xc1: 'decrement_i', 0xc4: 'negate_i', 0xc5: 'add_i', 0xc6: 'subtract_i', 0xc7: 'multiply_i', 0xd0: 'getlocal0',
          0xd1: 'getlocal1', 0xd2: 'getlocal2', 0xd3: 'getlocal3', 0xd4: 'setlocal0', 0xd5: 'setlocal1', 0xd6: 'setlocal2', 0xd7: 'setlocal3', 0x71: 'esc_xelem',
          0x72: 'esc_xattr', 0x83: 'coerce_i', 0x88: 'coerce_u', 0x89: 'coerce_o', 0x81: 'coerce_b', 0x84: 'coerce_d', 0x35: 'li8', 0x01: 'bkpt'}

def dis(d, a, m):
    p, cl, _ = a['bodies'][m]; r = R(d, p); out = []
    while r.p < p + cl:
        at = r.p - p; op = r.u8()
        if op in SIMPLE: out.append((at, SIMPLE[op], '')); continue
        if op == 0x1b:
            dflt = r.s24(); c = r.u30(); cs = [r.s24() for _ in range(c + 1)]; out.append((at, 'lookupswitch', '%d %s' % (dflt, cs))); continue
        if op not in OPS: out.append((at, 'op_%02x' % op, '?')); break
        name, kinds = OPS[op]; args = []
        for k in kinds:
            if k == 'b': args.append(str(r.u8()))
            elif k == 's': v = r.s24(); args.append('-> %d' % (r.p - p + v))
            elif k == 'm': args.append(str(a['mn'][r.u30()]))
            elif k == 'S': args.append(repr(a['strings'][r.u30()]))
            else: args.append(str(r.u30()))
        out.append((at, name, ' '.join(args)))
    return out

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read(); a = find(d); cmd = sys.argv[2]; arg = sys.argv[3] if len(sys.argv) > 3 else ''
    print('abc at', a['start'], 'bodies', len(a['bodies']), file=sys.stderr)
    if cmd == 'list':
        for m, n in sorted(a['names'].items(), key=lambda x: x[1]):
            if arg.lower() in n.lower() and m in a['bodies']: print(n, a['bodies'][m])
    elif cmd == 'dis':
        for m, n in a['names'].items():
            if n == arg and m in a['bodies']:
                print('==', n, a['bodies'][m])
                for at, op, x in dis(d, a, m): print('%5d %-16s %s' % (at, op, x))
    elif cmd == 'refs':
        for m, n in a['names'].items():
            if m in a['bodies'] and any(arg in x for _, _, x in dis(d, a, m)): print(n)
