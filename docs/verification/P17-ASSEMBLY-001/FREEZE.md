# P17-ASSEMBLY-001 — qualification freeze

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
2  the new tests under --repeat              ctest -R "StructuralSystem_"
                                             --repeat until-fail:5 -j 8
                                             26/26, 100%, 179.94 s
                                             (130 executions)
3  the shared build                          debug-shared-ext built clean and
                                             32/32 of the assembly selection
                                             passed, 56.70 s. Only this preset
                                             catches a DLL-boundary defect
4  export macros on header-defined
   constexpr / inline                        audited: every
                                             BETTERCAD_STRUCTURAL_EXPORT in
                                             StructuralSystem.hpp sits on a
                                             class or on a function DEFINED IN
                                             THE .cpp. `AssemblySource` is a
                                             plain struct with no out-of-line
                                             member and carries none
```

Check 2 is safe to repeat because each test case builds its own fixture inside
the `TEST_CASE`: `ctest --repeat` reruns a test back to back without rerunning
fixtures, so a test sharing mutable input would pass once and then fail.

## Candidate

```text
FROZEN            2026-10-08 19:22:54
HEAD              3b471a940baec875e4e7dd806cdd6efbcbc3a674
candidate tree    6d36b39e1d87bba04eee957a0b93da3ff49bf2dd

apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           f9f4febbc280774f8f49d67a0d97bde8c23494c0
src               d792886fad0e75f5d2157a2b948c818af03945c2
tests             06d87e4fba6835c3cd933a7347f54afc15087484
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
include   a4f21ca8 -> f9f4febb    one new public header
src       61b9a93e -> d792886f    one new source, plus one line in
                                  src/structural/CMakeLists.txt
tests     fa0840c6 -> 06d87e4f    two new suites, one new compile-fail group,
                                  two registration lines
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   UNCHANGED
```

### No predecessor production file was modified

```text
git status --porcelain -- src include
   M src/structural/CMakeLists.txt        (+1 line, registering the new source)
  ?? include/bettercad/structural/StructuralSystem.hpp
  ?? src/structural/StructuralSystem.cpp
```

So brief section 146's requalification requirement has nothing to act on:
P17-ELEM-001, P17-LOAD-001, P17-DOF-001 and P17-BC-001 are untouched, and
their suites pass unchanged inside the full unfiltered run and inside the
repeat selection.

```text
affected milestone     shared code changed?   requalification
---------------------------------------------------------------
P17-ELEM-001           NO                     not owed; 29 tests pass in all
                                              three presets and 5x in two
P17-LOAD-001           NO                     not owed; 23 tests, likewise
P17-DOF-001            NO                     not owed; 31 tests, likewise
P17-BC-001             NO                     not owed; 32 tests, likewise
P16                    NO                     not owed
```

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-BC-001's, and `qualify.cmd` unchanged since
P16-SIZE-001 — **eighteen milestones in a row**. `verify-harness.cmd` is its
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
repeat release-ext  660 selected, exit 0
repeat debug-ext    660 selected, exit 0
```

Each preset was configured, had **every build output removed**, rebuilt with
warnings as errors, and tested only after a successful build.

```text
started   2026-10-08 19:22:55
finished  2026-10-08 22:33:43
elapsed   3 h 11 min
```

## Results

```text
preset            tests        result   time       objects  warnings
---------------------------------------------------------------------
debug-ext         3615/3615    100%     1397.94 s      624         0
release-ext       3615/3615    100%     1369.88 s      624         0
debug-shared-ext  3615/3615    100%     1407.80 s      624         0

repeat release-ext  660 x until-fail:5   100%     833.77 s
repeat debug-ext    660 x until-fail:5   100%     885.94 s
```

```text
3583 at P17-BC-001  ->  3615 here, +32
    20  tests/structural/StructuralSystemTests.cpp
     6  tests/reference/StructuralSystemReferenceTests.cpp
     6  tests/compile_fail/StructuralSystemMisuse.cpp
```

### The selection was counted, not assumed

```text
unfiltered, each preset   3615, and this milestone's tests were found INSIDE
                          each preset's own ctest log:
                              StructuralSystem_         26  (52 log lines,
                                                            Start + result)
                              compile_fail.structasm     6  (12 log lines)
                          identical in all three

repeat, each preset       660 selected -- the harness records the number itself
                          and a zero-match filter is a FAILED stage twice over
                          (noTestsAction=error, and the guard above the loop)
                          and inside that 660:
                              StructuralSystem_         26
                              StructuralBC_             32
                              StructuralLoad_           23
                              Tet4Element               29
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

### Warning audit

```text
0 warnings over 624 objects, in each of the three presets
```

`-Werror` is on, so a warning is a failed build rather than a line in a log.
The categories brief section 145 names were the ones to watch here, and
`-Wconversion -Wsign-conversion -Wdouble-promotion -Werror` is what makes
their absence a result rather than a claim:

```text
sparse index narrowing          the CSR index is std::uint64_t throughout and
                                every conversion to a container index is an
                                explicit static_cast
signed/unsigned conversions     none; the Eigen::Index conversions in the TEST
                                oracles are explicit too
uninitialised storage           `values` is assigned 0.0 and `force` is
                                assigned 0.0 before either is written
implicit integer truncation     none
unused assembly branches        none; -Wunused is on
Eigen storage-order warnings    not applicable -- the structural library links
                                no Eigen at all
```

## Qualified tree == committed tree

```text
component hashes before the first build   as listed above
component hashes after the last test run  IDENTICAL, all eight
```

What moved after the freeze: `docs/verification/P17-ASSEMBLY-001/`,
`docs/architecture/decisions/ADR-038-*.md`, `TODO.md` and `ROADMAP.md` — all
documentation, none of it inside the fingerprint, which is the project's
existing policy for documentation that cannot affect the executable or the
tests.

## What is NOT claimed

```text
performance             not measured as a gate. The large-mesh case took 32 s
                        and that number is informational: a wall-clock bound
                        would be a bound on this machine. No baseline was taken
                        and no speedup is claimed
bitwise cross-preset
  equality              NOT claimed. The tests assert the properties -- Ndof,
                        nnz, the ordered coordinate pattern, the oracle
                        agreement, the symmetry bound, the rigid-mode
                        residuals -- in each preset independently. No preset's
                        `values` array was carried to another run and compared,
                        so no bitwise claim is recorded
peak memory             not instrumented. The storage figures in
                        DETERMINISM.md (1.56 MiB against 49.61 MiB dense) are
                        the measurement that was taken
P17 as a whole          not qualified. P17-QUAL-001 is a separate milestone
a solve                 nothing is solved anywhere in this milestone
```
