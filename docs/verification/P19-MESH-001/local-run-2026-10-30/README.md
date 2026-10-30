# Local build and full-suite run, 2026-10-30 — supporting evidence, NOT a gate

```text
RESULT:    NO VERDICT. This directory closes no gate and claims no PASS for
           P19-MESH-001, which remains BLOCKED as FREEZE.md records.
TICKED:    nothing. TODO.md is unchanged: Determinism PASS, Regression PASS and
           Evidence recorded stay open.
WHAT IT IS the frozen candidate built and its whole test suite run, once per
           code page, on the non-qualification machine, OUTSIDE the harness.
MACHINE:   the same machine as the 2026-10-29 recovery and re-audit, which could
           not build then. MSYS2 UCRT64 GCC 16.2.0, CMake 4.4.3, Ninja 1.13.2.
           The pinned dependencies were built here from deps/CMakeLists.txt.
```

## Why this is not the qualification

```text
one preset (debug-ext); the gate needs debug-ext, release-ext, debug-shared-ext
no clean build: the tree was built incrementally and never wiped
no determinism repeats: the gate needs 5 x the selected set, twice
no cross-preset comparison of the CfdMesh census and quality text
GCC 16.2.0, not the qualification machine's WinLibs 16.1.0
BETTERCAD_BUILD_GUI=OFF, so the 10 gui.* tests and infra.deploy.location-
  independent do not exist here: 5928 selected, not 5939
not run through qualify.cmd, so no stage accounting and no fingerprint check
```

It answers "does this code work on a second toolchain". It cannot answer "is it
qualified", and no number in it may be read as a gate result.

## Files

```text
configure-build-selection.txt       toolchain, dependency versions, the cache
                                    settings, the no-op rebuild proving the suites
                                    ran against the built binaries, and the
                                    selection accounting for 5928 against 5939
ctest-full-debug-ext-65001.log      the full suite under code page 65001, the page
                                    qualify.cmd:39 sets: 5928 / 5928, exit 0
ctest-full-debug-ext-oem-codepage.log  the full suite under code page 437:
                                    5927 / 5928, exit 8, the one failure
                                    cli.new.unicode-path
unicode-path-65001.log              that test alone under 65001: Passed
unicode-path-oem-codepage.log       that test alone under 437, captured through
                                    `cat -v` so the raw bytes are legible
quality-f1-65001.log                the F1 quality table, 104 assertions
```

## What was measured

```text
dependencies      OCCT 8.0.1, Netgen 6.2.2604, zlib 1.3.1, into a local prefix
configure         exit 0, Debug, static, warnings-as-errors ON, CLI ON,
                  desktop app OFF, volume meshing on Netgen, tests ON
build             773 / 773 edges, exit 0, 10m41s, 0 `warning:` lines -- which
                  warnings-as-errors would have turned into a failure anyway
full suite, 65001 5928 / 5928 passed, 0 failed, 0 timeout, 0 not run,
                  0 skipped, ctest exit 0, 2035.19 s
full suite, 437   5927 / 5928, ctest exit 8, 1946.89 s; see below
unit.CfdMesh_*    20 of 20 passed INSIDE each full run, not only in isolation
F1 quality table  all eight rows reproduce the figures in this milestone's
                  README exactly, on a different compiler build
```

## The single failure under code page 437, and why it is not a defect

The test writes a document whose name is `Plåt ✓` and asserts on both the file and
the process's stdout. Under code page 437 the argument and the file on disk are
correct UTF-8; only the captured stdout is not:

```text
argument, and the file written   Pl C3A5 t  E29C93              = "Plåt ✓", correct
captured stdout                  Pl E2949C C391 t  CE93 C2A3 C3B4
```

`C3` decoded as `├`, `A5` as `Ñ`, `E2` as `Γ`, `9C` as `£`, `93` as `ô`: the
emitted UTF-8 bytes were decoded as CP437 and re-encoded. The `FILE_REGEX` half
passes and the on-disk name is exactly `"name": "Plåt ✓"`; only `STDOUT_REGEX`
fails.

The cause is the console the run was launched from, not the code. The harness never
has this problem: `qualify.cmd:39` is `chcp 65001 > nul`, and `qualify.cmd:12` says
why — "Runs under code page 65001 (the CLI's Unicode test needs it)". Under 65001
the same binary and the same test print `Plåt ✓` and pass.

## The gap this closed, which is the reason the second run was made

The pre-freeze record's 5938 / 5939 was taken outside the harness and showed this
same failure, so it left the direction of the sensitivity open. A test that passed
under an OEM page and failed under UTF-8 would have failed attempt 2 inside the
harness, and nothing recorded would have predicted it. **No full-suite run under
65001 had ever been recorded.** One has been now, and it is 5928 / 5928: the
sensitivity runs one way only, and the one sensitive test is the one that is
sensitive on purpose.

## Repository state

```text
HEAD                b474ac8, == origin/main, unchanged by this work
source and tests    not modified. The eight subtree hashes and the four milestone
                    fingerprints are unchanged; this directory is under docs/,
                    which is in neither set, so the candidate needs no refreeze.
qualify.cmd         still d313a640
NEXT                the clean three-preset re-run on the qualification machine.
                    P19-TOPO-001 is NOT AUTHORIZED and was not started.
```

## Extended the same day to three presets and both determinism repeats

Everything above is one preset. The run was continued on 2026-10-30 to cover all
three qualification presets and both repeat stages, on the same frozen candidate
and the same machine. Still supporting evidence: no gate closed, no mark changed,
nothing ticked.

```text
release-ext       full suite    5928 / 5928 passed, exit 0, 1674.72 s
debug-shared-ext  full suite    5928 / 5928 passed, exit 0, 1795.96 s
debug-ext         full suite    5928 / 5928 passed, exit 0, 2035.19 s  (above)
release-ext       repeat 5x     2793 selected, 13965 Passed lines, exit 0, 1413.59 s
debug-ext         repeat 5x     2793 selected, 13965 Passed lines, exit 0, 1148.56 s
```

13965 is 2793 x 5 exactly, in both repeat logs: every test in the selection ran
five times and passed five times, under `--repeat until-fail:5`, the flag the
harness uses. The selection was counted from the log rather than assumed, which is
the check attempt 1 passed with 352.

### The cross-preset comparison, which is the part that could have failed

The F1 reference-mesh quality table was captured from each preset's own binary and
compared line by line. All eight rows are identical across `-O0` static Debug,
`-O2` Release and shared Debug, and identical to the figures recorded in ADR-063:

```text
RM-MESH-01 Block                   6   64.09   47.56   0.5376
RM-MESH-02 Cylinder              946   89.28   57.89   4.6077
RM-MESH-03 PlateWithHole         172   84.07   56.70   6.5099
RM-MESH-04 Tube                 1472   85.20   55.27   3.7138
RM-MESH-05 ThinPlate              18   88.81   66.48   0.8206
RM-MESH-06 TransformedBase        18   66.00   47.04   0.8206
RM-MESH-06 TransformedPlaced      18   66.00   47.04   0.8206
RM-MESH-07 LocalRefinement       528   75.58   31.14   1.3109
            interior faces  non-orth max  non-orth mean  skewness max
```

The only differences between the three captures are the build path, the working
directory and Catch2's per-run random seed (1880742401, 47156142, 1925996343 --
unseeded by default and different in all three). The table is identical despite
the differing seed, so the figures are independent of test order as well as of
optimisation level. 104 assertions in each preset.

This is the result worth having: non-orthogonality and skewness are built from
`arccos`, cross products and divisions, exactly the arithmetic where `-O0` and
`-O2` are free to diverge through reassociation or a different fused multiply-add.
They do not diverge here, to the last recorded digit, on a compiler that is not
the qualification machine's.

### What the earlier gap list now reads as

Three of the lines in "Why this is not the qualification" above are superseded:

```text
"one preset (debug-ext)"                 -> all three presets, full suite, exit 0
"no determinism repeats"                 -> both repeat stages, 5x, exit 0
"no cross-preset comparison of the       -> done, identical on all three
 CfdMesh census and quality text"
```

Four lines still stand, and they are still why this is not the qualification:

```text
not a clean build: release-ext and debug-shared-ext were configured and built
  from an empty tree in this session, but debug-ext was incremental and no stage
  ran `ninja -t clean`, which is what qualify.cmd does before each build
GCC 16.2.0, not the qualification machine's WinLibs 16.1.0
BETTERCAD_BUILD_GUI=OFF, so the full suite selects 5928 and not 5939, and the
  repeat selection is 2793 and not the 2803 the gate requires
not run through qualify.cmd, so there is no stage accounting and no fingerprint
  check inside the run
```

`Determinism PASS`, `Regression PASS` and `Evidence recorded` therefore stay open.
A determinism gate is what the harness measures across three presets with a
counted selection of 2803; five clean repeats of 2793 outside the harness is
evidence towards it and is not it.

### The no-op rebuild, stated precisely

Each preset's suite was run after a rebuild that did no work:

```text
cmake --build --preset release-ext        [1/7] Checking git revision    exit 0
cmake --build --preset debug-shared-ext   [1/19] Checking git revision   exit 0
```

That single line is the whole output of each. The git-revision stamp is always
dirty, so ninja plans it; its output is unchanged, so ninja prunes the six and
eighteen edges that depend on it. Zero `Building CXX` lines and zero `Linking`
lines in either, confirmed by grep. The binaries the suites ran against were
therefore the binaries the build stage produced.

### Fingerprint, re-confirmed after all of it

`fingerprint-after-three-presets.txt` is `freeze-mesh.ps1` re-run on this checkout
after the last stage. All eight subtree hashes and all four milestone fingerprints
equal the frozen ones, scratch tree `399d006c`, HEAD `b474ac8`, and the "P16 files
changed against HEAD" section is empty. The tree that was tested is the frozen
candidate.

### Disk footprint, which is also the free-space measurement

```text
debug-ext          6260 MB      release-ext         468 MB
debug-shared-ext   4769 MB      TOTAL            11 495 MB  = 11.23 GB
```

750 object files in each preset. Measured after configure, build, no-op rebuild
and full ctest in all three, from a build root that started empty, with the GUI
off. `footprint-final.txt` has the per-binary breakdown.

### Files added by this extension

```text
ctest-full-release-ext-65001.log        5928 / 5928, exit 0
ctest-full-debug-shared-ext-65001.log   5928 / 5928, exit 0
ctest-repeat-release-ext-65001.log      2793 x 5, 13965 Passed, exit 0
ctest-repeat-debug-ext-65001.log        2793 x 5, 13965 Passed, exit 0
quality-f1-release-ext-65001.log        the F1 table from the Release binary
quality-f1-debug-shared-ext-65001.log   the F1 table from the shared Debug binary
fingerprint-after-three-presets.txt     freeze-mesh.ps1 output after the last stage
footprint-final.txt                     the three-preset disk measurement
```

The two repeat logs are 3.1 MB each because the determinism claim is the 13965
lines in them; the claim would otherwise have no log behind it.
