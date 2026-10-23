"""Structure check for docs/native-hand-body-re.md (Plan B task B6, hand-body static RE).

The document gates every later hand-body task, so it must keep its shape:
  * sections "## Q1" .. "## Q8", each with a verdict tag "Verdict: [V]" / "[I]" / "[U]";
  * every RVA in a section (written 0x followed by 5-7 hex digits, not preceded by '+', '-', a word
    character or '.') is pinned in that same section, either by a line "bytes: 0x<rva> <hex bytes>"
    (at least 8 bytes) or by a line naming the RVA together with the word "known" and a cited file
    (src/..., docs/..., tools/... or tests/...). Offsets are written with a leading '+' (world+0x470),
    constants are zero-padded to 8 hex digits (0x0001ffff) and runtime-only globals are written as
    absolute VAs (0x1439e8610), so none of those is taken for an RVA;
  * a "known" citation counts only when a cited file exists and contains the RVA string;
  * a "## Route table" section with rows R1, R2 and R3, whose RVAs are pinned in that section; every RVA in
    any other section after the Q sections (summary, second opinion, ...) is pinned somewhere in the document;
  * a summary table after the last Q section with the columns "Verdict", "blocks route R1", "blocks route R2",
    "blocks route R3" and one row per question Q1..Q8. The first tag of the row's Verdict cell equals the
    section's own "Verdict:" tag, and every weaker tag used inside the section ([I] or [U] under a [V]
    section, [U] under an [I] section) also appears in that cell, so a section with inner [I]/[U] items is
    never summarised as a plain [V].
Exe for the byte comparison: D2VR_EXE when set (then it must be readable and the pinned build, or the
check fails); otherwise the Linux Steam path or the default Windows Steam path, whichever exists. With
none of them present the comparison is not run (the structure checks still are). tools/build-msvc.cmd
does not set D2VR_EXE: there is no fixed Windows install location for the game.
"""
import hashlib
import os
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / 'docs' / 'native-hand-body-re.md'
EXE_SHA = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
DEFAULT_EXES = ('/mnt/steamssd/SteamLibrary/steamapps/common/Dishonored2/Dishonored2.exe',
                r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2\Dishonored2.exe')
RVA_RE = re.compile(r'(?<![+\-\w.])0x([0-9a-fA-F]{5,7})(?![0-9a-fA-F])')
BYTES_RE = re.compile(r'bytes:\s*0x([0-9a-fA-F]{5,7})\b((?:\s+[0-9a-fA-F]{2}){8,})')
VERDICT_RE = re.compile(r'Verdict:\**\s*\[(V|I|U)\]')
CITE_RE = re.compile(r'\b(?:src|docs|tools|tests)/[\w./-]+')
QUESTIONS = ['Q%d' % i for i in range(1, 9)]
TAG_RE = re.compile(r'\[(V|I|U)\]')
STRENGTH = {'V': 2, 'I': 1, 'U': 0}
PINS_BLOCK_RE = re.compile(r'<!-- PINS -->.*?<!-- /PINS -->', re.S)


def sections(text):
    """Split on level-2 headings: {first word of heading: body}."""
    out, order, name, buf = {}, [], None, []
    for line in text.splitlines():
        if line.startswith('## '):
            if name is not None:
                out[name] = '\n'.join(buf)
            name = line[3:].split()[0] if line[3:].split() else ''
            order.append(name)
            buf = [line]
        elif name is not None:
            buf.append(line)
    if name is not None:
        out[name] = '\n'.join(buf)
    return out, order


def cited_text(path):
    file = ROOT / path.rstrip('.')
    return file.read_text(encoding='utf-8', errors='replace').lower() if file.is_file() else None


def known_rvas(body):
    """RVAs on a line with 'known' whose cited file(s) exist and contain the RVA string."""
    known = set()
    for line in body.splitlines():
        if not re.search(r'\bknown\b', line):
            continue
        texts = [t for t in (cited_text(c) for c in CITE_RE.findall(line)) if t is not None]
        for m in RVA_RE.finditer(line):
            rva = m.group(1).lower()
            if any(('0x' + rva) in t for t in texts):
                known.add(rva)
    return known


def unpinned_rvas(body, pinned_elsewhere=frozenset()):
    pinned = {m.group(1).lower() for m in BYTES_RE.finditer(body)} | set(pinned_elsewhere)
    return sorted({m.group(1).lower() for m in RVA_RE.finditer(body)} - pinned - known_rvas(body))


def section_tags(body):
    """(verdict, set of every tag used in the section outside its pin block)."""
    body = PINS_BLOCK_RE.sub('', body)
    verdict = VERDICT_RE.search(body)
    return (verdict.group(1) if verdict else None), set(TAG_RE.findall(body))


def table_rows(body):
    rows = []
    for line in body.splitlines():
        if line.strip().startswith('|'):
            rows.append([c.strip() for c in line.strip().strip('|').split('|')])
    return rows


class PeFile:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        optional = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.sections = []
        for i in range(count):
            at = pe + 24 + optional + 40 * i
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.data, at + 8)
            self.sections.append((va, max(vsize, rsize), rsize, raw))

    def read(self, rva, n):
        for va, vsize, rsize, raw in self.sections:
            if va <= rva < va + vsize:
                if rva + n > va + rsize:
                    return None
                return self.data[raw + rva - va: raw + rva - va + n]
        return None


class HandBodyReDocCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = DOC.read_text(encoding='utf-8') if DOC.exists() else None
        cls.sections, cls.order = sections(cls.text or '')

    def test_document_exists(self):
        self.assertIsNotNone(self.text, '%s is missing' % DOC)

    def test_every_question_has_a_section_and_a_verdict(self):
        self.assertIsNotNone(self.text)
        for q in QUESTIONS:
            self.assertIn(q, self.sections, 'no "## %s" heading' % q)
            self.assertRegex(self.sections[q], VERDICT_RE, '%s has no "Verdict: [V|I|U]" tag' % q)

    def test_every_rva_is_pinned_in_its_section(self):
        self.assertIsNotNone(self.text)
        for q in QUESTIONS:
            missing = unpinned_rvas(self.sections.get(q, ''))
            self.assertEqual(missing, [], '%s cites RVAs without a bytes: line or a "known" citation: %s' % (q, missing))

    def test_route_table_names_r1_r2_r3(self):
        self.assertIsNotNone(self.text)
        self.assertIn('Route', self.sections, 'no "## Route table" section')
        firsts = [r[0] for r in table_rows(self.sections['Route'])]
        for route in ('R1', 'R2', 'R3'):
            self.assertTrue(any(c.startswith(route) for c in firsts), 'route table has no %s row' % route)
        missing = unpinned_rvas(self.sections['Route'])
        self.assertEqual(missing, [], 'Route table cites RVAs without a pin in that section: %s' % missing)

    def test_rvas_after_the_questions_are_pinned_somewhere(self):
        self.assertIsNotNone(self.text)
        everywhere = {m.group(1).lower() for m in BYTES_RE.finditer(self.text)}
        last_q = max(self.order.index(q) for q in QUESTIONS if q in self.order)
        for name in self.order[last_q + 1:]:
            if name == 'Route':
                continue
            missing = unpinned_rvas(self.sections[name], everywhere)
            self.assertEqual(missing, [], 'section %s cites RVAs pinned nowhere in the document: %s' % (name, missing))

    def test_known_citations_name_files_that_contain_the_rva(self):
        self.assertIsNotNone(self.text)
        for line in self.text.splitlines():
            if not re.search(r'\bknown\b', line) or not RVA_RE.search(line) or not CITE_RE.search(line):
                continue
            texts = [t for t in (cited_text(c) for c in CITE_RE.findall(line)) if t is not None]
            self.assertTrue(texts, 'known citation names no existing file: %s' % line.strip())

    def test_summary_table_lists_all_questions_with_route_columns(self):
        self.assertIsNotNone(self.text)
        last_q = max(self.order.index(q) for q in QUESTIONS if q in self.order)
        tail = '\n'.join(self.sections[name] for name in self.order[last_q + 1:])
        rows = table_rows(tail)
        header = next((r for r in rows if 'blocks route R1' in r), None)
        self.assertIsNotNone(header, 'no summary table with a "blocks route R1" column after the Q sections')
        for col in ('Verdict', 'blocks route R2', 'blocks route R3'):
            self.assertIn(col, header)
        verdict_col = header.index('Verdict')
        for q in QUESTIONS:
            row = next((r for r in rows if r and r[0].startswith(q)), None)
            self.assertIsNotNone(row, 'summary table has no %s row' % q)
            self.assertEqual(len(row), len(header), 'summary row %s does not fill every column' % q)
            cell_tags = TAG_RE.findall(row[verdict_col])
            self.assertTrue(cell_tags, 'summary row %s has no verdict tag' % q)
            verdict, used = section_tags(self.sections[q])
            self.assertEqual(cell_tags[0], verdict, 'summary verdict for %s is [%s], the section says [%s]' % (q, cell_tags[0], verdict))
            weaker = sorted(t for t in used if STRENGTH[t] < STRENGTH[verdict])
            hidden = [t for t in weaker if t not in cell_tags]
            self.assertEqual(hidden, [], 'summary row %s hides inner %s items behind [%s]' % (q, hidden, verdict))

    def test_bytes_lines_match_the_pinned_exe(self):
        self.assertIsNotNone(self.text)
        lines = list(BYTES_RE.finditer(self.text))
        self.assertTrue(lines, 'the document has no bytes: lines')
        explicit = os.environ.get('D2VR_EXE')
        if explicit:
            self.assertTrue(os.path.isfile(explicit) and os.access(explicit, os.R_OK), 'D2VR_EXE=%s is not readable' % explicit)
            path = explicit
        else:
            path = next((p for p in DEFAULT_EXES if os.path.isfile(p)), None)
            if path is None:
                print('hand_body_re_doc_check: no Dishonored2.exe at the default paths, byte comparison not run')
                return
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != EXE_SHA:
            self.fail('%s is not the pinned build' % path)
        pe = PeFile(path)
        for m in lines:
            rva = int(m.group(1), 16)
            want = bytes.fromhex(m.group(2).replace(' ', '').replace('\t', ''))
            self.assertEqual(pe.read(rva, len(want)), want, 'bytes: 0x%x does not match the exe' % rva)


if __name__ == '__main__':
    result = unittest.main(exit=False, verbosity=1).result
    sys.exit(0 if result.wasSuccessful() else 1)
