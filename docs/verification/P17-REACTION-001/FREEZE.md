# P17-REACTION-001 — qualification freeze

```text
RESULT: PASS
```

## The five pre-freeze checks

Run **before** the expensive one, which is the discipline P15 paid for three
times: all three of its voided qualifications came from doing a cheap check
after an expensive one and finding something the freeze had already
invalidated.

```text
1  git diff --check                        clean, 0 lines, over the eight
                                           fingerprinted paths
2  export macros on header-defined
   constexpr / inline                      audited. Every
                                           BETTERCAD_STRUCTURAL_EXPORT in
                                           StructuralReaction.hpp sits on a
                                           function DEFINED IN THE .cpp, or on
                                           `SupportReactions`, which has
                                           out-of-line members. The five
                                           aggregates -- SupportReaction,
                                           RestraintReaction, ForceBalance,
                                           MomentBalance,
                                           EquilibriumTolerance -- carry none.
                                           A grep for the macro on the same
                                           line as constexpr or inline returns
                                           nothing
3  the new tests under --repeat             ctest -R "StructuralReaction_|
                                           compile_fail.structreaction|
                                           StructuralBC_"
                                           --repeat until-fail:5 -j 8
                                           59/59, 100%, 295.33 s
                                           (295 executions). P17-BC is IN this
                                           selection deliberately: its shared
                                           code changed
4  the shared build                         debug-shared-ext built clean and
                                           74/74 of the reaction,
                                           compile-fail, BC and architecture
                                           selection passed, 66.33 s. Only
                                           this preset catches a DLL-boundary
                                           defect
5  every line of run-qualification.cmd
   accounted for                            grep -nvE "^(rem|set |call |echo |
                                           setlocal|endlocal|@echo)" returns
                                           nothing. Added to the procedure by
                                           P17-POST-001 after it found bare
                                           OLD_END lines in two earlier
                                           milestones' copies
```

## Candidate

```text
FROZEN            2026-10-10 14:49:00
HEAD              5682b98e81cfcdcf268b8313e99c05cad4d58d31
staged tree       46a3d29435a9156564a8d4b3b430620fb64bad76

apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           f2a6fb7ec499f27d4004d2b1541490542167e915
src               1a0fcf71f151b925c8f956161456502efc35ca3a
tests             0ab21f75c5b22983934e40c004bc4eb237b4844a
examples          75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

The **eight component hashes** are the durable record, not the tree id: the
tree id is a function of the eight paths *plus* the base HEAD, and moves when
documentation lands. The harness read them itself, before the first build and
again after the last test run.

```text
include   a1a6ba1c -> f2a6fb7e    one new public header, plus the
                                  RestraintResolution field in
                                  StructuralConstraints.hpp
src       bba30676 -> 1a0fcf71    one new source, the registration in
                                  src/structural/CMakeLists.txt, and the
                                  retained DofIndex list in
                                  StructuralConstraints.cpp
tests     14ae02cf -> 0ab21f75    two new suites, one new compile-fail group,
                                  three registration edits
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   UNCHANGED
```

## Predecessor requalification

```text
git status --porcelain -- src include
   M src/structural/CMakeLists.txt                       the new source only
   M src/structural/StructuralConstraints.cpp            P17-BC-001 code
   M include/bettercad/structural/StructuralConstraints.hpp   P17-BC-001 code
  ?? include/bettercad/structural/StructuralReaction.hpp
  ?? src/structural/StructuralReaction.cpp
```

```text
milestone          shared code changed?   requalification
---------------------------------------------------------------------------
P17-BC-001         YES                    OWED AND PAID. 32 tests before the
                                          freeze (32/32), 32 inside EVERY
                                          preset's unfiltered run -- counted
                                          from the logs -- and 32 x 5 in the
                                          two repeat presets
P17-DOF-001        NO                     not owed; in the repeat set anyway
P17-ELEM-001       NO                     not owed; likewise
P17-LOAD-001       NO                     not owed; likewise
P17-ASSEMBLY-001   NO                     not owed; likewise
P17-SOLVE-001      NO                     StructuralSolve.cpp UNTOUCHED. This
                                          milestone CONSUMES
                                          fullResidual() and computes no
                                          second residual
P17-POST-001       NO                     not owed; 41 tests, likewise
P16                NO                     not owed
```

### What changed in P17-BC-001, and why

`RestraintResolution` gained `std::vector<DofIndex> constrained` — the degrees
of freedom that restraint resolved to, ascending and unique within it
(ADR-041, Decision 3).

Per-restraint reaction inspection is a **required** checklist item and counts
cannot attribute a reaction. The only alternative was to re-resolve each
restraint's target after the solve and *assume* the geometry mapping
reproduced the same set — which is how a source mismatch hides.

The change is **additive and keeps a value already computed**: that loop
already called `numbering.indexOf` for every degree of freedom it resolved,
verified the answer, and discarded the index. `degreesOfFreedom` is now
`constrained.size()`, which equals the previous `nodes * components.count()`
because a restraint's nodes are unique and its components are a mask.

### No shared test infrastructure changed

`tests/architecture/CheckLayering.cmake` is byte-identical to P17-POST-001's.
`architecture` is in the repeat set because a new public header went into an
existing module; all 15 of its tests pass in all three presets and 5x in two.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-POST-001's, and `qualify.cmd` unchanged since
P16-SIZE-001 — **twenty-one milestones in a row**. `verify-harness.cmd` is its
regression and is meant to be run whenever `qualify.cmd` is edited; it was not
edited, and the hash above is the evidence.

`QUALIFY_REPEAT` is expanded as `!REPEAT!` and never `%REPEAT%`: a `%VAR%`
holding `|`, `(` or `)` is substituted when the `for` block is **parsed** and
closes the block early, which kills the run after every preset has already
passed.

The exact invocation is committed beside this file as
[qualification/run-qualification.cmd](qualification/run-qualification.cmd).

### One comment in it was corrected AFTER the run

That file's `unit\.Structural` note first read "this milestone's 27 (21 unit +
6 reference)". Both halves were wrong: 27 is the total **including** the six
compile-failure cases, and the unit/reference split is **17 / 4**.

```text
affected the run?   NO. The filter is `unit\.Structural` and selects whatever
                    exists; it selected 748 tests, and every count in this
                    document was read back out of the run's own ctest logs
                    rather than from that comment
why not fixed
  during the run?   cmd.exe reads a batch file INCREMENTALLY, BY BYTE OFFSET.
                    Editing it mid-run moves everything after the cursor --
                    which cost INFRA-VIEWER-001 a four-hour qualification that
                    had already PASSED
corrected           after `qualify.cmd exit 0`, with the correction recorded
                    in the file itself
```

It is recorded here rather than silently fixed because a quoted figure is an
unverified figure until it is derived from both ends, and this one was not.

## Stages

```text
17 stages, 0 failed, qualify.cmd exit 0

debug-ext         configure 0   clean 0   build 0   no-op 0   ctest 0
release-ext       configure 0   clean 0   build 0   no-op 0   ctest 0
debug-shared-ext  configure 0   clean 0   build 0   no-op 0   ctest 0
repeat release-ext  748 selected, exit 0
repeat debug-ext    748 selected, exit 0
```

Each preset was configured, had **every build output removed**, rebuilt with
warnings as errors, and tested only after a successful build.

```text
started   2026-10-10 14:49:17
finished  2026-10-10 18:00:57
elapsed   3 h 11 min
```

```text
stage                      from      to        duration
----------------------------------------------------------
debug-ext build            14:49:28  15:17:18  27 min 50 s
debug-ext ctest            15:17:19  15:41:52  24 min 33 s
release-ext build          15:42:01  16:15:29  33 min 28 s
release-ext ctest          16:15:30  16:39:04  23 min 34 s
debug-shared-ext build     16:39:12  17:05:38  26 min 26 s
debug-shared-ext ctest     17:05:39  17:30:19  24 min 40 s
repeat release-ext         17:30:20  17:45:01  14 min 41 s
repeat debug-ext           17:45:03  18:00:55  15 min 52 s
```

## Results

```text
preset            tests        result   objects  warnings
-----------------------------------------------------------
debug-ext         3721/3721    100%         636         0
release-ext       3721/3721    100%         636         0
debug-shared-ext  3721/3721    100%         636         0

repeat release-ext  748 x until-fail:5   100%
repeat debug-ext    748 x until-fail:5   100%
```

```text
3694 at P17-POST-001  ->  3721 here, +27
    17  tests/structural/StructuralReactionTests.cpp
     4  tests/reference/StructuralReactionReferenceTests.cpp
     6  tests/compile_fail/StructuralReactionMisuse.cpp
```

### The selection was counted, not assumed

```text
unfiltered, each preset   3721, and this milestone's tests were found INSIDE
                          each preset's own ctest log:
                              StructuralReaction_        21
                              compile_fail.structreaction 6
                              StructuralBC_              32   <- the
                                                              requalification
                              architecture               15
                          identical in all three

repeat, each preset       748 selected -- the harness records the number
                          itself, and a zero-match filter is a FAILED stage
                          twice over (noTestsAction=error, and the guard above
                          the loop). Inside that 748:
                              StructuralReaction_        21
                              StructuralBC_              32
                              StructuralPost_            41
                              StructuralSolve_           25
```

P17-DOF-001 found a real instance of the opposite failure — an inherited
filter that selected 751 tests and covered only 17 of that milestone's 31 — so
the counts above are derived from inside the selection rather than from
reading the filter.

### No-op rebuild

```text
rebuild-debug-ext.log         0 Building or Linking lines
rebuild-release-ext.log       0
rebuild-debug-shared-ext.log  0
```

Which is what proves the binaries CTest ran are the ones the clean build just
produced. P17-POST-001 has the cautionary tale: an orphaned probe harness left
a 0-byte `bettercad_tests.exe` that ninja considered up to date, and the next
`ctest` reported failures from a mutant binary.

### Fresh binary discovery

```text
preset            build root                                        test binary
--------------------------------------------------------------------------------
debug-ext         C:/Users/uqhas/AppData/Local/bc-build/debug-ext        bin/bettercad_tests.exe
release-ext       C:/Users/uqhas/AppData/Local/bc-build/release-ext      bin/bettercad_tests.exe
debug-shared-ext  C:/Users/uqhas/AppData/Local/bc-build/debug-shared-ext bin/bettercad_tests.exe
```

Each preset has its own build root outside OneDrive, each was emptied before
its build, and `ctest --preset` runs that root's own binary.
`debug-shared-ext` additionally loads that root's own DLLs, which is the preset
that catches an export defect.

### Warning audit

```text
0 warnings over 636 objects, in each of the three presets
```

`-Werror` is on, so a warning is a failed build rather than a line in a log.
The categories brief section 184 names were the ones to watch:

```text
Force3D / Moment3D unit conversion   impossible to get silently wrong:
                                     Quantity has no implicit conversion, and
                                     four compile-failure cases enforce that a
                                     force is not a torque and a norm is not a
                                     number
cross-product operand order          one cross product in the module, core's
                                     momentOf, whose signature takes a
                                     RELATIVE lever. -Wunused caught nothing
                                     here, and the mutation that reverses it
                                     is killed by 16 tests
signed/unsigned DOF indexing         -Wsign-conversion is on; every row index
                                     is an explicit static_cast from
                                     DofIndex::ValueType
duplicate restraint provenance       -Wunused is on, and it DID fire during
                                     development on an orphaned local in a
                                     mutation -- which is how two weak probes
                                     were identified and re-run
unused overlap branch                none; the shared bucket is reached by the
                                     overlapping fixture
narrowing                            -Wconversion is on
nonfinite checks                     present and reachable through the
                                     balances
```

## Qualified tree == committed tree

```text
component hashes before the first build   as listed above
component hashes after the last test run  IDENTICAL, all eight
```

What moved after the freeze: `docs/verification/P17-REACTION-001/`,
`docs/architecture/decisions/ADR-041-*.md`, the corrected comment in this
milestone's own `run-qualification.cmd`, `TODO.md` and `ROADMAP.md` — all
documentation, none of it inside the fingerprint, which is the project's
existing policy for documentation that cannot affect the executable or the
tests.

## What is NOT claimed

```text
performance             not measured as a gate. The large-mesh reaction
                        recovery sits inside a 46 s test that also meshes and
                        solves; no recovery timing was isolated, no baseline
                        was taken and no speedup is claimed
bitwise cross-preset
  equality              NOT claimed. The tests assert the PROPERTIES -- the
                        partition formula, the analytical resultants, the
                        exact fully-constrained identity, both balances -- in
                        each preset independently. WITHIN a run, five repeats
                        are bitwise identical and that IS claimed
peak memory             not instrumented
a predicted two-support
  split                 not claimed; the exact statics is. See
                        ANALYTICAL_REFERENCES.md case 6
analytical agreement on
  a CURVED pressure face not claimed; that comparison would measure
                        P17-LOAD's discretisation rather than equilibrium
continuum accuracy      not this milestone's subject
P17 as a whole          not qualified. P17-QUAL-001 is a separate milestone
```
