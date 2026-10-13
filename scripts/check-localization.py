#!/usr/bin/env python3
"""QuickSift localization checker (portable, standard library only).

Reports, for the English -> Vietnamese table in src/localization.cpp:
  missing       literal reaching a translating sink (Tr, ShowToast, ShowGlassAlert,
                ShowGlassConfirmation, MakeMenuItem, user-error catalog, marked key
                providers) that is neither an exact key nor fully covered by the
                dynamic-fragment pass
  hardcoded     English literal passed straight to a pre-localized sink
                (ShowGlassAlertText, ShowGlassChoice, ShowToastText, SetWindowTextW)
  concatenated  Tr(L"..." + value) style keys that can never match the table
  identical     Vietnamese text equal to the English key (outside the allowlist)
  placeholder   {token} / printf placeholder sets that differ between EN and VI
  duplicate     the same English key listed twice with different translations
  diacritics    Vietnamese text with no Vietnamese-specific letters although it
                contains several words (a likely ASCII/unaccented translation)

Exit code 0 when clean, 1 when findings exist, 2 on internal error.
`--self-test` runs the checker against built-in synthetic sources.

The export file format (ratings CSV/TXT header, footer, column names) is
intentionally English and is never routed through Tr; see USER_GUIDE.md.

Key providers: a literal that reaches Tr() through a variable (menu tables,
label tables, ternaries) can be marked with a trailing `// loc-key` comment on
its line, or with a `// loc-keys-begin` ... `// loc-keys-end` block. All string
literals in marked regions are treated as translation keys.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TRANSLATING_SINKS = ("Tr", "ShowToast", "ShowGlassAlert", "ShowGlassConfirmation", "MakeMenuItem",
                     "addCause")
PRELOCALIZED_SINKS = ("ShowGlassAlertText", "ShowGlassChoice", "ShowToastText", "SetWindowTextW",
                      # 0.9.4.0 UI polish: framework painters and message boxes take
                      # already-translated text (wrap literals in Tr()).
                      "PaintSectionTitle", "PaintCenteredMutedText", "MessageBoxW")
# (sink, file) pairs allowed to carry English literals: main.cpp reports startup
# failures before the Localizer (and the chosen language) exists.
PRELOCALIZED_EXEMPT = {("MessageBoxW", "src/main.cpp")}
# Calls whose literal arguments are never user-visible text (tokens, log fields).
NON_TEXT_CALLS = ("FillToken", "ReplaceToken", "LogEvent", "WriteEvent", "Write", "Log",
                  "LogFileName", "LogPathId")
# Words that are deliberately identical in Vietnamese (product names, formats, keys).
ALLOWED_WORDS = {
    "quicksift", "xmp", "exif", "iptc", "jpeg", "jpg", "png", "tiff", "tif", "heic", "heif", "webp",
    "raw", "dng", "cr2", "cr3", "nef", "arw", "raf", "orf", "rw2", "csv", "txt", "json", "gpl",
    "ctrl", "shift", "alt", "esc", "enter", "tab", "del", "delete", "space", "f1", "f2", "f5", "f11",
    "am", "pm", "iso", "mm", "ev", "mb", "gb", "kb", "mib", "gib", "ok", "windows", "wic", "codec",
    "sqlite", "url", "http", "https", "www", "github", "com", "dev", "pages", "lightroom", "sidecar",
    "english", "tiếng", "việt", "id", "ui", "gps", "dpi", "srgb", "rgb", "icc", "hdr", "nas", "smb",
    "bmp", "gif", "avif", "jxl", "x", "f", "s", "ms", "px", "vs", "gpu", "cpu", "ram", "d2d", "dxgi",
    "led", "msix", "appx", "zip", "ini", "log", "utc", "pfp", "abc", "e", "g", "i", "ii", "iii", "v", "vi", "en",
    # Established Vietnamese UI terminology keeps these loanwords (see the table).
    "metadata", "hud", "jpegs", "png", "exiv2", "utf",
}
IDENTICAL_ALLOWED = {
    "QuickSift", "English", "Tiếng Việt", "EXIF", "XMP", "JPEG", "RAW", "{shown} / {total}", "OK",
    "QuickSift — Fast Photo Shortlisting",
}
VI_LETTERS = set("ăâđêôơưàáạảãằắặẳẵầấậẩẫèéẹẻẽềếệểễìíịỉĩòóọỏõồốộổỗờớợởỡùúụủũừứựửữỳýỵỷỹ"
                 "ĂÂĐÊÔƠƯÀÁẠẢÃẰẮẶẲẴẦẤẬẨẪÈÉẸẺẼỀẾỆỂỄÌÍỊỈĨÒÓỌỎÕỒỐỘỔỖỜỚỢỞỠÙÚỤỦŨỪỨỰỬỮỲÝỴỶỸ")
PLACEHOLDER = re.compile(r"\{[A-Za-z_][A-Za-z0-9_]*\}|%[-+ #0]*\d*(?:\.\d+)?(?:ls|lu|llu|zu|[sdufxXc])")
WORD = re.compile(r"[^\W\d_][^\W\d_']*")


def ascii_words(value: str) -> list[str]:
    """Whole words made only of ASCII letters (Vietnamese words carry diacritics or
    are whole tokens, so substrings of "không" are never mistaken for English)."""
    return [w for w in WORD.findall(value) if w.isascii()]


def decode_literal(body: str) -> str:
    out = []
    i = 0
    while i < len(body):
        ch = body[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        nxt = body[i + 1] if i + 1 < len(body) else ""
        if nxt == "u":
            out.append(chr(int(body[i + 2:i + 6], 16)))
            i += 6
        elif nxt == "U":
            out.append(chr(int(body[i + 2:i + 10], 16)))
            i += 10
        elif nxt == "x":
            m = re.match(r"[0-9A-Fa-f]+", body[i + 2:])
            out.append(chr(int(m.group(0), 16)) if m else "x")
            i += 2 + (len(m.group(0)) if m else 0)
        else:
            out.append({"n": "\n", "r": "\r", "t": "\t", "0": "\0"}.get(nxt, nxt))
            i += 2
    return "".join(out)


def blank_comments(text: str) -> tuple[str, list[str]]:
    """Returns (code with comments replaced by spaces, per-line comment text)."""
    out = []
    comments: dict[int, str] = {}
    i, n, line = 0, len(text), 0
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            out.append(ch)
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments[line] = comments.get(line, "") + text[i:j]
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            chunk = text[i:j]
            comments[line] = comments.get(line, "") + chunk
            out.append("".join(c if c == "\n" else " " for c in chunk))
            line += chunk.count("\n")
            i = j
        elif ch == "R" and text.startswith('R"', i) or text.startswith('LR"', i):
            start = i + (2 if text[i] == "R" else 3)
            m = re.match(r'([^(]*)\(', text[start:])
            if not m:
                out.append(ch)
                i += 1
                continue
            end = text.find(")" + m.group(1) + '"', start)
            end = n if end < 0 else end + len(m.group(1)) + 2
            chunk = text[i:end]
            out.append(chunk)
            line += chunk.count("\n")
            i = end
        elif ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch:
                j += 2 if text[j] == "\\" else 1
            chunk = text[i:j + 1]
            out.append(chunk)
            i = j + 1
        else:
            out.append(ch)
            i += 1
    lines = ["" for _ in range(line + 1)]
    for k, v in comments.items():
        if k < len(lines):
            lines[k] = v
    return "".join(out), lines


LITERAL = re.compile(r'(?<![A-Za-z0-9_])(L|u8|u|U)?"((?:[^"\\\n]|\\.)*)"')


def literal_groups(code: str, wide_only: bool = False) -> list[tuple[int, int, str]]:
    """Adjacent string literals are concatenated, as the compiler does."""
    groups: list[tuple[int, int, str]] = []
    for m in LITERAL.finditer(code):
        if wide_only and m.group(1) != "L":
            continue
        value = decode_literal(m.group(2))
        if groups and code[groups[-1][1]:m.start()].strip() == "":
            s, _, v = groups[-1]
            groups[-1] = (s, m.end(), v + value)
        else:
            groups.append((m.start(), m.end(), value))
    return groups


def matching_paren(code: str, open_index: int) -> int:
    depth = 0
    i = open_index
    while i < len(code):
        ch = code[i]
        if ch in "\"'":
            j = i + 1
            while j < len(code) and code[j] != ch:
                j += 2 if code[j] == "\\" else 1
            i = j + 1
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def call_spans(code: str, names: tuple[str, ...]):
    pattern = re.compile(r"(?<![A-Za-z0-9_:])(?:[A-Za-z_][A-Za-z0-9_]*(?:\.|->))?(" +
                         "|".join(names) + r")\s*\(")
    for m in pattern.finditer(code):
        prefix = code[max(0, m.start() - 40):m.start()]
        # Skip declarations/definitions: "void ShowToast(const std::wstring& ..."
        if re.search(r"(void|std::wstring|auto|bool)\s*(QuickSiftApplicationImpl::)?\s*$", prefix):
            continue
        open_index = m.end() - 1
        close = matching_paren(code, open_index)
        if close < 0:
            continue
        yield m.group(1), open_index + 1, close


def mask_calls(code: str, start: int, end: int, names: tuple[str, ...]) -> list[tuple[int, int]]:
    spans = []
    for _, s, e in call_spans(code[start:end], names):
        spans.append((start + s, start + e))
    return spans


def has_text(value: str) -> bool:
    stripped = PLACEHOLDER.sub(" ", value)
    return any(len(w) >= 2 for w in WORD.findall(stripped))


def english_residue(value: str) -> list[str]:
    stripped = PLACEHOLDER.sub(" ", value)
    stripped = re.sub(r"https?://\S+", " ", stripped)
    return [w for w in ascii_words(stripped) if w.lower() not in ALLOWED_WORDS and len(w) >= 2]


def parse_table(text: str, name: str) -> list[tuple[str, str, int]]:
    start = text.find(name)
    if start < 0:
        raise RuntimeError(f"table {name} not found")
    brace = text.find("{", start)
    depth, i = 0, brace
    code, _ = blank_comments(text)
    while i < len(code):
        if code[i] == '"':
            j = i + 1
            while code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            i = j + 1
            continue
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = code[brace:i]
    pairs = []
    for m in re.finditer(r"TranslationPair\s*\{", body):
        close = body.find("}", m.end())
        inner = body[m.end():close]
        # The closing brace search must skip braces inside literals.
        groups = literal_groups(inner)
        while len(groups) < 2 or not inner.rstrip().endswith('"'):
            close = body.find("}", close + 1)
            if close < 0:
                break
            inner = body[m.end():close]
            groups = literal_groups(inner)
        if len(groups) >= 2:
            line = text[:brace + m.start()].count("\n") + 1
            pairs.append((groups[0][2], groups[1][2], line))
    return pairs


class Translator:
    def __init__(self, exact: list[tuple[str, str, int]], fragments: list[tuple[str, str, int]]):
        # Mirrors Localizer::Text: exact keys from both tables first, then the
        # fragment pass with the longest English fragment first.
        self.exact = {}
        for en, vi, _ in list(exact) + list(fragments):
            self.exact.setdefault(en, vi)
        self.fragments = sorted(fragments, key=lambda row: -len(row[0]))

    def translate(self, english: str) -> str | None:
        if english in self.exact:
            return self.exact[english]
        value = english
        for en, vi, _ in self.fragments:
            if en:
                value = value.replace(en, vi)
        return value

    def covered(self, english: str) -> bool:
        if english in self.exact:
            return True
        translated = self.translate(english) or english
        # English words that survived the fragment pass unchanged. (Comparing with
        # the source words avoids flagging unaccented Vietnamese such as "sau".)
        source_words = {w.lower() for w in english_residue(english)}
        return not [w for w in english_residue(translated) if w.lower() in source_words]


class Finding:
    def __init__(self, kind: str, where: str, text: str):
        self.kind, self.where, self.text = kind, where, text

    def __str__(self) -> str:
        return f"[{self.kind}] {self.where}: {self.text}"


def scan_sources(files: dict[str, str], translator: Translator) -> tuple[list[Finding], set[str]]:
    findings: list[Finding] = []
    keys: set[str] = set()

    def where(path: str, code: str, index: int) -> str:
        return f"{path}:{code.count(chr(10), 0, index) + 1}"

    for path, raw in sorted(files.items()):
        if path.endswith("localization.cpp"):
            continue
        code, comments = blank_comments(raw)
        lines_code = code.split("\n")
        # Marked key providers.
        in_block = False
        offset = 0
        for number, line in enumerate(lines_code):
            comment = comments[number] if number < len(comments) else ""
            if "loc-keys-begin" in comment:
                in_block = True
            marked = in_block or "loc-key" in comment.replace("loc-keys-begin", "").replace("loc-keys-end", "")
            if "loc-keys-end" in comment:
                in_block = False
            if marked:
                # Wide literals only: narrow strings in marked tables are file-format
                # tokens (ratings export), which stay English by design.
                for _, _, value in literal_groups(line, wide_only=True):
                    if has_text(value):
                        keys.add(value)
                        if not translator.covered(value):
                            findings.append(Finding("missing", f"{path}:{number + 1}", value))
            offset += len(line) + 1

        # Result details are shown through Tr(result.detail) / ShowGlassAlert, so
        # literals assigned to a `detail` field are translation keys (sentence
        # fragments, since details are often extended with sizes or paths).
        for m in re.finditer(r"(?<![A-Za-z0-9_])(?:[A-Za-z_][A-Za-z0-9_]*(?:\.|->))?(?:detail|initialDetail)"
                             r"\s*(?:=|\+=)\s*", code):
            stop = code.find(";", m.end())
            line_no = code.count("\n", 0, m.start())
            if any(0 <= n < len(comments) and "loc-composed" in comments[n] for n in (line_no, line_no - 1)):
                continue  # composed sentence; every full variant is in the fragment table
            segment = code[m.end():stop]
            for s0, _, value in literal_groups(segment):
                if re.search(r"[!=]=\s*$", segment[:s0]):
                    continue  # compared sentinel, not displayed text
                if has_text(value):
                    keys.add(value)
                    if not translator.covered(value):
                        findings.append(Finding("missing", where(path, code, m.end() + s0), value))

        # Streamed status text: Tr(stream.str()) localizes whatever was streamed in,
        # through the fragment pass, so streamed literals must be covered.
        for m in re.finditer(r"(?<![A-Za-z0-9_])Tr\(\s*([A-Za-z_][A-Za-z0-9_]*)\.str\(\)\s*\)", code):
            ident = m.group(1)
            window_start = max(0, m.start() - 6000)
            for chain in re.finditer(r"(?<![A-Za-z0-9_.])" + re.escape(ident) + r"\s*<<", code[window_start:m.start()]):
                begin = window_start + chain.end()
                stop = code.find(";", begin)
                segment = code[begin:stop]
                for s0, _, value in literal_groups(segment):
                    if re.search(r"\?\s*$|:\s*$", segment[:s0]) and len(value) <= 1:
                        continue
                    if has_text(value):
                        keys.add(value)
                        if not translator.covered(value):
                            findings.append(Finding("missing", where(path, code, begin + s0), value))

        # Local key providers: Tr(name) / Tr(name[i]) where `name` is initialized
        # with literals shortly before (label tables, ternaries).
        for m in re.finditer(r"(?<![A-Za-z0-9_])Tr\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*[\[)]", code):
            ident = m.group(1)
            window_start = max(0, code.rfind("\n", 0, max(0, m.start() - 6000)))
            window = code[window_start:m.start()]
            decl = None
            for d in re.finditer(r"(?<![A-Za-z0-9_.>])" + re.escape(ident) +
                                 r"\s*(?:\[\s*\d*\s*\])?\s*(?:=|\{)", window):
                decl = d
            if not decl:
                continue
            init_start = window_start + decl.end()
            init_end = code.find(";", init_start)
            if init_end < 0 or init_end > m.start():
                continue
            for s0, _, value in literal_groups(code[init_start:init_end]):
                if has_text(value):
                    keys.add(value)
                    if not translator.covered(value):
                        findings.append(Finding("missing", where(path, code, init_start + s0), value))

        for name, start, end in call_spans(code, TRANSLATING_SINKS + PRELOCALIZED_SINKS):
            masked = mask_calls(code, start, end, NON_TEXT_CALLS)
            translating = name in TRANSLATING_SINKS
            nested_tr = [] if translating else mask_calls(code, start, end, TRANSLATING_SINKS + (
                "UserErrorText", "UserErrorTitle", "MetadataFieldLabel", "Text"))
            args = code[start:end]
            if name == "Tr" and re.search(r'L"(?:[^"\\]|\\.)*"\s*\+|\+\s*L"', args) and \
                    not re.search(r"\?", args):
                findings.append(Finding("concatenated", where(path, code, start), args.strip()[:120]))
                continue
            for s, e, value in literal_groups(args):
                absolute = start + s
                if any(a <= absolute < b for a, b in masked):
                    # Only the token/name arguments are masked; the first argument of
                    # FillToken/ReplaceToken may itself be a key wrapped in Tr().
                    continue
                if not has_text(value):
                    continue
                if translating:
                    keys.add(value)
                    if not translator.covered(value):
                        findings.append(Finding("missing", where(path, code, absolute), value))
                elif not any(a <= absolute < b for a, b in nested_tr):
                    if (name, path) in PRELOCALIZED_EXEMPT:
                        continue
                    if english_residue(value):
                        findings.append(Finding("hardcoded", where(path, code, absolute),
                                                f"{name}: {value}"))
    return findings, keys


def check_table(exact, fragments) -> list[Finding]:
    findings: list[Finding] = []
    seen: dict[str, tuple[str, int]] = {}
    for table, rows in (("kTranslations", exact), ("kDynamicFragments", fragments)):
        for en, vi, line in rows:
            where = f"src/localization.cpp:{line}"
            if table == "kTranslations":
                if en in seen and seen[en][0] != vi:
                    findings.append(Finding("duplicate", where, f"{en!r} (first at line {seen[en][1]})"))
                seen.setdefault(en, (vi, line))
            if sorted(PLACEHOLDER.findall(en)) != sorted(PLACEHOLDER.findall(vi)):
                findings.append(Finding("placeholder", where,
                                        f"{en!r}: EN {sorted(PLACEHOLDER.findall(en))} VI {sorted(PLACEHOLDER.findall(vi))}"))
            if en == vi and en.strip() not in IDENTICAL_ALLOWED and english_residue(en):
                findings.append(Finding("identical", where, en))
            elif table == "kTranslations" and en != vi and len(english_residue(en)) >= 3 and \
                    not any(c in VI_LETTERS for c in vi):
                findings.append(Finding("diacritics", where, vi))
    return findings


def run(root: Path, extra_files: dict[str, str] | None = None, quiet: bool = False) -> int:
    localization = (root / "src/localization.cpp").read_text(encoding="utf-8")
    exact = parse_table(localization, "kTranslations{")
    fragments = parse_table(localization, "kDynamicFragments{")
    translator = Translator(exact, fragments)
    files = extra_files if extra_files is not None else {
        str(p.relative_to(root)).replace("\\", "/"): p.read_text(encoding="utf-8", errors="replace")
        for p in sorted((root / "src").rglob("*")) if p.suffix in (".cpp", ".h")
    }
    findings = check_table(exact, fragments)
    source_findings, keys = scan_sources(files, translator)
    findings += source_findings
    # User-error catalog keys reach Tr() through UserErrorText/UserErrorTitle.
    catalog = root / "src/core/user_error_catalog.cpp"
    if extra_files is None and catalog.exists():
        code, _ = blank_comments(catalog.read_text(encoding="utf-8"))
        for s, _, value in literal_groups(code):
            if has_text(value) and not re.fullmatch(r"[a-z_./]+(\.h)?", value):
                keys.add(value)
                if not translator.covered(value):
                    findings.append(Finding("missing", f"src/core/user_error_catalog.cpp:{code.count(chr(10), 0, s) + 1}", value))
    unique = []
    seen = set()
    for f in findings:
        key = (f.kind, f.text)
        if key not in seen:
            seen.add(key)
            unique.append(f)
    counts: dict[str, int] = {}
    for f in unique:
        counts[f.kind] = counts.get(f.kind, 0) + 1
    if not quiet:
        for f in unique:
            print(f)
        summary = ", ".join(f"{k}={v}" for k, v in sorted(counts.items())) or "no findings"
        print(f"QuickSift localization check: {len(exact)} exact keys, {len(fragments)} fragments, "
              f"{len(keys)} source keys; {summary}.")
    return 1 if unique else 0


def self_test() -> int:
    import tempfile
    table = '''
const std::initializer_list<TranslationPair> kTranslations{
    TranslationPair{L"Open folder", L"Mở thư mục"},
    TranslationPair{L"Saved {count} files", L"Đã lưu {count} tệp"},
    TranslationPair{L"Broken {count}", L"Hỏng {total}"},
    TranslationPair{L"Same text here", L"Same text here"},
    TranslationPair{L"Retry", L"Thử lại"},
};
constexpr std::array kDynamicFragments{
    TranslationPair{L" photos", L" ảnh"},
};
'''
    sources = {
        "src/a.cpp": '''
void F() {
    ShowToast(L"Open folder");
    ShowToast(L"Unknown words here");
    auto t = Tr(L"Prefix " + name);
    ShowGlassAlertText(Tr(L"Retry"), L"Hard coded English", GlassAlertKind::Info);
    ShowGlassChoice(Tr(L"Retry"), FillToken(Tr(L"Saved {count} files"), L"{count}", n), Tr(L"Retry"), a, Tr(L"Retry"), k);
    SetWindowTextW(hwnd, L"");
    const wchar_t* key = L"Not in table"; // loc-key
    // Tr(L"Commented out should be ignored")
    text = Tr(L"12 photos");
}
''',
    }
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "src").mkdir()
        (root / "src/localization.cpp").write_text(table, encoding="utf-8")
        exact = parse_table(table, "kTranslations{")
        fragments = parse_table(table, "kDynamicFragments{")
        translator = Translator(exact, fragments)
        findings = check_table(exact, fragments)
        src_findings, _ = scan_sources(sources, translator)
        findings += src_findings
        kinds = sorted((f.kind, f.text) for f in findings)
        expected = {
            ("placeholder", "'Broken {count}': EN ['{count}'] VI ['{total}']"),
            ("identical", "Same text here"),
            ("missing", "Unknown words here"),
            ("missing", "Not in table"),
            ("hardcoded", "ShowGlassAlertText: Hard coded English"),
        }
        got = set(kinds)
        ok = expected.issubset(got)
        concatenated = any(k == "concatenated" for k, _ in kinds)
        unexpected = [k for k in kinds if k not in expected and k[0] != "concatenated"]
        commented = any("Commented" in t for _, t in kinds)
        if not ok or not concatenated or unexpected or commented:
            print("self-test FAILED")
            print("got:", kinds)
            return 1
        if run(root, extra_files={"src/b.cpp": 'void G(){ ShowToast(L"Open folder"); }'}, quiet=True) != 1:
            print("self-test FAILED: table findings should fail the run")
            return 1
    print("check-localization self-test passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            return self_test()
        return run(args.source_dir.resolve())
    except Exception as error:  # pragma: no cover - reported, not raised
        print(f"check-localization internal error: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
