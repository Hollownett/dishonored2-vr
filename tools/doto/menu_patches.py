"""Generate the native menu and glyph movie patch tables for a port target, by method and string name.

    python tools/doto/menu_patches.py                 # target doto: write the two headers, then self-check against D2
    python tools/doto/menu_patches.py --target doto --check-only    # generate in memory, self-check, write nothing
    python tools/doto/menu_patches.py --no-self-check

Outputs (target "doto"):
  src/menu_patches_doto.hpp                     same file shape as src/menu_patches.hpp (namespace d2vr::menu_patches,
                                                structs Edit and Movie, array `movies`); include it INSTEAD of menu_patches.hpp
  src/controller_glyph_atlas_doto_edits.hpp     `inline constexpr NativeArtMovieEdit native_art_movie_edits[]={...};` only;
                                                include it INSIDE namespace d2vr in place of the D2 array, after struct
                                                NativeArtMovieEdit is defined

How: the UI movies (ui/*.iggy) are read from the game's resource archives (read-only; cached under
artifacts/doto-lab/movies/<target>/). Every edit is regenerated from the movie's own AS3 bytecode: methods are found by name
(or by their instruction shape), operand indices (multinames, strings, doubles) are read from the target's constant pool, and the
same byte shapes the Dishonored 2 patches use are emitted. Each row is pinned to the full SHA-256 of the movie before and after it
(a movie with a native menu patch starts from that patched hash, as in src/controller_glyph_atlas.hpp). Rows whose method does not
match the base build's structure are skipped and listed in the header comment.

Self-check: the same code is run on the Dishonored 2 movies and compared with the shipped src/menu_patches.hpp (byte-for-byte text)
and native_art_movie_edits (every field of every row it can generate); rows it cannot reproduce are reported.

Read-only on the game folders. No game process is touched.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))
import iggy_abc as A  # noqa: E402
from extract_arm_assets import index_entries, unpack_asset  # noqa: E402
import target as targets  # noqa: E402

CONTAINERS = ("game1", "game2", "game3", "game4")


# ---------------------------------------------------------------------------------------------------------------- extraction
def _index_key(p):
    stem = p.stem
    series = stem.split("_")[0]
    level = 2 if stem.endswith("_patch") else (0 if "_" not in stem else 1)
    return (series, level, stem)


def _candidate_files(base, series, stem, resource_id):
    """Resource containers an entry may live in, most likely first. D2 numbers the files of a series (0 = base, 1..5 = _00N,
    6 = _patch); DOTO's index uses ids relative to the index file, so try both readings and let the inflate check decide."""
    out = []
    if resource_id == 8192:
        out.append(base / "shared_2_3.sharedrsc")
    elif 0 <= resource_id <= 6:
        suffix = "" if resource_id == 0 else "_patch" if resource_id == 6 else f"_{resource_id:03d}"
        out.append(base / (series + suffix + ".resources"))
    out.append(base / (stem + ".resources"))
    out.extend(sorted(base.glob(series + "*.resources")))
    seen, res = set(), []
    for p in out:
        if p not in seen and p.is_file():
            seen.add(p)
            res.append(p)
    return res


def extract_movies(tgt, log=print):
    """{name: bytes} for every ui/*.iggy of the target's final patch level; cached on disk."""
    game = tgt.exe.parent
    base = game / "base"
    cache = tgt.lab / "movies" / tgt.name
    indexes = sorted(base.glob("game?*.index"), key=_index_key)
    stamp = [[p.name, p.stat().st_size, p.stat().st_mtime_ns] for p in indexes]
    manifest = cache / "manifest.json"
    if manifest.is_file():
        try:
            info = json.loads(manifest.read_text())
            if info.get("sources") == stamp and all((cache / n).is_file() and (cache / n).stat().st_size == s
                                                    for n, s in info["sizes"].items()):
                return {n: (cache / n).read_bytes() for n in info["sizes"]}
        except (ValueError, KeyError, OSError):
            pass
    log(f"extracting {tgt.name} movies from {base}")
    found = {}
    for ix in indexes:
        series = ix.stem.split("_")[0]
        with ix.open("rb") as stream:
            for e in index_entries(stream):
                dest = e["destination"].replace("\\", "/").lower()
                if dest.startswith("ui/") and dest.endswith(".iggy") and 0 < e["size"] <= 32 * 1024 * 1024 and e["packed"]:  # skips the huge font movies
                    found[dest] = (series, ix.stem, e)  # later index wins
    movies = {}
    for dest, (series, stem, e) in sorted(found.items()):
        data, last = None, None
        for path in _candidate_files(base, series, stem, e["resource_id"]):
            try:
                with path.open("rb") as f:
                    f.seek(e["offset"])
                    packed = f.read(e["packed"])
                data = unpack_asset(packed, e["size"])
                break
            except Exception as ex:  # wrong container: bad zlib stream or short read
                last = ex
        if data is None:
            raise RuntimeError(f"cannot read {dest}: {last}")
        movies[dest[3:]] = data
    cache.mkdir(parents=True, exist_ok=True)
    for n, data in movies.items():
        (cache / n).write_bytes(data)
    manifest.write_text(json.dumps({"sources": stamp, "sizes": {n: len(d) for n, d in movies.items()},
                                    "sha256": {n: hashlib.sha256(d).hexdigest() for n, d in movies.items()}}, indent=1))
    return movies


# ---------------------------------------------------------------------------------------------------------------- ABC helpers
class Movie:
    def __init__(self, data):
        self.data = data
        self.abc = A.find(data)
        a = self.abc
        r = A.R(data, a["start"])
        r.u16(); r.u16()
        n = r.u30(); [r.u30() for _ in range(max(n - 1, 0))]
        n = r.u30(); [r.u30() for _ in range(max(n - 1, 0))]
        n = r.u30()
        self.doubles = [float("nan")]
        for _ in range(max(n - 1, 0)):
            self.doubles.append(struct.unpack_from("<d", data, r.p)[0])
            r.p += 8
        self._ops = {}
        self._uses = None

    def method(self, name):
        ms = [m for m, n in self.abc["names"].items() if n == name and m in self.abc["bodies"]]
        if len(ms) != 1:
            raise LookupError(f"{name}: {len(ms)} bodies")
        return ms[0]

    def body(self, m):
        p, cl, _ = self.abc["bodies"][m]
        return p, cl

    def ops(self, m):
        """[(at, name, [operands], end)] of one method body; m/S operands are pool indices (see name_of)."""
        if m in self._ops:
            return self._ops[m]
        p, cl = self.body(m)
        r = A.R(self.data, p)
        out = []
        while r.p < p + cl:
            at = r.p - p
            op = r.u8()
            if op in A.SIMPLE:
                out.append((at, A.SIMPLE[op], [], r.p - p))
                continue
            if op == 0x1b:
                dflt = r.s24(); c = r.u30(); cs = [r.s24() for _ in range(c + 1)]
                out.append((at, "lookupswitch", [dflt, c] + cs, r.p - p))
                continue
            if op not in A.OPS:
                break
            name, kinds = A.OPS[op]
            vals = []
            for k in kinds:
                vals.append(r.u8() if k == "b" else r.s24() if k == "s" else r.u30())
            out.append((at, name, vals, r.p - p))
        self._ops[m] = out
        return out

    def mn(self, i):
        return self.abc["mn"][i]

    def operand_name(self, op):
        at, name, vals, end = op
        kinds = A.OPS_BY_NAME.get(name, "")
        if vals and kinds.startswith("m"):
            return self.mn(vals[0])
        if vals and kinds.startswith("S"):
            return self.abc["strings"][vals[0]]
        return None

    def shape(self, m):
        """Operand-free instruction sequence: structural identity of a method across builds."""
        return [o[1] for o in self.ops(m)]

    def mn_uses(self):
        """{(opname, multiname name): Counter of pool index} over every method body."""
        if self._uses is None:
            u = collections.defaultdict(collections.Counter)
            for m in self.abc["bodies"]:
                try:
                    for op in self.ops(m):
                        n = self.operand_name(op)
                        if n is not None and A.OPS_BY_NAME.get(op[1], "").startswith("m"):
                            u[(op[1], n)][op[2][0]] += 1
                except Exception:
                    pass
            self._uses = u
        return self._uses

    def common_mn(self, opname, name):
        c = self.mn_uses().get((opname, name))
        if not c:
            raise LookupError(f"no {opname} {name}")
        return c.most_common(1)[0][0]


A.OPS_BY_NAME = {name: kinds for name, kinds in A.OPS.values()}
SKIP = "skip"


def u30(v):
    out = bytearray()
    while True:
        b = v & 0x7f
        v >>= 7
        out.append(b | 0x80 if v else b)
        if not v:
            return bytes(out)


def s24(v):
    return (v & 0xffffff).to_bytes(3, "little")


def sig(ops, names):
    """Index of the first run of ops whose names equal `names`, else None."""
    seq = [o[1] for o in ops]
    for i in range(len(seq) - len(names) + 1):
        if seq[i:i + len(names)] == names:
            return i
    return None


# ---------------------------------------------------------------------------------------------------------------- menu patches
def menu_edits(mv, kind, keep_exit=False):
    """[(offset, old, new)] for one pause or main menu movie, in the order src/menu_patches.hpp lists them."""
    d = mv.data
    S = mv.abc["strings"]
    gs = mv.common_mn("getlex", "GameService")
    sn = mv.common_mn("callpropvoid", "sendNotification")
    edits = []

    def notify(name, string):
        m = mv.method(name)
        p, cl = mv.body(m)
        code = b"\xd0\x30\x60" + u30(gs) + b"\x2c" + u30(S.index(string)) + b"\x4f" + u30(sn) + b"\x01"
        if len(code) > cl:
            raise ValueError(f"{name} too short for the notification")
        return p, bytes(d[p:p + cl]), code + b"\x47" * (cl - len(code))

    def string_edit(old, new):
        assert len(old) == len(new)
        o = old.encode()
        i = d.find(o)
        if i < 0:
            raise LookupError(old)
        return i, o, new.encode()

    if kind == "pause":
        m = mv.method("PauseMenuData._initData")
        p, _ = mv.body(m)
        ops = mv.ops(m)
        k = [i for i, o in enumerate(ops) if o[1] == "pushstring" and S[o[2][0]] == "ui_j_tab_stats"]
        assert len(k) == 1 and ops[k[0] + 2][1] == "pushtrue", "stats entry flag"
        # callproperty translate follows the string; then the 'new' flag
        at = ops[k[0] + 2][0]
        edits.append((p + at, b"\x26", b"\x27"))
        m = mv.method("PauseMenuData._initMenuEntries")
        p, _ = mv.body(m)
        ops = mv.ops(m)
        i = sig(ops, ["getlocal0", "getproperty", "getlocal0", "getproperty", "callpropvoid"] * 2 +
                ["getlocal0", "getproperty", "iffalse"] + ["getlocal0", "getproperty", "getlocal0", "getproperty", "callpropvoid"])
        names = [mv.operand_name(ops[j]) for j in range(i, i + 18)]
        assert names[3] == "_optionsEntry" and names[8] == "_tutorialsEntry" and names[11] == "statsEntryAvailable" \
            and names[16] == "_statsEntry", names
        start, end = ops[i][0], ops[i + 17][3]
        assert end - start == 44 and ops[i + 10][0] == start + 24
        old = bytes(d[p + start:p + end])
        b = [old[0:12], old[12:24], old[24:32], old[32:44]]
        edits.append((p + start, old, b[0] + b[3] + b[1] + b"\xd0\x29" * 4))
        edits.append(notify("PauseMenuScreen._onStatsClicked", "ui_pause_devGUI"))
        edits.append(string_edit("ui_pause_devGUI", "VR_SETTINGS_EVT"))
        edits.append(string_edit("ui_j_tab_stats", "VR Settings   "))
    elif keep_exit:
        edits = main_keep_exit_edits(mv)
    else:
        edits.append(notify("MainHomeScreen._onQuit", "ui_main_home_quitMessage"))
        m = mv.method("MainMenuData._initData")
        p, _ = mv.body(m)
        ops = mv.ops(m)
        k = [i for i, o in enumerate(ops) if o[1] == "getproperty" and mv.operand_name(o) == "_creditsEntry"
             and ops[i + 1][1] == "callpropvoid"]
        assert len(k) == 1, "credits entry"
        i = k[0] - 3
        assert [o[1] for o in ops[i:i + 23]] == (
            ["getlocal0", "getproperty", "getlocal0", "getproperty", "callpropvoid",      # credits addItem
             "getlocal0", "getproperty", "iffalse",                                       # if (DLCEntryEnabled)
             "getlocal0", "getproperty", "getlocal0", "getproperty", "callpropvoid",      # DLC addItem
             "getlex", "getproperty", "getlex", "getproperty", "ifne",                    # Platform != PC guard
             "getlocal0", "getproperty", "getlocal0", "getproperty", "callpropvoid"]) and             mv.operand_name(ops[i + 6]) == "DLCEntryEnabled" and mv.operand_name(ops[i + 21]) == "_quitEntry", "main menu entry run"
        start = ops[i][0]
        assert ops[i + 22][3] - start == 59
        old = bytes(d[p + start:p + start + 59])
        b = [old[0:12], old[12:20], old[20:32], old[32:47], old[47:59]]
        new = b[4] + b[0] + b[1] + b[2] + b"\x24\x00" + b"\x29" + b"\xd0\x29" * 6
        assert len(new) == 59
        edits.append((p + start, old, new))
        edits.append(string_edit("ui_main_home_quitMessage", "VR_SETTINGS_OPEN_REQUEST"))
        edits.append(string_edit("ui_main_home_quit", "VR Settings      "))
        # shipped order: _onQuit, _initData, quitMessage, quit
        edits = [edits[0], edits[1], edits[2], edits[3]]
    return edits


def main_keep_exit_edits(mv):
    """Main menu without losing Exit: the Credits entry is repurposed. Its click is routed by MainHomeScreen._onEntrySelected
    (ExtraItemDefinition -> sendNotification(entry.gameCallback)), so only constants change:
      * MainMenuGameCallbacks.SHOW_CREDITS (pushstring 'ShowCredits') points at a pool string renamed to VR_SETTINGS_EVT;
      * the Credits label key in MainMenuData.updateEntriesLabel (pushstring 'ui_credits') points at a pool string renamed to
        'VR Settings    ';
      * _onEntrySelected disables InputsManager before a notification (the credits screen re-enables it): pushfalse -> pushtrue.
    Both renamed strings are 15-char debug strings used only by the main-menu simulator, never by a multiname. The Exit entry,
    _onQuit and the quit message are untouched."""
    d = mv.data
    S = mv.abc["strings"]
    mn_names = set(n for n in mv.abc["mn"] if n)
    pushers = collections.defaultdict(set)
    for m in mv.abc["bodies"]:
        for o in mv.ops(m):
            if o[1] == "pushstring":
                pushers[o[2][0]].add(mv.abc["names"].get(m, "?"))
    edits = []

    def operand_edit(method, op_name, string, new_string):
        m = mv.method(method)
        p, _ = mv.body(m)
        hits = [o for o in mv.ops(m) if o[1] == "pushstring" and S[o[2][0]] == string]
        assert len(hits) == 1, (method, string)
        o = hits[0]
        new = b"," + u30(S.index(new_string))
        old = bytes(d[p + o[0]:p + o[3]])
        assert len(new) == len(old), "pool index width differs"
        return p + o[0], old, new

    def string_edit(old, new):
        assert len(old) == len(new) and S.count(old) == 1 and old not in mn_names
        assert all(n.startswith("MainMenuSimulator") for n in pushers[S.index(old)]), pushers[S.index(old)]
        o = old.encode()
        assert d.count(o) == 1, old
        return d.find(o), o, new.encode()

    m = mv.method("MainHomeScreen._onEntrySelected")
    p, _ = mv.body(m)
    ops = mv.ops(m)
    i = sig(ops, ["getlex", "pushfalse", "setproperty"])
    assert mv.operand_name(ops[i]) == "InputsManager" and mv.operand_name(ops[i + 2]) == "enabled"
    edits.append((p + ops[i + 1][0], b"'", b"&"))
    edits.append(operand_edit("MainMenuGameCallbacks.<cinit>", "pushstring", "ShowCredits", "Controls Scheme"))
    edits.append(operand_edit("MainMenuData.updateEntriesLabel", "pushstring", "ui_credits", "Continue locked"))
    edits.append(string_edit("Controls Scheme", "VR_SETTINGS_EVT"))
    edits.append(string_edit("Continue locked", "VR Settings    "))
    return edits


def menu_header(movies):
    """movies: [(suffix, original bytes, patched bytes, edits)] -> text of src/menu_patches*.hpp (as tools/gen_menu_patches.py)."""
    out = ["#pragma once",
           "// Generated by tools/gen_menu_patches.py; complete extracted movie hashes pin ownership.",
           "#include <array>", "#include <cstddef>", "namespace d2vr::menu_patches {",
           "struct Edit {std::size_t offset;const unsigned char* old_bytes;const unsigned char* new_bytes;std::size_t size;};",
           "struct Movie {const char* suffix;std::size_t size;const Edit* edits;std::size_t count;std::array<unsigned char,32> original_sha256,patched_sha256;};"]
    arr = lambda v: ",".join("0x%02x" % b for b in v)
    entries = []
    for i, (suffix, original, patched, edits) in enumerate(movies):
        gen = []
        for j, (off, old, new) in enumerate(edits):
            out.append(f"inline constexpr unsigned char m{i}_o{j}[]={{" + arr(old) + "};")
            out.append(f"inline constexpr unsigned char m{i}_n{j}[]={{" + arr(new) + "};")
            gen.append("{%d,m%d_o%d,m%d_n%d,%d}" % (off, i, j, i, j, len(old)))
        out.append(f"inline constexpr Edit m{i}_edits[]={{" + ",".join(gen) + "};")
        entries.append('{"%s",%d,m%d_edits,%d,{%s},{%s}}' % (suffix, len(original), i, len(edits),
                                                             arr(hashlib.sha256(original).digest()), arr(hashlib.sha256(patched).digest())))
    out.append("inline constexpr Movie movies[]={" + ",".join(entries) + "};")
    out.append("} // namespace d2vr::menu_patches")
    return "\n".join(out) + "\n"


def build_menu(movies, keep_exit=False):
    """{name: Movie} -> (header text, {name: patched bytes})"""
    rows, patched_by = [], {}
    for suffix in sorted(n for n in movies if n in ("corvo_pause_menu.iggy", "emily_pause_menu.iggy", "main_menu.iggy")):
        mv = Movie(movies[suffix])
        edits = menu_edits(mv, "main" if suffix == "main_menu.iggy" else "pause", keep_exit)
        patched = bytearray(movies[suffix])
        occupied = set()
        for off, old, new in edits:
            assert len(old) == len(new) and patched[off:off + len(old)] == old, (suffix, off)
            span = set(range(off, off + len(old)))
            assert not occupied & span, (suffix, off, "overlap")
            occupied |= span
            patched[off:off + len(new)] = new
        rows.append((suffix, movies[suffix], bytes(patched), edits))
        patched_by[suffix] = bytes(patched)
    return menu_header(rows), patched_by


# ---------------------------------------------------------------------------------------------------------------- glyph rows
# Row kinds, in chain order within a movie: crosshair, button image, then the rest.
RANK = {"crosshair": 0, "button": 1, "wheel_shortcuts": 2, "quick_potion": 2, "options_back": 2, "wheel_potions": 3, "options_back_jump": 3}


def button_row(mv):
    """ControllerButtonDefinition.getButtonImage (found by shape: 'pushnull coerce_s setlocal 4 jump' + image_XBOX + lookupswitch):
    retarget the entry jump to the label of the image_XBOX branch. -> (offset, old4, new4) or None."""
    hits = []
    for m in mv.abc["bodies"]:
        ops = mv.ops(m)
        names = [o[1] for o in ops]
        if "lookupswitch" not in names:
            continue
        if not any(o[1] == "getproperty" and mv.operand_name(o) == "image_XBOX" for o in ops):
            continue
        for i, o in enumerate(ops):
            if o[1] == "jump" and i >= 3 and names[i - 3:i] == ["pushnull", "coerce_s", "setlocal"] and ops[i - 1][2] == [4]:
                x = [j for j, q in enumerate(ops) if q[1] == "getproperty" and mv.operand_name(q) == "image_XBOX"][0]
                lab = max(j for j in range(x) if ops[j][1] == "label")
                at = o[0]
                p, _ = mv.body(m)
                old = bytes(mv.data[p + at:p + at + 4])
                new = b"\x10" + s24(ops[lab][0] - (at + 4))
                hits.append((p + at, old, new))
                break
    if len(hits) != 1:
        return None if not hits else SKIP
    return hits[0]


def gamepad_branch_row(mv, base_mv, name):
    """Method `name`: the branch on PCControllerTypeHandler.isGamepad always falls through to the pad layout
    (iffalse -> pop + 3 nops). Skipped unless the method has the same instruction shape as in the base build."""
    m = mv.method(name)
    if base_mv is not None and mv.shape(m) != base_mv.shape(base_mv.method(name)):
        return SKIP
    ops = mv.ops(m)
    i = sig(ops, ["getlex", "getproperty"])
    while i is not None:
        if mv.operand_name(ops[i]) == "PCControllerTypeHandler" and mv.operand_name(ops[i + 1]) == "isGamepad":
            break
        nxt = sig(ops[i + 1:], ["getlex", "getproperty"])
        i = None if nxt is None else i + 1 + nxt
    if i is None:
        return None
    j = i + 2 + (1 if ops[i + 2][1] == "convert_b" else 0)
    if ops[j][1] != "iffalse":
        return SKIP
    p, _ = mv.body(m)
    at = ops[j][0]
    return p + at, bytes(mv.data[p + at:p + at + 4]), b"\x29\x02\x02\x02"


def quick_potion_row(mv, base_mv):
    name = "QuickPotion._updateControllerType"
    m = mv.method(name)
    if base_mv is not None and mv.shape(m) != base_mv.shape(base_mv.method(name)):
        return SKIP
    ops = mv.ops(m)
    for i in range(len(ops) - 1):
        if ops[i][1] == "getlex" and mv.operand_name(ops[i]) == "PCControllerTypeHandler" \
                and ops[i + 1][1] == "getproperty" and mv.operand_name(ops[i + 1]) == "isGamepad":
            p, _ = mv.body(m)
            a, e = ops[i][0], ops[i + 1][3]
            return p + a, bytes(mv.data[p + a:p + e]), b"\x26" + b"\x02" * (e - a - 1)
    return SKIP


def crosshair_row(mv, base_mv):
    name = "Crosshair_gun.<init>"
    m = mv.method(name)
    if base_mv is not None and mv.shape(m) != base_mv.shape(base_mv.method(name)):
        return SKIP
    ops = mv.ops(m)
    i = sig(ops, ["getlocal0", "pushbyte", "initproperty", "getlocal0", "pushbyte", "initproperty"])
    if i is None or [mv.operand_name(ops[i + 2]), mv.operand_name(ops[i + 5])] != ["_maxGraphicRadius", "_minGraphicRadius"]:
        return SKIP
    half = [k for k, v in enumerate(mv.doubles) if v == 0.5]
    if not half:
        return SKIP
    sx, sy = mv.common_mn("setproperty", "scaleX"), mv.common_mn("setproperty", "scaleY")
    p, _ = mv.body(m)
    a, e = ops[i][0], ops[i + 5][3]
    old = bytes(mv.data[p + a:p + e])
    unit = lambda s: b"\xd0\x2f" + u30(half[0]) + b"\x61" + u30(s)
    new = unit(sx) + unit(sy)
    if len(new) > len(old):
        return SKIP
    return p + a, old, new + b"\x02" * (len(old) - len(new))


def options_back_rows(mv):
    """AbsOptionSectionScreen._BPressed: only DEBUG builds navigated back; now `if (_data.instanceIndex < 100) return;`
    and then the existing DEBUG branch (root._C_BackFromOptionSection()) runs for the VR page. Two rows (12 bytes, then ifne -> iflt)."""
    m = mv.method("AbsOptionSectionScreen._BPressed")
    ops = mv.ops(m)
    i = sig(ops, ["getlocal0", "getproperty", "getproperty", "callpropvoid"])
    j = sig(ops, ["getlex", "getproperty", "getlex", "getproperty", "ifne"])
    if i is None or j is None or mv.operand_name(ops[i + 1]) != "_data" or mv.operand_name(ops[i + 2]) != "instanceIndex" \
            or mv.operand_name(ops[j]) != "CompilationProfile" or mv.operand_name(ops[j + 3]) != "DEBUG":
        return SKIP
    p, _ = mv.body(m)
    a, e = ops[j][0], ops[j + 3][3]
    core = b"\xd0\x66" + u30(ops[i + 1][2][0]) + b"\x66" + u30(ops[i + 2][2][0]) + b"\x24\x64"
    if len(core) > e - a:
        return SKIP
    old = bytes(mv.data[p + a:p + e])
    row1 = (p + a, old, b"\x02" * (e - a - len(core)) + core)
    at = ops[j + 4][0]
    old2 = bytes(mv.data[p + at:p + at + 4])
    assert old2[0] == 0x14
    return row1, (p + at, old2, b"\x15" + old2[1:])


def glyph_rows(movies, base_movies, patched_menu):
    """-> ({movie name: [(kind, offset, old, new)]}, [skipped notes])"""
    rows, notes = {}, []
    for name in sorted(movies):
        mv = Movie(movies[name])
        base_mv = Movie(base_movies[name]) if base_movies is not None and name in base_movies else None
        found = []
        b = button_row(mv)
        if b is SKIP:
            notes.append(f"ui/{name}: getButtonImage shape found more than once; skipped")
        elif b:
            found.append(("button",) + b)
        else:
            notes.append(f"ui/{name}: no getButtonImage method (nothing to retarget)")
        special = []
        if name == "hud.iggy":
            special = [("crosshair", lambda: crosshair_row(mv, base_mv)), ("quick_potion", lambda: quick_potion_row(mv, base_mv))]
        elif name == "quick_access_wheel.iggy":
            special = [("wheel_shortcuts", lambda: gamepad_branch_row(mv, base_mv, "ShortcutsHandlerWheel._updateShortcutsType")),
                       ("wheel_potions", lambda: gamepad_branch_row(mv, base_mv, "PotionsGamePad._onControllerChanged"))]
        elif name == "lib_options.iggy":
            special = [("options_back", lambda: options_back_rows(mv))]
        for kind, fn in special:
            try:
                r = fn()
            except LookupError as ex:
                r = SKIP
                notes.append(f"ui/{name}: {kind}: {ex}")
            if r is SKIP or r is None:
                notes.append(f"ui/{name}: {kind} row skipped: method shape differs from the base build (not equivalent)")
            elif kind == "options_back":
                found.append(("options_back",) + r[0])
                found.append(("options_back_jump",) + r[1])
            else:
                found.append((kind,) + r)
        found.sort(key=lambda r: RANK[r[0]])
        if found:
            rows[name] = found
    return rows, notes


def glyph_header(tgt_name, movies, rows, notes, start_bytes):
    """-> (text, {path: (patched bytes, count)}). Rows chain per movie: each pins the hash before and after itself."""
    arr = lambda v: ",".join("0x%02x" % b for b in v)
    out = [f"// Generated by tools/doto/menu_patches.py for target {tgt_name}; do not edit. Include inside namespace d2vr, in place of the",
           "// Dishonored 2 array, after struct NativeArtMovieEdit. Same row semantics as src/controller_glyph_atlas.hpp: rows of one movie are",
           "// applied in table order, each pinned to the full movie hash before it (a movie with a native menu patch starts from the",
           "// hash after that patch: menus: VR Settings first; hud: crosshair first)."]
    for n in notes:
        out.append("// SKIPPED " + n)
    out.append("inline constexpr NativeArtMovieEdit native_art_movie_edits[]={")
    total = 0
    for name in sorted(rows):
        cur = bytearray(start_bytes[name])
        size = len(cur)
        occupied = set()
        for kind, off, old, new in rows[name]:
            assert len(old) == len(new) <= 12 and cur[off:off + len(old)] == old, (name, kind, off)
            span = set(range(off, off + len(old)))
            assert not occupied & span, (name, kind, "overlap")
            occupied |= span
            before = hashlib.sha256(cur).digest()
            cur[off:off + len(new)] = new
            after = hashlib.sha256(cur).digest()
            pad = lambda v: bytes(v) + b"\x00" * (12 - len(v))
            out.append('{"ui/%s",%d,%d,%d,{%s},{%s},{%s},{%s}},' % (name, size, off, len(old), arr(pad(old)), arr(pad(new)),
                                                                   arr(before), arr(after)))
            total += 1
    out.append("};")
    return "\n".join(out) + "\n", total


# ---------------------------------------------------------------------------------------------------------------- pipeline
def generate(tgt, base_movies=None, keep_exit=False):
    movies = extract_movies(tgt)
    menu_text, patched = build_menu(movies, keep_exit)
    start = {n: patched.get(n, movies[n]) for n in movies}
    rows, notes = glyph_rows(movies, base_movies, patched)
    glyph_text, count = glyph_header(tgt.name, movies, rows, notes, start)
    return dict(movies=movies, menu_text=menu_text, glyph_text=glyph_text, rows=rows, notes=notes, glyph_count=count, patched=patched)


ROW_RE = re.compile(r'\{"([^"]+)",(\d+),(\d+),(\d+),\{([^}]*)\},\{([^}]*)\},\{([^}]*)\},\{([^}]*)\}\}')


def parse_rows(text):
    toint = lambda s: bytes(int(x, 16) for x in s.split(",") if x.strip())
    pad12 = lambda v: v + bytes(12 - len(v)) if len(v) < 12 else v  # a shorter C++ initializer is zero-filled
    return [(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), pad12(toint(m.group(5))), pad12(toint(m.group(6))),
             toint(m.group(7)), toint(m.group(8))) for m in ROW_RE.finditer(text)]


def self_check(base_result):
    ok = True
    shipped = (ROOT / "src" / "menu_patches.hpp").read_text(encoding="utf-8")
    same = shipped.replace("\r\n", "\n") == base_result["menu_text"]
    print(f"self-check menu_patches.hpp: {'IDENTICAL to src/menu_patches.hpp' if same else 'DIFFERS'}")
    ok &= same
    atlas = (ROOT / "src" / "controller_glyph_atlas.hpp").read_text(encoding="utf-8")
    a = atlas.index("native_art_movie_edits[]={")
    shipped_rows = {(r[0], r[2]): r for r in parse_rows(atlas[a:atlas.index("\n};", a)])}
    gen_rows = {(r[0], r[2]): r for r in parse_rows(base_result["glyph_text"])}
    exact = [k for k in shipped_rows if k in gen_rows and shipped_rows[k] == gen_rows[k]]
    wrong = [k for k in shipped_rows if k in gen_rows and shipped_rows[k] != gen_rows[k]]
    missing = [k for k in shipped_rows if k not in gen_rows]
    extra = [k for k in gen_rows if k not in shipped_rows]
    print(f"self-check native_art_movie_edits: {len(shipped_rows)} shipped rows, {len(gen_rows)} generated, "
          f"{len(exact)} identical in every field (offset, bytes, size, both hashes)")
    for k in wrong:
        print("  DIFFERS:", k)
    for k in missing:
        print("  NOT GENERATED:", k)
    for k in extra:
        print("  EXTRA (not shipped):", k)
    for n in base_result["notes"]:
        print("  D2 note:", n)
    return ok and not wrong and not missing and not extra


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="doto", help="port target (tools/doto/targets/<name>.json); base = Dishonored 2")
    ap.add_argument("--check-only", action="store_true", help="generate in memory, write nothing")
    ap.add_argument("--no-self-check", action="store_true")
    ap.add_argument("--replace-exit", action="store_true",
                    help="main menu: D2's edit (the Exit entry becomes VR Settings) instead of repurposing Credits")
    args = ap.parse_args()
    base_t = targets.base()
    base_movies = extract_movies(base_t)
    base_result = None
    if not args.no_self_check or args.target == "base":
        base_result = generate(base_t, None)
    ok = True
    if base_result is not None:
        ok = self_check(base_result)
    if args.target == "base":
        return 0 if ok else 1
    tgt = targets.load(args.target)
    res = generate(tgt, base_movies, keep_exit=not args.replace_exit)
    n_menu = len(re.findall(r'\{"[a-z_]+\.iggy",', res["menu_text"]))
    print(f"{tgt.name}: menu movies {n_menu}, glyph rows {res['glyph_count']} in {len(res['rows'])} movies, skipped {len(res['notes'])}")
    for n in res["notes"]:
        print("  skipped:", n)
    if not args.check_only:
        (ROOT / "src" / f"menu_patches_{tgt.name}.hpp").write_text(res["menu_text"], encoding="utf-8", newline="\n")
        (ROOT / "src" / f"controller_glyph_atlas_{tgt.name}_edits.hpp").write_text(res["glyph_text"], encoding="utf-8", newline="\n")
        print(f"wrote src/menu_patches_{tgt.name}.hpp and src/controller_glyph_atlas_{tgt.name}_edits.hpp")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
