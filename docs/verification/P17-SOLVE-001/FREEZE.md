# P17-SOLVE-001 — qualification freeze

```text
RESULT: PASS
```

## The four pre-freeze checks

Run **before** the expensive one, which is the discipline P15 paid for three
times: all three of its voided qualifications came from doing a cheap check
after an expensive one and finding something the freeze had already
invalidated.

```text
1  git diff --check                          clean, 0 lines, over the eight
                                             fingerprinted paths
2  the new tests under --repeat              ctest -R "StructuralSolve_"
                                             --repeat until-fail:5 -j 8
                                             25/25, 100%, 212.23 s
                                             (125 executions)
3  the shared build                          debug-shared-ext built clean and
                                             46/46 of the solve, compile-fail
                                             and architecture selection passed,
                                             55.24 s. Only this preset catches
                                             a DLL-boundary defect
4  export macros on header-defined
   constexpr / inline                        audited: every
                                             BETTERCAD_STRUCTURAL_EXPORT in
                                             StructuralSolve.hpp sits on a
                                             class or on a function DEFINED IN
                                             THE .cpp. SolverSettings,
                                             ResidualMetrics and
                                             SymmetricSolution are plain
                                             structs with no out-of-line member
                                             and carry none
```

## Candidate

```text
FROZEN            2026-10-09 00:48:59
HEAD              bd098c07959088f61bb215ca41cb753e38b91bc0
candidate tree    4e41fdd2611437f952d1134fa3310918a007cb3c

apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           7bfbf016361c22b0ff19fb1f8423da138f8d07be
src               b1010cf32b84f5137f09e1edc8e69f3c7bdfd710
tests             de6bf9d8b7ce39731bf53b6fb018d085f9c25f2d
examples          75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

The **eight component hashes** are the durable record, not the tree id: the
tree id is a function of the eight paths *plus* the base HEAD, and moves when
documentation lands. The harness read them itself, before the first build and
again after the last test run, and they are identical in both readings and
identical to the list above.

```text
include   f9f4febb -> 7bfbf016    one new public header
src       d792886f -> b1010cf3    one new source, plus the Eigen PRIVATE link
                                  and the registration in
                                  src/structural/CMakeLists.txt
tests     06d87e4f -> de6bf9d8    two new suites, one new compile-fail group,
                                  rule 7 and its fixture in
                                  tests/architecture/, three registration edits
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   UNCHANGED
```

### No predecessor production file was modified

```text
git status --porcelain -- src include
   M src/structural/CMakeLists.txt        the Eigen PRIVATE link + the new
                                          source, and nothing else
  ?? include/bettercad/structural/StructuralSolve.hpp
  ?? src/structural/StructuralSolve.cpp
```

So brief section 150's requalification requirement has nothing to act on:
P17-DOF-001, P17-BC-001 and P17-ASSEMBLY-001 are untouched, and their suites
pass unchanged inside the full unfiltered run and inside the repeat selection.

```text
affected milestone     shared code changed?   requalification
-----------------------------------------------------------------
P17-DOF-001            NO                     not owed; 31 tests pass in all
                                              three presets and 5x in two
P17-BC-001             NO                     not owed; 32 tests, likewise
P17-ASSEMBLY-001       NO                     not owed; 26 tests, likewise
P17-ELEM-001           NO                     not owed; 29 tests, likewise
P17-LOAD-001           NO                     not owed; 23 tests, likewise
P16                    NO                     not owed
```

### One piece of shared TEST infrastructure did change

`tests/architecture/CheckLayering.cmake` gained **rule 7**: Eigen headers may
be included from `src/` only, never from a public header (ADR-039). It was
added because this milestone made that invariant load-bearing by admitting
Eigen to a third module, and because no earlier rule could cover it — rule 1
keys on the `.hxx` extension and Eigen's entry headers have no extension at
all.

```text
affected                 every module, since the checker runs over include/,
                         src/, apps/ and examples/ in one pass
requalification          `architecture` is IN the repeat set for that reason,
                         and all 15 architecture tests pass in all three
                         presets and 5x in two
self-test                architecture.checker.eigen-public-header, on a fixture
                         holding BOTH a violation and a permitted use, so the
                         expected count of ONE proves the rule fires on the
                         right one
```

Writing that self-test found two defects in the rule immediately — a message
split across two `list(APPEND)` elements, and a semicolon inside it that CMake
treats as a list separator — which is why the fixture exists rather than a
review.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-ASSEMBLY-001's, and `qualify.cmd` unchanged since
P16-SIZE-001 — **nineteen milestones in a row**. `verify-harness.cmd` is its
regression and is meant to be run whenever `qualify.cmd` is edited; it was not
edited, and the hash above is the evidence.

`QUALIFY_REPEAT` is expanded as `!REPEAT!` and never `%REPEAT%`: a `%VAR%`
holding `|`, `(` or `)` is substituted when the `for` block is **parsed** and
closes the block early, which kills the run after every preset has already
passed.

The exact invocation is committed beside this file as
[qualification/run-qualification.cmd](qualification/run-qualification.cmd),
with every term of the repeat filter justified in its header.

## Stages

```text
17 stages, 0 failed, qualify.cmd exit 0

debug-ext         configure 0   clean 0   build 0   no-op 0   ctest 0
release-ext       configure 0   clean 0   build 0   no-op 0   ctest 0
debug-shared-ext  configure 0   clean 0   build 0   no-op 0   ctest 0
repeat release-ext  686 selected, exit 0
repeat debug-ext    686 selected, exit 0
```

Each preset was configured, had **every build output removed**, rebuilt with
warnings as errors, and tested only after a successful build.

```text
started   2026-10-09 00:49:04
finished  2026-10-09 04:04:56
elapsed   3 h 16 min
```

## Results

```text
preset            tests        result   time       objects  warnings
---------------------------------------------------------------------
debug-ext         3647/3647    100%     1493.73 s      628         0
release-ext       3647/3647    100%     1404.71 s      628         0
debug-shared-ext  3647/3647    100%     1485.01 s      628         0

repeat release-ext  686 x until-fail:5   100%     874.08 s
repeat debug-ext    686 x until-fail:5   100%     919.89 s
```

```text
3615 at P17-ASSEMBLY-001  ->  3647 here, +32
    19  tests/structural/StructuralSolveTests.cpp
     6  tests/reference/StructuralSolveReferenceTests.cpp
     6  tests/compile_fail/StructuralSolveMisuse.cpp
     1  architecture.checker.eigen-public-header
```

### The selection was counted, not assumed

```text
unfiltered, each preset   3647, and this milestone's tests were found INSIDE
                          each preset's own ctest log:
                              StructuralSolve_          25  (50 log lines,
                                                             Start + result)
                              compile_fail.structsolve   6  (12 log lines)
                              architecture.*            15  (30 log lines)
                          identical in all three

repeat, each preset       686 selected -- the harness records the number itself
                          and a zero-match filter is a FAILED stage twice over
                          (noTestsAction=error, and the guard above the loop)
                          and inside that 686:
                              StructuralSolve_          25
                              StructuralSystem_         26
                              StructuralBC_             32
                              StructuralLoad_           23
                              Tet4Element               29
                              architecture              15
```

P17-DOF-001 found a real instance of the opposite failure — an inherited filter
that selected 751 tests and covered only 17 of that milestone's 31 — so the
counts above are derived from inside the selection rather than from reading the
filter.

### No-op rebuild

```text
rebuild-debug-ext.log         0 Building or Linking lines
rebuild-release-ext.log       0
rebuild-debug-shared-ext.log  0
```

Which is what proves the binaries CTest ran are the ones the clean build just
produced, and not a stale artifact from an earlier configuration.

### Fresh binary discovery

```text
preset            build root                                        test binary
--------------------------------------------------------------------------------
debug-ext         C:/Users/uqhas/AppData/Local/bc-build/debug-ext        bin/bettercad_tests.exe
release-ext       C:/Users/uqhas/AppData/Local/bc-build/release-ext      bin/bettercad_tests.exe
debug-shared-ext  C:/Users/uqhas/AppData/Local/bc-build/debug-shared-ext bin/bettercad_tests.exe
```

Each preset has its own build root outside OneDrive, each was emptied before
its build, and `ctest --preset` runs that root's own binary. `debug-shared-ext`
additionally loads that root's own DLLs, which is the preset that catches an
export defect.

### Warning audit

```text
0 warnings over 628 objects, in each of the three presets
```

`-Werror` is on, so a warning is a failed build rather than a line in a log.
The categories brief section 149 names were the ones to watch, and
`-Wconversion -Wsign-conversion -Wdouble-promotion -Wunused -Werror` is what
makes their absence a result rather than a claim:

```text
sparse index narrowing             every conversion between BetterCAD's
                                   uint64_t CSR index, std::size_t and Eigen's
                                   StorageIndex is an explicit static_cast
DofIndex <-> FreeEquationIndex     ADR-037 made them different types, so a
                                   conversion does not compile; the one
                                   1-based-to-row conversion is confined to
                                   extractFreeSystem
unused solver-status branch        none; -Wunused is on, and it caught a stray
                                   `scale` during development
uninitialised displacement         the full vector is assigned 0.0 before any
                                   free row is written
library deprecation                none from Eigen 5.0.1 at this standard
licence / dependency build         none; Eigen is header-only and already
                                   fetched
```

## Qualified tree == committed tree

```text
component hashes before the first build   as listed above
component hashes after the last test run  IDENTICAL, all eight
```

What moved after the freeze: `docs/verification/P17-SOLVE-001/`,
`docs/architecture/decisions/ADR-039-*.md`, `TODO.md` and `ROADMAP.md` — all
documentation, none of it inside the fingerprint, which is the project's
existing policy for documentation that cannot affect the executable or the
tests.

## What is NOT claimed

```text
performance             not measured as a gate. The large-mesh solve took
                        48.6 s and that number is informational: a wall-clock
                        bound would be a bound on this machine. No baseline
                        was taken and no speedup is claimed
bitwise cross-preset
  equality              NOT claimed. The tests assert the properties -- the
                        classification, the solution against its oracle, the
                        residual, the pivot ratio, the exact zeros -- in each
                        preset independently. No preset's displacement vector
                        was carried to another run and compared, so no bitwise
                        claim is recorded. WITHIN a run, five repeats are
                        bitwise identical and that IS claimed
peak memory             not instrumented. The storage figures in the README
                        (Kff 1.26 MiB against a dense 38.62 MiB) are the
                        measurement that was taken
continuum accuracy      not claimed beyond the one-sided axial bound.
                        P17-REFMOD-001 owns it
P17 as a whole          not qualified. P17-QUAL-001 is a separate milestone
reactions               retained, not aggregated. P17-REACTION-001 owns them
```
