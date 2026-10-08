# P17-BC-001 — qualification freeze

```text
RESULT: PASS
```

## The four pre-freeze checks

Run **before** the expensive one, which is the discipline P15 paid for three
times: all three of its voided qualifications came from doing a cheap check
after an expensive one and finding something the freeze had already invalidated.

```text
1  git diff --check                          clean, 0 lines
2  the new tests under --repeat              ctest -R "StructuralBC_"
                                             --repeat until-fail:5 -j 8
                                             32/32, 100%, 192.19 s
                                             (160 executions)
3  the shared build                          debug-shared-ext built clean and
                                             61/61 of the BC + LOAD + structbc
                                             selection passed, 91.82 s.
                                             Only this preset catches a
                                             DLL-boundary defect
4  export macros on header-defined
   constexpr / inline                        audited: every
                                             BETTERCAD_STRUCTURAL_EXPORT in the
                                             three new headers sits on a class
                                             or on a function DEFINED IN A .cpp.
                                             RestraintComponents, which is
                                             entirely header-defined constexpr,
                                             carries none -- the macro would
                                             expand to dllimport and fail only
                                             in debug-shared-ext
```

Check 2 is safe to repeat because each test case builds its own fixture inside
the `TEST_CASE`: `ctest --repeat` reruns a test back to back without rerunning
fixtures, so a test sharing mutable input would pass once and then fail.

## Candidate

```text
FROZEN            2026-10-08 09:25:54
HEAD              caf0f1e52ec82a8daf39711487bcf584764d3ace
candidate tree    60e01a988ee8302740bfa8f147e28fc0e888c74e

apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           a4f21ca89058bd36fa293ae43594956f1f478695
src               61b9a93e7a4bed0f77ea07380009db391f6ff749
tests             fa0840c6e42f698d87bcccf1f7a95f261720872f
examples          75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

The **eight component hashes** are the durable record, not the tree id: the
tree id is a function of the eight paths *plus* the base HEAD, and moves when
documentation lands. The harness read them itself, before the first build and
again after the last test run, and they are identical in both readings and
identical to the line above.

```text
include   38b76d28 -> a4f21ca8    three new public headers, one changed
src       4d5eb402 -> 61b9a93e    two new sources, one changed, CMakeLists
tests     25903e4f -> fa0840c6    two new suites, one new compile-fail group,
                                  two changed
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   UNCHANGED
```

Nothing outside `src/structural/`, `include/bettercad/structural/` and `tests/`
was touched, which is what narrowed the repeat set relative to P17-LOAD-001's —
that milestone reached into `core`.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-LOAD-001's, and `qualify.cmd` unchanged since
P16-SIZE-001 — **seventeen milestones in a row**. `verify-harness.cmd` is its
regression and is meant to be run whenever `qualify.cmd` is edited; it was not
edited, and the hash above is the evidence.

`QUALIFY_REPEAT` is expanded as `!REPEAT!` and never `%REPEAT%`: a `%VAR%`
holding `|`, `(` or `)` is substituted when the `for` block is **parsed** and
closes the block early, which kills the run after every preset has already
passed — the most expensive way to lose a gate.

The exact invocation is committed beside this file as
[qualification/run-qualification.cmd](qualification/run-qualification.cmd), with
the blast radius and every term of the repeat filter justified in its header.

## Stages

```text
17 stages, 0 failed, qualify.cmd exit 0

debug-ext         configure 0   clean 0   build 0   no-op 0   ctest 0
release-ext       configure 0   clean 0   build 0   no-op 0   ctest 0
debug-shared-ext  configure 0   clean 0   build 0   no-op 0   ctest 0
repeat release-ext  634 selected, exit 0
repeat debug-ext    634 selected, exit 0
```

Each preset was configured, had **every build output removed**, rebuilt with
warnings as errors, and tested only after a successful build.

```text
started   2026-10-08 09:25:55
finished  2026-10-08 12:36:02
elapsed   3 h 10 min
```

## Results

```text
preset            tests        result   time      objects  warnings
--------------------------------------------------------------------
debug-ext         3583/3583    100%     1378.80 s     620         0
release-ext       3583/3583    100%     1340.71 s     620         0
debug-shared-ext  3583/3583    100%     1409.60 s     620         0

repeat release-ext  634 x until-fail:5   100%    788.29 s
repeat debug-ext    634 x until-fail:5   100%    865.15 s
```

```text
3545 at P17-LOAD-001  ->  3583 here, +38
    24  tests/structural/StructuralBCTests.cpp
     8  tests/reference/StructuralBCReferenceTests.cpp
     6  tests/compile_fail/StructuralRestraintMisuse.cpp
```

### The selection was counted, not assumed

```text
unfiltered, each preset   3583, and this milestone's tests were found INSIDE
                          each preset's own ctest log:
                              StructuralBC_            32
                              compile_fail.structbc     6
                              StructuralLoad_          23   (the requalification)
                          identical in all three

repeat, each preset       634 selected -- the harness records the number itself
                          and a zero-match filter is a FAILED stage twice over
                          (noTestsAction=error, and the guard above the loop)
                          and inside that 634:
                              StructuralBC_            32
                              StructuralLoad_          23
                              StructuralDof + sets     31
                              StructuralData_          21
                              Tet4Element              29
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
0 warnings over 620 objects, in each of the three presets
```

`-Werror` is on, so a warning is a failed build rather than a line in a log.
The categories brief section 153 names were the ones to watch here —
enum/bitmask conversions in `RestraintComponents`, signed/unsigned narrowing
between `NodeId`, `DofIndex` and `std::size_t`, duplicate-insertion handling,
and unused diagnostic paths — and `-Wconversion -Wsign-conversion` with
`-Werror` is what makes their absence a result rather than a claim.

## Qualified tree == committed tree

```text
component hashes before the first build   as listed above
component hashes after the last test run  IDENTICAL, all eight
```

What moved after the freeze: `docs/verification/P17-BC-001/`,
`docs/verification/P17-LOAD-001/README.md` (the dated forward note), `TODO.md`
and `ROADMAP.md` — all documentation, none of it inside the fingerprint, which
is the project's existing policy for documentation that cannot affect the
executable or the tests.

## What is NOT claimed

```text
performance       not measured, and no claim is made. This milestone adds no
                  hot path: resolving a restraint is one mapping lookup, one
                  sorted node list and k N index lookups. No baseline was taken
                  and none is quoted
P17 as a whole    not qualified. P17-QUAL-001 is a separate milestone
a solve           nothing is solved anywhere in this milestone
```
