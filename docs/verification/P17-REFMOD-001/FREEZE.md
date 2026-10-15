# P17-REFMOD-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: 460321a85be1ef4026cae2668ff770fad063cb72  (the parent commit)
           plus the working tree: 5 files modified, 5 sources new, the
           evidence directory new -- git status --short recorded before
           the launch (twelve entries, plus the untracked .claude/ settings
           directory, which is not committed)
RAN:       Thu 15/10/2026 12:44:40 -> 16:28:21  =  3h43m41s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
MACHINE:   the hasib machine, as P17-CLI-001 -- see "Where this ran" below
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **4006 / 4006** | **4006 / 4006** | **4006 / 4006** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| objects compiled | 657 / 657 (679 build steps) | 657 / 657 | 657 / 657 |
| configure | 3.7s | 3.8s | 3.8s |
| clean | exit 0 | exit 0 | exit 0 |
| build | 29m16s | 35m54s | 28m46s |
| no-op rebuild | exit 0, 1 edge | exit 0, 1 edge | exit 0, 1 edge |
| ctest | 30m41s (1841.09 s) | 27m23s (1642.80 s) | 29m34s (1773.87 s) |

```text
repeat release-ext   1135 tests x 5   19m23s (1162.72 s)   exit 0, 0 failure lines
repeat debug-ext     1135 tests x 5   22m12s (1331.80 s)   exit 0, 0 failure lines
TOTAL                                 3h43m41s
```

Every count **measured, not predicted**, and each reconciles against the
P17-CLI-001 run plus what this milestone added:

```text
suite    3949 -> 4006   +57:  16 unit.Structural* reference cases in process,
                              41 refmod.structural.* fresh-process tests, of
                              which one is the runner that builds, saves,
                              meshes, validates and solves all twelve
                              documents and is the others' FIXTURES_SETUP
repeat   1078 -> 1135   +57:  THE FILTER IS UNCHANGED. `unit\.Structural` and
                              `refmod\.` already catch the new names as
                              substrings. compile_fail stays out of the
                              repeat set for the reason every milestone
                              records: each case is a real compilation.
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git
revision", which is the always-run step that stamps the binaries; nothing
was recompiled or relinked, so the binaries that ran the tests are the ones
the build produced.

The harness's stderr holds 268 lines, all of them git's "CRLF will be
replaced by LF" notices from the fingerprint step over files whose
`.gitattributes` line endings differ from the working copy; none is from a
build or a test.

## The tree that was qualified is the tree that was committed

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175
include            c9696d26cd8583a90cdddd00a51ee654fc3ba72b
src                f3c1245dabbfda68008db3660f3d15a73e8e607f
tests              3568eb0e8d194387d9244ff02367fc155a9596c8
examples           a955cf6511ce715fcb95bc06ef82b877535869e6
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, taken by qualify.cmd from a scratch index at 12:44:40
and again at 16:28:21. **Six of the eight are byte-identical to the
P17-CLI-001 freeze** -- `apps`, `include`, `src`, `cmake`, `CMakeLists.txt`,
`CMakePresets.json` -- which is the fingerprint-level proof of the README's
claim that not one production file changed. Only `tests` and `examples`
moved, and those are the milestone.

The closeout commit's own subtree hashes are compared against these eight
values after the commit (`git write-tree --prefix=<dir>/` on the committed
index); the comparison is recorded in the commit's TODO.md entry.

## Where this ran, and what is identical

```text
MACHINE                the hasib machine, Windows 11 Pro 10.0.26300,
                       Ryzen 7 5800H, 16 logical cores, 32 GB -- the same
                       machine, toolchain, dependency prefix and caches
                       P17-CLI-001 qualified on
TOOLCHAIN              GCC 16.1.0, WinLibs POSIX UCRT r4; the CMake 4.4.1
                       and Ninja 1.13.2 bundled with it
DEPENDENCY PREFIX      C:/Users/hasib/bcdev/bettercad-deps/gnu-16-mingw-amd64,
                       built by deps/CMakeLists.txt from the hash-verified
                       archives: Qt 6.11.2, OCCT 8.0.1, zlib 1.3.1, Netgen
                       6.2.2604
BUILD ROOT             C:/Users/hasib/bcdev/bc-build, in the cache since
                       P17-CLI-001, with the three FETCHCONTENT_SOURCE_DIR_*
                       entries that milestone recorded
```

Nothing about the environment changed between the two milestones; the
deviations from the uqhas machine and their reasons are
P17-CLI-001's FREEZE.md and are restated in
`qualification/run-qualification.cmd`.

## The fresh-binary proof, per preset

```text
preset             absolute binary                                            bytes          sha256
debug-ext          C:/Users/hasib/bcdev/bc-build/debug-ext/bin/
                     bettercad-cli.exe                                        368 170 949    2ebb60b7f90f5aed...
                     bettercad_example_reference_models.exe                   372 836 927    fae161219cbecaaf...
                     bettercad_tests.exe                                    1 159 705 803    1cef3fac24005081...
release-ext        C:/Users/hasib/bcdev/bc-build/release-ext/bin/
                     bettercad-cli.exe                                         10 113 338    faf1ad2e9d9d21a4...
                     bettercad_example_reference_models.exe                     9 496 172    8c139ef17561098f...
                     bettercad_tests.exe                                       48 664 357    229ba19bdae7798e...
debug-shared-ext   C:/Users/hasib/bcdev/bc-build/debug-shared-ext/bin/
                     bettercad-cli.exe                                         41 310 182    11ec3a62a3b36045...
                     bettercad_example_reference_models.exe                    52 417 896    183b64021e43ac03...
                     bettercad_tests.exe                                      826 748 543    a0cc1b23ce6ba34b...
```

The runner and the CLI are `$<TARGET_FILE:...>` in every process test, so a
PATH lookup is impossible by construction; no installed copy of BetterCAD
exists on this machine. **Runtime library origin for debug-shared-ext:** the
ten `libbettercad_*.dll` modules sit beside the three executables in their
own `bin/`, put there by `bettercad_copy_runtime_dlls`, with the Qt, OCCT and
compiler runtime DLLs. The debug-ext CLI differs from P17-CLI-001's
(a3bfc89d...) only by the embedded git revision stamp, since no CLI source
changed; the release-ext CLI is byte-identical in size to that run's and
differs in the same stamp.

## Zero-match protection

| filter | minimum | discovered | executed | PASS |
| --- | --- | --- | --- | --- |
| `unit\.Structural(Patch\|AxialBar\|Cantilever\|PressureBlock\|GravityBlock\|Transformed\|LocalRefinement\|UnderConstrained\|StaleMesh\|Unresolved\|Reference)` | 12 | 16 | 16 | ✓ in all three presets |
| `refmod\.structural\.` | 30 | 41 | 41 | ✓ in all three presets |
| the whole suite | — | 4006 | 4006 | ✓ in all three presets |

Discovered with `ctest -N` after the run on every preset's tree; executed by
counting the `Passed` lines in each preset's ctest log.
`cli.mesh.zero-match-guard`, which proves the counter itself against an
impossible filter, passed in all three, and the impossible filter discovers
0 on this tree. The repeat filter's selected count is the harness's own,
1135, recorded in `qualification-times.txt`.

## Cross-preset equivalence

Three of the runner's documents, solved by each preset's own CLI after the
run, from each preset's own `cli-output/structural-refmod/` fixture:

```text
                                        bytes   sha256 (first 16)
structural_axial_bar.bcad                 701   03fbdf8e1d007efa   x 3 presets
structural_cantilever.bcad               1671   b914e776bbd3282e   x 3 presets
structural_transformed_placed.bcad        699   80ffabe53205efdc   x 3 presets
```

**Byte-identical across all three presets.** And the in-process suite, run
with each preset's own test binary after the harness, prints the same ten
solve fingerprints in all three -- RM-FEA-01 `0x367f5be15a7ad964`,
RM-FEA-02 `0xf7f8eac642d0dbdf`, RM-FEA-03 `0xdde09da083f54664`, RM-FEA-05
`0xd9bb91489e45f1d2`, RM-FEA-06 `0x7711566fd39cb23c` and `0xbf2b68f30eadd86`,
RM-FEA-07 `0x482f3f482809b849` and `0x1ffb082549c98bcc`, RM-FEA-04
`0x7080dbacde25e0ec`, RM-FEA-09 `0x665456e4bf19f4fe` -- over every
displacement, strain, stress, principal value, von Mises, hydrostatic stress
and reaction, bit for bit, under `-O0 -g`, under the optimiser and across a
DLL boundary:

```text
PRESET            REFERENCE SUITE               CLI FIXTURES
debug-ext         16 cases, 133 872 assertions  41 / 41
release-ext       16 cases, 133 872 assertions  41 / 41
debug-shared-ext  16 cases, 133 872 assertions  41 / 41
```

Recorded as a measurement, not promoted to a general guarantee.

## Determinism

The repeat stages: 1135 tests, five times each, in release-ext and in
debug-ext, exit 0 both with no failure line. In process,
`StructuralReference_IsDeterministicOverRepeatedSolves` requires one
fingerprint over five fresh documents of each of six models; the runner's
twelve documents give the same outcome, and the two refusals the same
diagnostic text, over three attempts. Nothing in any payload is a
timestamp, a path, an address or a duration, which is why the byte
comparisons above are available at all.
