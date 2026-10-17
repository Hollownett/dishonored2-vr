"""Compile-check-only source rewrites for two clang-cl 23 limitations (never run).

cl accepts both of these constructs; clang-cl does not, and has no flag for either.
The harness retries a failed compile on a rewritten COPY of the sources and, if that
passes, reports "PASS (compat)"; it never executes anything built from the copy,
because the rewrites change semantics (they are only sound for "does it compile?").

1. Nested-struct default member initializers (NSDMIs). A class that uses a nested
   struct with NSDMIs before the enclosing class is complete, e.g.

       class A { struct Entry { int x{}; };  inline static std::array<Entry,16> e_{}; };

   fails with "default member initializer for 'x' needed within definition of
   enclosing class 'A' outside of member functions" (llvm/llvm-project#36684 family).
   strip_nsdmis() removes member initialisers that sit directly in a class body, so
   value-initialisation (`T{}`) still works but default-initialisation (`T t;`)
   no longer zero-fills.

2. `static alignas(16) T x;` (alignas after a storage-class keyword) is rejected with
   "'alignas' attribute cannot be applied to types"; fix_alignas_order() moves the
   alignas in front, which is the same declaration.

3. Nested `__try { } __finally { __try { } __finally { } }` makes clang's SEH
   codegen segfault (GenerateSEHFinallyFunction recursion). flatten_nested_finally()
   turns the inner try/finally into two plain blocks.
"""
import re

_CLASS_HEAD = re.compile(r"\b(?:struct|class|union)\b[^;(){}=]*$")
_ENUM_HEAD = re.compile(r"\benum\b[^;(){}=]*$")
_NSDMI = re.compile(
    r"(?P<name>\b[A-Za-z_]\w*(?:\s*\[[^\]\[;{}]*\])*)"
    r"\s*(?P<init>\{[^{};]*\})(?P<tail>\s*[;,])")


def _scan(text):
    """Yield (index, char) for characters that are code (not comment/string/char)."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == "R" and text.startswith('R"', i):
            m = re.compile(r'R"([^()\\ ]{0,16})\(').match(text, i)
            if m:
                end = text.find(")" + m.group(1) + '"', m.end())
                i = n if end < 0 else end + len(m.group(1)) + 2
                continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        yield i, c
        i += 1


def strip_nsdmis(text):
    """Return (text with class-body-level member initialisers removed, count)."""
    matches = list(_NSDMI.finditer(text))
    stack = []          # per open brace: True when it is a class/struct/union body
    seg_start = 0       # start of the current declaration (after ; { })
    edits, mi = [], 0
    code = set()
    for i, c in _scan(text):
        code.add(i)
        while mi < len(matches) and matches[mi].start("name") <= i:
            m, mi = matches[mi], mi + 1
            if m.start("name") in code and stack and stack[-1] and "return" not in m.group("init"):
                decl = text[seg_start:m.start("name")]
                before = text[max(0, m.start("name") - 12):m.start("name")].rstrip()
                name = re.match(r"\w+", m.group("name")).group(0)
                if name in ("noexcept", "const", "override", "final", "try", "else"):
                    # keywords: only a function-pointer member's trailing noexcept
                    # or a data member that happens to be called "final" qualify
                    ok = ((name == "noexcept" and re.search(r"\(\s*\*\s*\w+\s*\)", decl)) or
                          (name == "final" and ")" not in decl))
                    if not ok:
                        continue
                is_const_object = re.search(r"\bconst\b", decl) and "*" not in decl
                if (not before.endswith(("return", "=", "(", "case", "<", ":")) and
                        not re.search(r"\b(?:static|constexpr|typedef|using|enum)\b", decl) and not is_const_object):
                    edits.append((m.start("init"), m.end("init")))
        if c == "{":
            head = text[seg_start:i]
            stack.append(bool(_CLASS_HEAD.search(head)) and not _ENUM_HEAD.search(head))
            seg_start = i + 1
        elif c == "}":
            if stack:
                stack.pop()
            seg_start = i + 1
        elif c == ";":
            seg_start = i + 1
    out, last = [], 0
    for b, e in edits:
        out.append(text[last:b])
        last = e
    out.append(text[last:])
    return "".join(out), len(edits)


_NESTED_FINALLY = re.compile(
    r"__finally(\s*)\{(\s*)__try\s*\{([^{}]*)\}\s*__finally\s*\{([^{}]*)\}(\s*)\}")


def flatten_nested_finally(text):
    return _NESTED_FINALLY.subn(lambda m: "__finally%s{%s{%s}{%s}%s}" % (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)), text)


_ALIGNAS_AFTER_STORAGE = re.compile(r"\b(static|inline|extern|constexpr|thread_local)\s+(alignas\s*\([^()]*\))")


def fix_alignas_order(text):
    return _ALIGNAS_AFTER_STORAGE.subn(lambda m: "%s %s" % (m.group(2), m.group(1)), text)


def transform(text):
    """Apply every rewrite. Returns (new text, number of edits)."""
    text, n1 = strip_nsdmis(text)
    text, n2 = flatten_nested_finally(text)
    text, n3 = fix_alignas_order(text)
    return text, n1 + n2 + n3
