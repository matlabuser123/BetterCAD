# P17-VIZ-001 — the element contours' qualification

```text
RESULT:    PASS
CANDIDATE: 974b62be08daa40285238d214b2672c2492af13f  (the parent commit)
RAN:       Tue 13/10/2026 05:50:35 -> 09:05:52  =  3h15m17s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
```

This run takes P17-VIZ-001 to **23 of 23**.

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **3835 / 3835** | **3835 / 3835** | **3835 / 3835** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| build | 27m04s | 32m22s | 26m36s |
| ctest | 25m41s | 24m27s | 25m00s |
| no-op rebuild | exit 0 | exit 0 | exit 0 |

```text
repeat release-ext   980 tests x 5   16m00s   exit 0
repeat debug-ext     980 tests x 5   17m24s   exit 0
```

Every count was **measured, not predicted**, and each reconciles against the
glyph run plus the ten cases added here:

```text
suite       3825 + 10 = 3835    MeshView 13 -> 20, StructuralViz 66 -> 69
repeat       970 + 10 = 980     the filter is unchanged -- `unit\.Mesh` already
                                covers unit.MeshView and `unit\.Structural`
                                covers unit.StructuralViz, as substrings
```

## The tree that was qualified is the tree that was committed

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            d6b09e353fd6c4017fbf0c2d1f6dcdb11b64b867
src                7713081c1c09982502192961209449b3a9ffcb6e
tests              cb1e5f7e415e3eb95a01581ac4ac87fde8e75220
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, and `freeze-fingerprint.txt` beside this file carries the
same eight values taken independently, in Bash, before the launch. Two
witnesses, and they agree.

Three of the eight moved against `974b62b` and five did not — `include`, `src`
and `tests` changed; `apps`, `examples`, `cmake`, `CMakeLists.txt` and
`CMakePresets.json` are byte-identical. The element contours touched no
application code, no reference model and no build configuration.

### And the meshing module is untouched, which is the point of the increment

```text
git diff --name-only HEAD -- src/meshing include/bettercad/meshing
  (empty)
```

Checked as a **gate before the freeze**, not asserted afterwards. The blocker
this increment removes claimed that element contours needed a new capability in
the qualified P16 meshing module; that `src` changed while `meshing` did not is
the measured refutation.

`StructuralContour` is likewise unchanged — `triangleCornerBands` reads
`elementOfTriangle` and looks the id up in the field, so a view whose element
ids are tetrahedra simply works.

## The pre-freeze checks

Cheap first, for the reason P15 recorded three times over.

```text
1  MeshView.cpp byte-compared against its pristine copy -- no mutant left
   behind by the probe batch
2  the meshing module: zero diff against HEAD
3  line endings: MeshView.hpp, MeshView.cpp and MeshViewTests.cpp uniformly
   CRLF; StructuralResultViewTests.cpp uniformly LF. No mixed file.
4  git diff --check -- no whitespace errors
5  no BETTERCAD_*_EXPORT on a header-defined constexpr or inline
6  the FULL default target built -- the clean stage deletes the application,
   so a targeted rebuild would leave gui.* with no executable
7  the filter and the suite recounted with ctest -N: 980, 89, 3835
8  the affected set (89 cases) under --repeat until-fail:5  ->  89/89
9  the debug-shared-ext build  ->  0 compiler diagnostics
10 the affected set in the shared preset  ->  89/89
```

## A verdict script that cried VOID, fixed and then PRE-VALIDATED

The glyph run's verdict printed

```text
*** THE TREE MOVED DURING THE RUN -- the qualification is VOID ***
*** THE TWO WITNESSES DISAGREE ***
```

on a sound qualification. Both came from one off-by-one:
`grep -A9 "<header>" | tail -8` returns the header plus nine lines, so `tail -8`
**drops the `apps` line and picks up the following unrelated line** — for the
"after" block, `qualification finished ...`. The hashes all matched; only the
trailing junk differed.

For this run both checks extract the block **by name**, with `awk` keyed on the
header and stopping after eight hash lines. And both were **run against the
glyph run first**, where the answer is known to be "agree" — because a verdict
script that cries void is exactly as dangerous as one that cries pass, and
testing it only on the run it is about to judge tests nothing.

```text
validated on qualification-glyphs before being trusted here:
  before == after                           (8 lines each, identical)
  the two witnesses agree on all eight paths
```

They reported correctly here, with no false alarm.

## What this run owes, and discharges

Nothing new outside the renderer. One qualified P16 file pair was extended
again — `MeshView.hpp` and `MeshView.cpp` — for one new `MeshSource`
enumerator and one new factory. The ten new cases are 7 in `MeshViewTests` and
3 in `StructuralResultViewTests`.

`qualify.cmd` is byte-identical to the one twenty-six milestones have now used:

```text
qualify.cmd          git hash-object d313a64070718c44fae290ac042fe259d1a03c8b
                     sha1sum         244b38de8feac0475af7cdc56ba9415d27d8dd29
verify-harness.cmd   git hash-object 11855b8ff891093b021d1e0351761283d0eedd24
                     sha1sum         afd0197d4f4726db7f533c2f27f4f6e069ba83c6
```

## P17-VIZ-001 is complete at 23 of 23

What remains undrawn is recorded in REMAINING_SCOPE.md and is **not** part of
these boxes: `LoadGlyph` (a pressure has no single point and gravity has no
location, so a per-facet arrow needs facet centroids through
`GeometryMeshMap`), and the Qt inspector surface — no panel, no channel
selector, no legend widget, no stale badge, no navigation buttons. Every
headless layer those would bind to is built, tested and qualified; none of it
is wired.
