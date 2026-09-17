# P12-HOLE-001 — the sources of the standards data

BetterCAD's hole standards are tabulated data: their correctness is a
question of transcription, so every table is checked against published
copies of the standard it comes from, by
[`crosscheck.py`](crosscheck.py) (its run:
[`crosscheck.log`](crosscheck.log)).

Two transcriptions are compared with the sources, and with each other by the
tests:

| | |
| --- | --- |
| BetterCAD's tables | `src/core/standards/*.cpp` |
| The tests' reference tables | `tests/core/standards/HoleStandardsTests.cpp` |

The tests compare the two; `crosscheck.py` compares both with the sources.
Where a source's table is available in two copies, both are used, and the
reference table of a value is taken from a different copy than BetterCAD's
own, so that one transcription error cannot pass both.

## The sources

The files themselves are **not** in the repository (they are published
previews and third-party pages, not BetterCAD's to redistribute).
`python crosscheck.py --fetch` downloads them into `sources/`, which is
ignored by git, and checks each against the SHA-256 below.

| File | What it is | URL | SHA-256 |
| --- | --- | --- | --- |
| `iso286-2-2010-sample.pdf` | ISO 286-2:2010 published preview: Table 1 (standard tolerance grades IT01 to IT18, taken from ISO 286-1:2010), Table 3 (the limit deviations of holes D and E) and Figures 3 and 4 (the tolerance classes the standard tabulates) | `https://cdn.standards.iteh.ai/samples/54915/7d6147c29fed4e31af36b60561e88752/ISO-286-2-2010.pdf` | `b96cbd437dd86ea46584e5c4f1f95e736ac941c417620a5ce0e998cd3fc16e2b` |
| `it_grade.wiki` | The Wikipedia article "IT Grade", whose table is ISO 286-1:2010, Table 1 (source of the tests' reference table of the standard tolerances) | `https://en.wikipedia.org/w/index.php?title=IT_Grade&action=raw` | `9211cf722badd09b5a9b831a3a0122d59e0865feadcd41a77f0196304afa4080` |
| `misumi_holefits.pdf` | MISUMI's technical data sheet "Tolerances of regularly used hole fits", an excerpt of JIS B 0401 (1999), which is ISO 286: the limit deviations of D8 to D10, E7 to E9, F6 to F8, G6, G7 and H6 to H10 for every size range up to 500 mm | `https://us.misumi-ec.com/pdf/press/us_12e_pr1263.pdf` | `de6d42b9d1ee202d384999c590eefa2179e193d1b6b73eb0d2aef26ce3f2c5ba` |
| `iso965-1-2013-sample.pdf` | ISO 965-1:2013 published preview: Table 1 (the fundamental deviations of positions G and H), Table 2 (the minor diameter tolerances TD1) and the printed part of Table 4 (the pitch diameter tolerances TD2 of the ranges up to 2.8 mm) | `https://cdn.standards.iteh.ai/samples/57778/765c17fc0f7946ff850db22b43920f09/ISO-965-1-2013.pdf` | `fef2802aebccef9de45742a53db3b00de5d983332e3bd8dba7c98a50245b2280` |
| `iso965-2-1998-sample.pdf` | ISO 965-2:1998 published preview: the limits of sizes of the internal threads (5H and 6H) and external threads (6h and 6g) of the coarse and fine series | `https://cdn.standards.iteh.ai/samples/23594/74d3eec3e0d04d999a1680f11b4f04ec/ISO-965-2-1998.pdf` | `76949e20668cd1601bb841f05e0ec1e25ecf8ad4b309fca62d35edef639ea13c` |
| `iso965-2-2024-sample.pdf` | ISO 965-2:2024 published preview (the current edition): the same limits, in one table, as far as the preview prints it (M1 to M16) | `https://cdn.standards.iteh.ai/samples/87890/690c4af45f494dbea186bd921fa9132e/ISO-965-2-2024.pdf` | `9dc79f4bc84aa25968b9a2a812c32ff2ac9a62aa136ec53d20f5ca501b6a9388` |
| `engineeringhardware_clearance.html` | Engineering Hardware's "Clearance Hole Sizes (ISO 273)": every size of ISO 273 from M1 to M64 but M1.8 | `https://engineeringhardware.com/fastener/clearance-hole-sizes/` | `a6cb22692097f7af7104ec0cd3c0eac317277fb93402bc026f4ab3dadcb60845` |
| `de_durchgangsbohrung.wiki` | The German Wikipedia article "Durchgangsbohrung", whose table is DIN EN 20273 (ISO 273): M1 to M64, including M1.8 | `https://de.wikipedia.org/w/index.php?title=Durchgangsbohrung&action=raw` | `8cd54563280f16eae69e07e2914b2b77403b2f22b6517e5a1deb0b633adb442c` |

The previews of ISO 273:1979
(`https://cdn.standards.iteh.ai/samples/4183/1a8db2e6de054d2e9bed7d40be64d6e1/ISO-273-1979.pdf`)
were read as well: the 1979 edition is a scan, and its clearance holes are
readable but only through OCR, so it is used as a third reading of the
values rather than as a source `crosscheck.py` parses. It is also where the
tolerance classes the standard gives the three series for information (H12,
H13 and H14) come from.

## What each table is checked against

| BetterCAD's table | Checked against | Cross-checked by the tests against |
| --- | --- | --- |
| Standard tolerances IT1 to IT18, sizes up to 500 mm (`HoleTolerances.cpp`) | ISO 286-2:2010 preview, Table 1 | the Wikipedia copy of ISO 286-1:2010, Table 1 |
| The fundamental deviations of D, E, F and G (`HoleTolerances.cpp`) | ISO 286-2:2010 preview, Table 3 (D and E) | MISUMI's excerpt of JIS B 0401 (D, E, F, G and H, as limit deviations) |
| The metric thread sizes of ISO 965-2 (`MetricThreads.cpp`) | the ISO 965-2 previews | — |
| The fundamental deviation of position G (`MetricThreads.cpp`) | ISO 965-1:2013 preview, Table 1 | the major diameters of the 6g external threads of ISO 965-2, which are the basic diameter less the same deviation |
| The minor diameter tolerances TD1 (`MetricThreads.cpp`) | ISO 965-1:2013 preview, Table 2 | the minor diameter limits of ISO 965-2 (their difference is TD1) |
| The pitch diameter tolerances TD2 (`MetricThreads.cpp`) | ISO 965-1:2013 preview, Table 4, as far as it is printed | the pitch diameter limits of ISO 965-2 (their difference is TD2), and the formula of ISO 965-1 (in the tests, within one R40 step) |
| The clearance holes of ISO 273 (`ClearanceHoles.cpp`) | Engineering Hardware's chart and the German Wikipedia article, which agree wherever both list a size | the OCR of the ISO 273:1979 preview |

## What the previews leave unreadable

A published preview stamps a watermark across its pages, and the text under
it comes out interleaved with the stamp's characters. `crosscheck.py` skips
any line the watermark crosses and reports how many rows it read, so no
comparison is silently dropped:

- ISO 286-2 Table 1: 11 of the 13 size ranges (the Wikipedia copy covers all
  13, and BetterCAD's table agrees with both wherever they are readable);
- ISO 286-2 Table 3: 9 of the 13 ranges;
- ISO 965-1 Table 2: 18 of the 22 pitches;
- ISO 965-2: 56 of the 60 sizes. The four it leaves unreadable (M18x1.5,
  M20x1.5, M20x2 and M22x1.5) are in the fine series, which the 2024
  preview's pages stop before. Their rows were read from the characters the
  watermark leaves interleaved, and each is checked against the row of the
  same pitch two millimetres below it, which the previews do print: a fine
  thread's limits differ by exactly the difference of the two diameters,
  because the pitch, the tolerances and the fundamental deviation are the
  same.
