# P17-VIZ-001 — the glyph overlays' qualification

```text
RESULT:   PASS
CANDIDATE: ec680762d113b285f39f76e484f9b6f9f2d732f8  (the parent commit)
RAN:      Tue 13/10/2026 00:48:36.16 -> 04:09:36.64   =  3h21m00s
STAGES:   17, every one exit 0, 0 stage(s) failed
HARNESS:  qualify.cmd exit 0, "Qualification passed: every stage exited 0."
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **3825 / 3825** | **3825 / 3825** | **3825 / 3825** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| build | 28m13s | 33m51s | 27m15s |
| ctest | 26m25s | 24m40s | 25m48s |
| no-op rebuild | exit 0 | exit 0 | exit 0 |

```text
repeat release-ext   970 tests x 5   16m19s   exit 0
repeat debug-ext     970 tests x 5   17m57s   exit 0
```

The **no-op rebuild** stage is what says the binaries that ran the tests are
the ones the build just produced — the counterpart of "a restored source is not
a restored binary", checked rather than assumed.

The repeat count was **counted, not predicted**: `ctest -N` against the filter
gave 970, which reconciles with the display run's 951 plus the 19 cases added
here (47 → 66). The suite total moves 3806 → 3825 by the same 19.

## The tree that was qualified is the tree that was committed

The eight fingerprinted paths, recorded by `qualify.cmd` before the first build
and again after the last test run:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            a8336ea248163f703b77aceaa2af6e160c7ef76f
src                09da08e5b8b0ee34e62ff6bbfee76693efabb490
tests              2b1cddebb9d3a5e8beba1162786b71fa33609e52
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, and `freeze-fingerprint.txt` beside this file carries the
same eight values taken **independently**, in Bash, before the run was
launched. A fingerprint recorded only by the thing being audited is not a
cross-check, so there are two witnesses and they agree.

Three of the eight moved against the display run and five did not, which is
itself a check: `include`, `src` and `tests` changed; `apps`, `examples`,
`cmake`, `CMakeLists.txt` and `CMakePresets.json` are byte-identical to
`ec68076`. The glyph work touched no application code, no reference model and
no build configuration.

## The harness

Byte-identical to the one P16-SIZE-001 settled on, **twenty-five milestones in
a row** counting this one. Both hash forms are recorded because they differ and
a hash without its algorithm cannot be checked — `.gitattributes` sets
`*.cmd text eol=crlf`, so git stores LF while the working tree is CRLF.

```text
qualify.cmd          git hash-object d313a64070718c44fae290ac042fe259d1a03c8b
                     sha1sum         244b38de8feac0475af7cdc56ba9415d27d8dd29
verify-harness.cmd   git hash-object 11855b8ff891093b021d1e0351761283d0eedd24
                     sha1sum         afd0197d4f4726db7f533c2f27f4f6e069ba83c6
```

Derived from both ends rather than carried from prose: that `sha1sum` appears
in 24 of the 96 `qualify.cmd` copies under `docs/verification/`, and
`verify-harness.cmd`'s in 26 of 58.

`run-qualification.cmd`'s every-line-begins-with-`rem`/`set`/`call`/`echo`
invariant was checked before the run, and no stray line was found.

## The pre-freeze checks, in cheap-first order

All three of P15's voided qualifications came from running a cheap check after
an expensive one, so the order is deliberate. Every one of these ran **before**
the freeze:

```text
1  the four mutated sources byte-compared against their pristine copies
2  Viewer.hpp normalised -- it held 398 CRLF and 27 BARE LF lines, because the
   lines added here were written LF into a CRLF file. Git stores LF for that
   path either way, so the COMMITTED content is identical; the raw bytes are
   not, and the fingerprint is taken over the working tree.
3  no mixed line endings anywhere in the changed set (measured with perl, after
   a grep -c $'\r' reading gave a false uniform answer)
4  git diff --check -- no whitespace errors
5  no BETTERCAD_*_EXPORT on a header-defined constexpr or inline, across all
   five renderer headers
6  the FULL default target built -- the clean stage deletes the application, so
   a targeted rebuild of bettercad_tests alone leaves gui.* with no executable
7  the repeat filter recounted: 970, of which 66 are unit.StructuralViz
8  the viz set under --repeat until-fail:5  ->  66/66
9  the debug-shared-ext build, the only preset that catches a DLL-boundary
   defect  ->  0 compiler diagnostics
10 the viz set in the shared preset  ->  66/66
```

Check 9's log reported one line matching `warning`, which was the CMake banner
`--   Warnings as errors    : ON` and not a diagnostic. Counting `warning:` in
gcc's own format gives 0 in all three presets.

## Two false starts before the run took, and neither is a gate failure

**`cmd /c` from Git Bash runs nothing and returns 0.** MSYS rewrites the bare
`/c` into `C:/`, so cmd.exe never sees a switch, starts interactively, reads
EOF from the detached stdin and exits successfully. The launcher printed
"returned 0" in zero seconds. What caught it was not the exit code but the
absence of `qualification-times.txt` — a launch whose success is inferred from
`$?` is not verified at all.

The three candidate forms were then tried against a **two-line throwaway batch
file** rather than against the three-hour run, which mattered: one hangs
forever holding the pipeline, and one would have started the real thing
mid-experiment. The working form is `cmd //c "$(cygpath -w <path>)"` — `//c` so
the switch survives conversion, and `cygpath` so the path is converted
explicitly instead of guessed.

Neither false start ran a build or a test, and the frozen tree was byte-verified
intact afterwards, so no qualification was voided.

## A defect in my own verdict script, recorded because it looked like a void

The verdict printed:

```text
*** THE TREE MOVED DURING THE RUN -- the qualification is VOID ***
*** THE TWO WITNESSES DISAGREE ***
```

Both were wrong, and both came from one off-by-one of mine:
`grep -A9 "<header>" | tail -8` returns the header plus nine lines, so `tail -8`
**drops the `apps` line and picks up the following unrelated line** — for the
"after" block that is `qualification finished ...`. The two sides then differed
in their trailing junk while every hash matched.

Re-extracted by name instead of by offset, the before and after blocks are
identical line for line, and the independent fingerprint matches the harness's
on all eight paths. The run is sound; the check was not.

It is recorded rather than quietly corrected because a verdict script that
cries void is exactly as dangerous as one that cries pass: the next person to
see that output needs to know it was the extraction.

## What this run owes, and discharges

Nothing new was created outside the renderer. The run requalifies
`inspectNode`, whose signature changed to take the `MeshDofMap` — the fix for a
defect found in **committed, qualified code** (DEFECT_RESTRAINT_COMPONENTS.md).
Two qualified P16 files were extended again, `Viewer.hpp` and `OcctViewer.cpp`,
for `PresentationKind::Overlay` and `Viewer::displayOverlay`.

The three element-contour boxes are **not** covered by this run and stay
unticked. They are blocked on a triangle-to-owning-tetrahedron mapping that the
qualified P16 meshing module does not have, which is a scope decision rather
than a defect.
