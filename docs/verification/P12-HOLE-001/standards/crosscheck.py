#!/usr/bin/env python3
"""P12-HOLE-001: checks BetterCAD's standards tables against published copies.

The standards data of holes is transcribed, so it is checked against the
published sources themselves rather than against BetterCAD's own code:

    published source  ->  crosscheck.py  ->  BetterCAD's table (src/)
                                         ->  the tests' reference table

The C++ tests compare BetterCAD's tables with reference tables transcribed
from the sources; this script checks both transcriptions against the sources
they name.

The sources are not redistributed here (see SOURCES.md for what they are and
where they come from). Run

    python crosscheck.py --fetch

to download them into ./sources (each file's SHA-256 is checked against
SOURCES.md), then

    python crosscheck.py

to compare. The PDF previews are read with pdftotext -table (xpdf or
poppler). Every value is compared as a whole number of micrometres, or of
tenths of a micrometre where the standard tabulates them (IT1 to IT4).
"""

from __future__ import annotations

import argparse
import hashlib
import html
import math
import re
import subprocess
import sys
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SOURCES = HERE / "sources"

# name -> (url, sha256). SOURCES.md describes each one.
DOWNLOADS = {
    "it_grade.wiki": (
        "https://en.wikipedia.org/w/index.php?title=IT_Grade&action=raw",
        "9211cf722badd09b5a9b831a3a0122d59e0865feadcd41a77f0196304afa4080",
    ),
    "iso286-2-2010-sample.pdf": (
        "https://cdn.standards.iteh.ai/samples/54915/7d6147c29fed4e31af36b60561e88752/ISO-286-2-2010.pdf",
        "b96cbd437dd86ea46584e5c4f1f95e736ac941c417620a5ce0e998cd3fc16e2b",
    ),
    "iso965-1-2013-sample.pdf": (
        "https://cdn.standards.iteh.ai/samples/57778/765c17fc0f7946ff850db22b43920f09/ISO-965-1-2013.pdf",
        "fef2802aebccef9de45742a53db3b00de5d983332e3bd8dba7c98a50245b2280",
    ),
    "iso965-2-1998-sample.pdf": (
        "https://cdn.standards.iteh.ai/samples/23594/74d3eec3e0d04d999a1680f11b4f04ec/ISO-965-2-1998.pdf",
        "76949e20668cd1601bb841f05e0ec1e25ecf8ad4b309fca62d35edef639ea13c",
    ),
    "iso965-2-2024-sample.pdf": (
        "https://cdn.standards.iteh.ai/samples/87890/690c4af45f494dbea186bd921fa9132e/ISO-965-2-2024.pdf",
        "9dc79f4bc84aa25968b9a2a812c32ff2ac9a62aa136ec53d20f5ca501b6a9388",
    ),
    "misumi_holefits.pdf": (
        "https://us.misumi-ec.com/pdf/press/us_12e_pr1263.pdf",
        "de6d42b9d1ee202d384999c590eefa2179e193d1b6b73eb0d2aef26ce3f2c5ba",
    ),
    "engineeringhardware_clearance.html": (
        "https://engineeringhardware.com/fastener/clearance-hole-sizes/",
        "a6cb22692097f7af7104ec0cd3c0eac317277fb93402bc026f4ab3dadcb60845",
    ),
    "de_durchgangsbohrung.wiki": (
        "https://de.wikipedia.org/w/index.php?title=Durchgangsbohrung&action=raw",
        "8cd54563280f16eae69e07e2914b2b77403b2f22b6517e5a1deb0b633adb442c",
    ),
}

FAILURES: list[str] = []
CHECKS = 0


def report(name: str, ok: bool, detail: str) -> None:
    global CHECKS
    CHECKS += 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
    if not ok:
        FAILURES.append(name)


def fetch() -> None:
    SOURCES.mkdir(exist_ok=True)
    for name, (url, digest) in DOWNLOADS.items():
        path = SOURCES / name
        if not path.exists():
            request = urllib.request.Request(url, headers={"User-Agent": "BetterCAD-verification"})
            with urllib.request.urlopen(request, timeout=120) as response:
                path.write_bytes(response.read())
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        print(f"{'ok  ' if actual == digest else 'DIFF'} {name} {actual}")
        if actual != digest:
            FAILURES.append(f"download {name}")


def source(name: str) -> str:
    """The text of a source: PDFs through pdftotext -table."""
    path = SOURCES / name
    if not path.exists():
        sys.exit(f"{path} is missing; run crosscheck.py --fetch first")
    if path.suffix != ".pdf":
        return path.read_text(encoding="utf-8", errors="replace")
    text = path.with_suffix(".txt")
    if not text.exists():
        subprocess.run(["pdftotext", "-q", "-table", str(path), str(text)], check=True)
    return text.read_text(encoding="utf-8", errors="replace")


# ---------------------------------------------------------------------------
# BetterCAD's own tables and the tests' reference tables
# ---------------------------------------------------------------------------

def cpp(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def block(text: str, name: str, braces: str = "{{") -> str:
    """The initializer of a table, by its variable's name."""
    start = text.index(name + braces)
    return text[start:text.index("};" if braces == "{" else "}};", start)]


def numbers(text: str) -> list[int]:
    return [int(n) for n in re.findall(r"-?\d+", text)]


def rows(text: str, pattern: str) -> list[tuple[str, ...]]:
    return re.findall(pattern, text)


IMPL_TOLERANCES = cpp("src/core/standards/HoleTolerances.cpp")
IMPL_THREADS = cpp("src/core/standards/MetricThreads.cpp")
IMPL_CLEARANCE = cpp("src/core/standards/ClearanceHoles.cpp")
TESTS = cpp("tests/core/standards/HoleStandardsTests.cpp")

# BetterCAD lists the standard tolerances by grade, the sources by size
# range: IT1 to IT18 of each range, in tenths of a micrometre.
IMPL_IT_BY_GRADE = [numbers(row) for row in block(IMPL_TOLERANCES, "kStandardTolerances").split("{")[2:]]
IMPL_IT_BY_GRADE = [row for row in IMPL_IT_BY_GRADE if len(row) == 13]
IMPL_IT = [list(row) for row in zip(*IMPL_IT_BY_GRADE)]
# The lower limit deviations of D, E, F and G, in micrometres.
IMPL_EI = [numbers(row) for row in block(IMPL_TOLERANCES, "kFundamentalDeviations").split("{")[2:]]
IMPL_EI = [row for row in IMPL_EI if len(row) == 13]
# pitch -> (EI of position G, TD1), micrometres.
IMPL_PITCHES = {int(p): (int(g), int(td1))
                for p, g, td1 in rows(block(IMPL_THREADS, "kPitches"), r"\{(\d+), (\d+), (\d+)\}")}
# (diameter range limit, pitch) -> TD2, micrometres.
IMPL_TD2 = {(int(r), int(p)): int(t)
            for r, p, t in rows(block(IMPL_THREADS, "kPitchDiameterTolerances"), r"\{(\d+), (\d+), (\d+)\}")}
# The sizes, in micrometres.
IMPL_SIZES = [(int(d), int(p), kind == "coarse")
              for kind, d, p in rows(block(IMPL_THREADS, "kSizes"), r"(coarse|fine)\((\d+), (\d+)\)")]
# Clearance holes in tenths of a millimetre.
IMPL_CLEARANCE_ROWS = {int(d): (int(a), int(b), int(c)) for d, a, b, c in
                       rows(block(IMPL_CLEARANCE, "kClearanceHoles"), r"\{(\d+), \{(\d+), (\d+), (\d+)\}\}")}

TEST_IT = [[int(v) for v in values.split(",")] for _, values in
           rows(block(TESTS, "kStandardTolerances"), r"\{(\d+), \{([\d, ]+)\}\}")]
TEST_CLEARANCE = {row[0]: tuple(int(n) for n in row[1:])
                  for row in rows(block(TESTS, "kClearanceHoles"), r'\{"(M[\d.]+)", \{(\d+), (\d+), (\d+)\}\}')}
TEST_THREADS = {row[0]: tuple(int(n) for n in row[1:])
                for row in rows(block(TESTS, "kThreadLimits"), r'\{"(M[\dx.]+)", (\d+), (\d+), (\d+), (\d+)\}')}
TEST_DEVIATIONS = [numbers(row) for row in block(TESTS, "kLimitDeviations").split("\n") if numbers(row)]
TEST_DE = [numbers(row) for row in block(TESTS, "kLimitDeviationsDE").split("\n") if numbers(row)]
TEST_EXTERNAL = {int(p): int(v) for p, v in rows(block(TESTS, "kExternalDeviations", "{"), r"\{(\d+), (\d+)\}")}


# ---------------------------------------------------------------------------
# The sources
# ---------------------------------------------------------------------------

# Text the watermark of a published preview leaves on a line it crosses.
NOISE = ("iTeh", "standards", "ISO", "catalog", "http", "PREVIEW", "STANDARD", "Document", "Preview")

# The upper limits of the size ranges of ISO 286, in mm.
IT_RANGES = [3, 6, 10, 18, 30, 50, 80, 120, 180, 250, 315, 400, 500]


NUMBER = "-|\\d+(?: \\d{3})+|\\d+"


def readable(line: str) -> bool:
    return not any(n in line for n in NOISE)


def section(text: str, start: str, end: str | None = None) -> str:
    """The text from the first match of @p start to the first match of @p end
    after it (or to the end). The previews print table titles with irregular
    spacing, so both are patterns."""
    first = re.search(start, text)
    if first is None:
        raise ValueError(f"{start} is not in the source")
    if end is None:
        return text[first.start():]
    last = re.search(end, text[first.end():])
    return text[first.start():] if last is None else text[first.start():first.end() + last.start()]


def contiguous(values: list[int], wanted: list[int]) -> bool:
    """Whether @p wanted appears in @p values as a run, in order."""
    return any(values[i:i + len(wanted)] == wanted for i in range(len(values) - len(wanted) + 1))


def wikipedia_it() -> dict[int, list[int]]:
    """Size range (its upper limit, mm) -> IT1 to IT18, tenths of a
    micrometre, from the Wikipedia article "IT Grade"."""
    text = source("it_grade.wiki")
    table = text[text.index("ISO 286 - Table 1"):]
    table = table[:table.index("|}")]
    out = {}
    for chunk in table.split("\n|-")[1:]:
        cells = [c.replace(",", "") for c in re.findall(r"\{\{decimal cell\|([\d.,]+)\}\}", chunk)]
        if len(cells) < 20 or float(cells[1]) > 500:
            continue
        values = [float(v) for v in cells[2:15]] + [float(v) * 1000 for v in cells[15:]]
        out[int(float(cells[1]))] = [round(v * 10) for v in values[2:]]
    return out


def iso286_table1() -> dict[int, list[int]]:
    """The same table as ISO 286-2:2010 prints it. Lines its watermark
    crosses are left out."""
    text = source("iso286-2-2010-sample.pdf")
    body = section(text, r"Table\s+1\s+--", r"Figure 3")
    pairs = list(zip([0] + IT_RANGES[:-1], IT_RANGES))
    out = {}
    for line in body.splitlines():
        if not readable(line):
            continue
        values = []
        for token in line.split():
            if token == "--":
                values.append(0.0)
            elif re.fullmatch(r"\d+(,\d+)?", token):
                values.append(float(token.replace(",", ".")))
            else:
                values = []
                break
        if len(values) != 22 or (int(values[0]), int(values[1])) not in pairs:
            continue
        micro = values[2:15] + [v * 1000 for v in values[15:]]
        out[int(values[1])] = [round(v * 10) for v in micro[2:]]
    return out


def iso286_table3() -> dict[int, list[int]]:
    """Size range -> the upper deviations of D6 to D13, the lower one of D,
    the upper deviations of E5 to E10 and the lower one of E, in
    micrometres. Lines the watermark crosses are left out."""
    text = source("iso286-2-2010-sample.pdf")
    body = section(text, r"Table\s+3\s+--", r"a  The intermediate fundamental deviation")
    out = {}
    pending = None
    for line in body.splitlines():
        if not readable(line):
            pending = None
            continue
        values = [int(v.replace(" ", "")) for v in re.findall(r"\+(\d[\d ]*\d|\d)", line)]
        if not values:
            continue
        head = re.match(r"\s*(--|\d[\d ]*\d|\d)\s+(\d[\d ]*\d|\d)\s", line)
        if head and len(values) >= 14:
            pending = (int(head.group(2).replace(" ", "")), values[-14:])
        elif pending is not None and len(values) >= 14:
            limit, upper = pending
            lower = values[-14:]
            if limit in IT_RANGES:
                out[limit] = upper[:8] + [lower[0]] + upper[8:] + [lower[8]]
            pending = None
        else:
            pending = None
    return out


def misumi_holes() -> list[tuple[int, list[int], list[int]]]:
    """Per size range: its upper limit, and the two lines of MISUMI's
    excerpt of JIS B 0401 that hold its upper and lower deviations."""
    text = source("misumi_holefits.pdf")
    body = section(text, r"Hole dimensional tolerances", r"Dimensional tolerances for")
    rows = []
    pending = None
    for line in body.splitlines():
        values = [int(float(v)) if float(v) == int(float(v)) else -1
                  for v in re.findall(r"(?<![\w.])(\d+(?:\.\d+)?)(?![\w.])", line)]
        if len(values) < 19:
            continue
        if pending is None:
            pending = values
        else:
            rows.append((pending, values))
            pending = None
    return [(0, upper, lower) for upper, lower in rows]


def iso965_1_table1() -> dict[int, int]:
    """Pitch (micrometres) -> the fundamental deviation EI of position G."""
    text = source("iso965-1-2013-sample.pdf")
    body = section(text, r"Table\s+1\s+--\s+Fundamental deviations", r"\n6\s+Tolerance grades")
    out = {}
    for line in body.splitlines():
        if not readable(line):
            continue
        match = re.match(r"\s*(\d+(?:,\d+)?)\s+\+?(\d+)\s+0\s", line)
        if match:
            out[round(float(match.group(1).replace(",", ".")) * 1000)] = int(match.group(2))
    return out


def iso965_1_table2() -> dict[int, list[int]]:
    """Pitch (micrometres) -> TD1 of grades 4 to 8 (0 where the standard
    gives none). Lines the watermark crosses are left out."""
    text = source("iso965-1-2013-sample.pdf")
    body = section(text, r"Table\s+2\s+--\s+Minor\s+diameter", r"Table\s+3\s+--\s+Major\s+diameter")
    out = {}
    for line in body.splitlines():
        if not readable(line):
            continue
        match = re.match(r"\s*(\d+(?:,\d+)?)\s+([\d \-]+)$", line.rstrip())
        if not match:
            continue
        values = [0 if v == "-" else int(v.replace(" ", "")) for v in re.findall(NUMBER, match.group(2))]
        if len(values) == 5:
            out[round(float(match.group(1).replace(",", ".")) * 1000)] = values
    return out


def iso965_1_table4() -> dict[tuple[int, int], list[int]]:
    """(diameter range limit, pitch), micrometres -> TD2 of grades 4 to 8.
    The preview prints the ranges up to 2.8 mm only.

    The table prints each range beside the middle row of its block, so the
    blocks are told apart by their pitches, which increase within one.
    """
    text = source("iso965-1-2013-sample.pdf")
    body = section(text, r"Table\s+4\s+--\s+Pitch\s+diameter")
    blocks: list[tuple[int | None, list[tuple[int, list[int]]]]] = [(None, [])]
    for line in body.splitlines():
        if not readable(line):
            continue
        head = re.match(r"\s*(\d+,\d+)\s+(\d+,\d+)\s+(\d+(?:,\d+)?)\s+([\d \-]+)$", line.rstrip())
        rest = re.match(r"\s*(\d+(?:,\d+)?)\s+([\d \-]+)$", line.rstrip())
        if head:
            limit = round(float(head.group(2).replace(",", ".")) * 1000)
            pitch, values = head.group(3), head.group(4)
        elif rest:
            limit, pitch, values = None, rest.group(1), rest.group(2)
        else:
            continue
        numbers = [0 if v == "-" else int(v.replace(" ", "")) for v in re.findall(NUMBER, values)]
        if len(numbers) != 5:
            continue
        micrometres = round(float(pitch.replace(",", ".")) * 1000)
        rows = blocks[-1][1]
        if rows and micrometres < rows[-1][0]:  # a smaller pitch starts a block
            blocks.append((None, []))
            rows = blocks[-1][1]
        rows.append((micrometres, numbers))
        if limit is not None:
            blocks[-1] = (limit, rows)
    out = {}
    for limit, rows in blocks:
        if limit is None:
            continue
        for pitch, numbers in rows:
            out[(limit, pitch)] = numbers
    return out


def iso965_2_internal() -> dict[str, tuple[int, int, int, int]]:
    """Designation -> D2 max, D2 min, D1 max, D1 min (micrometres), from the
    1998 preview, and from the 2024 one where the 1998 watermark crosses a
    row. The 1998 edition prints the coarse and fine series in two tables and
    leaves out a coarse row's pitch; the 2024 edition prints one table with
    every pitch.
    """
    def parse(text: str) -> dict:
        found = {}
        pattern = (r"^\s*M?(\d+(?:,\d+)?)\s+(?:(\d+(?:,\d+)?)\s+)?(\d+(?:,\d+)?)\s+(\d+(?:,\d+)?)\s+"
                   r"(\d+,\d{3})\s+(\d+,\d{3})\s+(\d+,\d{3})\s+(\d+,\d{3})\s*$")
        for line in text.splitlines():
            match = re.match(pattern, line)
            if not match:
                continue
            diameter = round(float(match.group(1).replace(",", ".")) * 1000)
            pitch = None if match.group(2) is None else round(float(match.group(2).replace(",", ".")) * 1000)
            if diameter not in COARSE_PITCHES:
                continue
            if pitch is None or pitch == COARSE_PITCHES[diameter]:
                key = f"M{diameter / 1000:g}"
            else:
                key = f"M{diameter / 1000:g}x{pitch / 1000:g}"
            found[key] = tuple(round(float(match.group(i).replace(",", ".")) * 1000) for i in range(5, 9))
        return found

    text98 = source("iso965-2-1998-sample.pdf")
    out = parse(section(text98, r"5\.1 Internal threads", r"5\.2 External threads"))
    out.update(parse(section(text98, r"5\.3 Internal threads", r"5\.4 External threads")))
    text24 = source("iso965-2-2024-sample.pdf")
    for key, value in parse(section(text24, r"Table\s+1\s+--\s+Limits of sizes for internal")).items():
        out.setdefault(key, value)
    return out


# The coarse pitch of each nominal diameter, micrometres (ISO 262).
COARSE_PITCHES = {1000: 250, 1200: 250, 1400: 300, 1600: 350, 1800: 350, 2000: 400, 2500: 450, 3000: 500,
                  3500: 600, 4000: 700, 5000: 800, 6000: 1000, 7000: 1000, 8000: 1250, 10000: 1500,
                  12000: 1750, 14000: 2000, 16000: 2000, 18000: 2500, 20000: 2500, 22000: 2500, 24000: 3000,
                  27000: 3000, 30000: 3500, 33000: 3500, 36000: 4000, 39000: 4000, 42000: 4500, 45000: 4500,
                  48000: 5000, 52000: 5000, 56000: 5500, 60000: 5500, 64000: 6000}

# The four rows the 1998 preview's watermark crosses and the 2024 preview's
# pages stop before. The tests' table reads them from the characters the
# watermark leaves interleaved, and each is its neighbour's row shifted by
# the difference of the two diameters (see the evidence).
WATERMARKED = {"M18x1.5", "M20x1.5", "M20x2", "M22x1.5"}


def iso965_2_external() -> dict[int, int]:
    """Pitch (micrometres) -> the fundamental deviation of position g, as the
    basic major diameter less the upper limit of the 6g external threads of
    ISO 965-2."""
    text = source("iso965-2-1998-sample.pdf")
    body = section(text, r"5\.2 External threads", r"5\.3 Internal threads")
    out = {}
    for line in body.splitlines():
        if not readable(line):
            continue
        match = re.match(r"\s*M(\d+(?:,\d+)?)\s+\d+(?:,\d+)?\s+\d+(?:,\d+)?\s+(\d+,\d{3})\s", line)
        if not match:
            continue
        diameter = round(float(match.group(1).replace(",", ".")) * 1000)
        # ISO 965-2 gives the external threads of M1 to M1.4 position h,
        # whose deviation is zero, and the larger ones position g.
        if diameter in COARSE_PITCHES and diameter > 1400:
            out[COARSE_PITCHES[diameter]] = diameter - round(float(match.group(2).replace(",", ".")) * 1000)
    return out


class Tables(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.rows: list[list[str]] = []
        self.row: list[str] | None = None
        self.cell: str | None = None

    def handle_starttag(self, tag, attrs):
        if tag == "tr":
            self.row = []
        elif tag in ("td", "th"):
            self.cell = ""

    def handle_endtag(self, tag):
        if tag in ("td", "th") and self.cell is not None and self.row is not None:
            self.row.append(" ".join(self.cell.split()))
            self.cell = None
        elif tag == "tr" and self.row is not None:
            self.rows.append(self.row)
            self.row = None

    def handle_data(self, data):
        if self.cell is not None:
            self.cell += html.unescape(data)


def clearance_sources() -> dict[str, tuple[int, int, int]]:
    """Designation -> fine, medium and coarse clearance holes, in tenths of a
    millimetre, from Engineering Hardware's chart and the German Wikipedia
    article; where both list a size they must agree."""
    parser = Tables()
    parser.feed(source("engineeringhardware_clearance.html"))
    out = {}
    for row in parser.rows:
        if row and re.fullmatch(r"M[\d.]+", row[0]) and len(row) > 3:
            out[row[0]] = tuple(round(float(v) * 10) for v in row[1:4])
    wiki = source("de_durchgangsbohrung.wiki")
    table = wiki[wiki.index("{|"):wiki.index("|}")]
    for chunk in table.split("|-")[1:]:
        cells = [c.lstrip("|").strip() for c in chunk.strip().split("\n") if c.strip().startswith("|")]
        if len(cells) < 4:
            continue
        key = cells[0].replace(",", ".")
        values = tuple(round(float(v.replace(",", ".")) * 10) for v in cells[1:4])
        if key in out and out[key] != values:
            report(f"clearance {key}", False, f"the two charts differ: {out[key]} and {values}")
        out.setdefault(key, values)
    return out


# ---------------------------------------------------------------------------
# The checks
# ---------------------------------------------------------------------------

def check_standard_tolerances() -> None:
    wiki = wikipedia_it()
    report("IT grades: the Wikipedia table lists every range up to 500 mm", sorted(wiki) == IT_RANGES,
           f"{len(wiki)} ranges")
    impl = dict(zip(IT_RANGES, IMPL_IT))
    tests = dict(zip(IT_RANGES, TEST_IT))
    same = sum(1 for limit in IT_RANGES if impl.get(limit) == wiki.get(limit))
    report("IT grades: BetterCAD's table against Wikipedia", same == 13, f"{same}/13 ranges, {13 * 18} values")
    same = sum(1 for limit in IT_RANGES if tests.get(limit) == wiki.get(limit))
    report("IT grades: the tests' table against Wikipedia", same == 13, f"{same}/13 ranges")
    iso = iso286_table1()
    same = sum(1 for limit, row in iso.items() if impl.get(limit) == row)
    report("IT grades: BetterCAD's table against the ISO 286-2:2010 preview", same == len(iso) and same >= 11,
           f"{same} of the {len(iso)} rows its watermark leaves readable")


def check_limit_deviations() -> None:
    iso = iso286_table3()
    tests = {row[0]: row[1:] for row in TEST_DE}
    same = sum(1 for limit, row in iso.items() if tests.get(limit) == row)
    report("D and E limits: the tests' table against the ISO 286-2:2010 preview", same == len(iso) and same >= 9,
           f"{same} of the {len(iso)} rows its watermark leaves readable")
    impl = dict(zip(IT_RANGES, zip(*IMPL_EI)))  # per range: D, E, F, G
    same = sum(1 for limit, row in iso.items() if impl[limit][0] == row[8] and impl[limit][1] == row[15])
    report("D and E fundamental deviations: BetterCAD's table against the same preview", same == len(iso),
           f"{same} ranges")
    misumi = misumi_holes()
    report("F, G and H limits: MISUMI's excerpt has a row pair per range", len(misumi) == 13, f"{len(misumi)} pairs")
    ok = 0
    for i, (_, upper, lower) in enumerate(misumi):
        if i >= len(TEST_DEVIATIONS):
            break
        expected = TEST_DEVIATIONS[i]
        wantedUpper = expected[1:17]
        d, e, f, g, _ = expected[17:22]
        wantedLower = [d, d, d, e, e, e, f, f, f, g, g]
        if contiguous(upper, wantedUpper) and contiguous(lower, wantedLower):
            ok += 1
    report("D to H limits: the tests' table against MISUMI's excerpt of JIS B 0401", ok == 13,
           f"{ok}/13 ranges, {13 * 16} upper and {13 * 5} lower deviations")


def check_threads() -> None:
    sizes = iso965_2_internal()
    missing = {key for key in TEST_THREADS if key not in sizes}
    same = sum(1 for key, value in TEST_THREADS.items() if sizes.get(key) == value)
    report("thread limits: the tests' table against the ISO 965-2 previews",
           same == len(TEST_THREADS) - len(missing) and missing == WATERMARKED,
           f"{same} of {len(TEST_THREADS)} sizes; the previews leave {sorted(missing)} unreadable")
    # Those four rows are the same as their neighbours of the same pitch, one
    # diameter step apart, which the previews do print.
    for key in sorted(WATERMARKED):
        pitch = key.split("x")[1]
        diameter = float(key[1:].split("x")[0])
        neighbour = f"M{diameter - 2:g}x{pitch}"
        if neighbour not in TEST_THREADS:
            continue
        shifted = tuple(v + 2000 for v in TEST_THREADS[neighbour])
        report(f"thread limits: {key} against {neighbour}", TEST_THREADS[key] == shifted,
               f"{TEST_THREADS[key]} against {shifted}")
    impl = []
    for diameter, pitch, coarse in IMPL_SIZES:
        impl.append(f"M{diameter / 1000:g}" if coarse else f"M{diameter / 1000:g}x{pitch / 1000:g}")
    report("thread sizes: BetterCAD's table against the tests' table", impl == list(TEST_THREADS),
           f"{len(impl)} sizes")

    ei = iso965_1_table1()
    same = sum(1 for pitch, (g, _) in IMPL_PITCHES.items() if ei.get(pitch) == g)
    report("position G: BetterCAD's deviations against ISO 965-1:2013, Table 1", same == len(IMPL_PITCHES),
           f"{same}/{len(IMPL_PITCHES)} pitches")
    external = iso965_2_external()
    same = sum(1 for pitch, value in external.items() if TEST_EXTERNAL.get(pitch) == value)
    report("position g: the tests' table against the 6g threads of ISO 965-2",
           same == len(external) and same >= 17, f"{same}/{len(external)} pitches")

    td1 = iso965_1_table2()
    checked, wrong = 0, 0
    for pitch, (_, value) in IMPL_PITCHES.items():
        row = td1.get(pitch)
        if row is None:
            continue
        checked += 1
        grade = 5 if pitch <= 300 else 6
        if row[grade - 4] != value:
            wrong += 1
            report(f"TD1 of pitch {pitch} um", False, f"{value} against the standard's {row[grade - 4]}")
    report("TD1: BetterCAD's tolerances against ISO 965-1:2013, Table 2", wrong == 0 and checked >= 17,
           f"{checked} of {len(IMPL_PITCHES)} pitches (its watermark crosses the others)")

    td2 = iso965_1_table4()
    checked, wrong = 0, 0
    for (limit, pitch), value in IMPL_TD2.items():
        row = td2.get((limit, pitch))
        if row is None:
            continue
        checked += 1
        grade = 5 if limit <= 1400 else 6
        if row[grade - 4] != value:
            wrong += 1
            report(f"TD2 of {limit} um, pitch {pitch} um", False, f"{value} against the standard's {row[grade - 4]}")
    report("TD2: BetterCAD's tolerances against the printed part of ISO 965-1:2013, Table 4",
           wrong == 0 and checked == 5, f"{checked} of {len(IMPL_TD2)} entries (the preview prints the ranges "
                                        f"up to 2.8 mm)")
    derived = 0
    for key, (d2max, d2min, _, _) in TEST_THREADS.items():
        diameter = round(float(key[1:].split("x")[0]) * 1000)
        pitch = round(float(key.split("x")[1]) * 1000) if "x" in key else None
        if pitch is None:
            pitch = next(p for d, p, coarse in IMPL_SIZES if d == diameter and coarse)
        limit = next(l for l in (1400, 2800, 5600, 11200, 22400, 45000, 90000) if diameter <= l)
        if IMPL_TD2.get((limit, pitch)) == d2max - d2min:
            derived += 1
    report("TD2: BetterCAD's tolerances against the limits of ISO 965-2", derived == len(TEST_THREADS),
           f"{derived}/{len(TEST_THREADS)} sizes")


def check_clearance_holes() -> None:
    charts = clearance_sources()
    same = 0
    for key, value in TEST_CLEARANCE.items():
        if charts.get(key) == value:
            same += 1
        else:
            report(f"clearance {key}", False, f"{value} against the charts' {charts.get(key)}")
    report("clearance holes: the tests' table against two published charts", same == len(TEST_CLEARANCE),
           f"{same}/{len(TEST_CLEARANCE)} sizes")
    impl = {f"M{diameter / 10:g}": holes for diameter, holes in IMPL_CLEARANCE_ROWS.items()}
    report("clearance holes: BetterCAD's table against the tests' table", impl == TEST_CLEARANCE,
           f"{len(impl)} sizes")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true", help="download the sources into ./sources")
    args = parser.parse_args()
    if args.fetch:
        fetch()
        print()
    check_standard_tolerances()
    check_limit_deviations()
    check_threads()
    check_clearance_holes()
    print()
    print(f"{CHECKS} checks, {len(FAILURES)} failed")
    for name in FAILURES:
        print(f"  failed: {name}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
