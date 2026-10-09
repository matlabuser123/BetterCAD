# P17-POST-001 — qualification freeze

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
                                           StructuralPost.hpp sits on a
                                           function DEFINED IN THE .cpp, or on
                                           `RecoveredFields`, which has
                                           out-of-line members. The three
                                           constexpr free functions --
                                           tensorOf x2 and hydrostaticStress
                                           -- and the five aggregates carry
                                           NONE. A grep for the macro on the
                                           same line as constexpr or inline
                                           returns nothing
3  the new tests under --repeat             ctest -R "StructuralPost_|
                                           compile_fail.structpost"
                                           --repeat until-fail:5 -j 8
                                           47/47, 100%, 245.74 s
                                           (235 executions)
4  the shared build                         debug-shared-ext built clean and
                                           62/62 of the post, compile-fail and
                                           architecture selection passed,
                                           55.40 s. Only this preset catches a
                                           DLL-boundary defect
5  NEW: every line of run-qualification.cmd
   accounted for                            grep -nvE "^(rem|set |call |echo |
                                           setlocal|endlocal|@echo)" returns
                                           nothing. Added to the procedure
                                           because P17-ASSEMBLY-001's and
                                           P17-SOLVE-001's copies each carried
                                           bare OLD_END lines that cmd.exe
                                           reported into their .err files --
                                           see PRIOR_EVIDENCE_CORRECTION.md
```

## Candidate

```text
FROZEN            2026-10-10 00:35:15
HEAD              5e06595a2151869b855fe57e1ae3c54fad063a4d
staged tree       6aff4d95e2e024d3a5a9fd083c7ed1ef07bbda19

apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           a1a6ba1c5f68e49eddf03a6df5a53d7af18d08d4
src               bba306769c933eeee6f29bbd91dbceee17e453de
tests             14ae02cfd533de311a55e7509efd7e50b32600c3
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
include   7bfbf016 -> a1a6ba1c    one new public header
src       b1010cf3 -> bba30676    one new source, the registration in
                                  src/structural/CMakeLists.txt, and a COMMENT
                                  correction in StructuralSolve.cpp
tests     de6bf9d8 -> 14ae02cf    two new suites, one new compile-fail group,
                                  three registration edits
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   UNCHANGED
```

### What was modified, and what it owes

```text
git status --porcelain -- src include
   M src/structural/CMakeLists.txt        the new source, and nothing else --
                                          Eigen was already a PRIVATE link
   M src/structural/StructuralSolve.cpp   A COMMENT ONLY
  ?? include/bettercad/structural/StructuralPost.hpp
  ?? src/structural/StructuralPost.cpp
```

`StructuralSolve.cpp`'s comment claimed "EIGEN IS USED HERE AND NOWHERE ELSE IN
THIS MODULE", which this milestone made false by admitting a second Eigen
consumer. Leaving a false statement in a qualified file is worse than
correcting it; the executable is unchanged, and `unit\.Structural` is in the
repeat set regardless, so P17-SOLVE-001's 25 tests ran 5x in two presets.

```text
affected milestone     shared code changed?   requalification
-----------------------------------------------------------------
P17-SOLVE-001          a comment only         25 tests pass in all three
                                              presets and 5x in two
P17-ASSEMBLY-001       NO                     not owed; 26 tests, likewise
P17-BC-001             NO                     not owed; 32 tests, likewise
P17-DOF-001            NO                     not owed; likewise
P17-ELEM-001           NO                     not owed; 29 tests, likewise.
                                              Tet4Element.cpp is UNTOUCHED:
                                              this milestone CONSUMES
                                              strainFrom and stressFrom
P17-LOAD-001           NO                     not owed; likewise
P16                    NO                     not owed
```

### No shared test infrastructure changed

`tests/architecture/CheckLayering.cmake` is byte-identical to
P17-SOLVE-001's. Rule 7 already keeps Eigen out of every public header, and
`StructuralPost.hpp` is the first new public header since that rule was
written — so the rule is **exercised** rather than extended. `architecture` is
in the repeat set for exactly that reason, and all 15 of its tests pass in all
three presets and 5x in two.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-SOLVE-001's, and `qualify.cmd` unchanged since
P16-SIZE-001 — **twenty milestones in a row**. `verify-harness.cmd` is its
regression and is meant to be run whenever `qualify.cmd` is edited; it was not
edited, and the hash above is the evidence.

`QUALIFY_REPEAT` is expanded as `!REPEAT!` and never `%REPEAT%`: a `%VAR%`
holding `|`, `(` or `)` is substituted when the `for` block is **parsed** and
closes the block early, which kills the run after every preset has already
passed.

The exact invocation is committed beside this file as
[qualification/run-qualification.cmd](qualification/run-qualification.cmd),
with every term of the repeat filter justified in its header — and, this time,
with every line verified to begin with an expected token.

## Stages

```text
17 stages, 0 failed, qualify.cmd exit 0

debug-ext         configure 0   clean 0   build 0   no-op 0   ctest 0
release-ext       configure 0   clean 0   build 0   no-op 0   ctest 0
debug-shared-ext  configure 0   clean 0   build 0   no-op 0   ctest 0
repeat release-ext  727 selected, exit 0
repeat debug-ext    727 selected, exit 0
```

Each preset was configured, had **every build output removed**, rebuilt with
warnings as errors, and tested only after a successful build.

```text
started   2026-10-10 00:35:51
finished  2026-10-10 03:40:15
elapsed   3 h 04 min
```

```text
stage                      from        to          duration
--------------------------------------------------------------
debug-ext build            00:36:02    01:02:41    26 min 39 s
debug-ext ctest            01:02:42    01:26:14    23 min 32 s
release-ext build          01:26:24    01:59:41    33 min 17 s
release-ext ctest          01:59:42    02:22:16    22 min 34 s
debug-shared-ext build     02:22:25    02:48:32    26 min 07 s
debug-shared-ext ctest     02:48:33    03:12:06    23 min 33 s
repeat release-ext         03:12:06    03:25:45    13 min 38 s
repeat debug-ext           03:25:45    03:40:13    14 min 28 s
```

## Results

```text
preset            tests        result   objects  warnings
-----------------------------------------------------------
debug-ext         3694/3694    100%         632         0
release-ext       3694/3694    100%         632         0
debug-shared-ext  3694/3694    100%         632         0

repeat release-ext  727 x until-fail:5   100%
repeat debug-ext    727 x until-fail:5   100%
```

```text
3647 at P17-SOLVE-001  ->  3694 here, +47
    35  tests/structural/StructuralPostTests.cpp
     6  tests/reference/StructuralPostReferenceTests.cpp
     6  tests/compile_fail/StructuralPostMisuse.cpp
```

### The selection was counted, not assumed

```text
unfiltered, each preset   3694, and this milestone's tests were found INSIDE
                          each preset's own ctest log:
                              StructuralPost_           41
                              compile_fail.structpost    6
                              architecture              15
                          identical in all three

repeat, each preset       727 selected -- the harness records the number
                          itself and a zero-match filter is a FAILED stage
                          twice over (noTestsAction=error, and the guard above
                          the loop) and inside that 727:
                              StructuralPost_           41
                              StructuralSolve_          25
                              StructuralSystem_         26
                              StructuralBC_             32
                              Tet4Element               29
                              architecture              15
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
produced, and not a stale artifact. **This milestone has a reason to insist on
it:** an orphaned probe harness earlier the same day left a 0-byte
`bettercad_tests.exe` that ninja considered up to date, and the next `ctest`
reported failures from a mutant binary. This check is what distinguishes the
two situations.

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
0 warnings over 632 objects, in each of the three presets
```

`-Werror` is on, so a warning is a failed build rather than a line in a log.
The categories brief section 153 names were the ones to watch:

```text
Eigen self-adjoint eigensolver     none. The 3x3 is built by explicit
                                   static_cast from Stress/double into
                                   Eigen::Index positions
uninitialised tensor values        none; every aggregate has default member
                                   initialisers and -Wunused is on. It caught
                                   a stray `scale` in P17-SOLVE and a
                                   misdirected test argument here
switch / component ordering        -Wimplicit-fallthrough and the
                                   no-default switches over TensorComponent
implicit unit conversion           impossible: Quantity has no implicit
                                   conversion to double, and four
                                   compile-failure cases enforce it
signed/unsigned NodeId indexing    -Wsign-conversion is on; every index is an
                                   explicit static_cast
narrowing                          -Wconversion is on
```

## Qualified tree == committed tree

```text
component hashes before the first build   as listed above
component hashes after the last test run  IDENTICAL, all eight
```

What moved after the freeze: `docs/verification/P17-POST-001/`,
`docs/architecture/decisions/ADR-040-*.md`, the two corrected
`run-qualification.cmd` files under `docs/verification/P17-{ASSEMBLY,SOLVE}-001/`,
`TODO.md` and `ROADMAP.md` — all documentation, none of it inside the
fingerprint, which is the project's existing policy for documentation that
cannot affect the executable or the tests.

The two corrected prior-milestone files are worth naming explicitly here: they
were edited **during** this run, and they are outside the eight fingerprinted
paths, so they could not and did not affect it. See
[PRIOR_EVIDENCE_CORRECTION.md](PRIOR_EVIDENCE_CORRECTION.md).

## What is NOT claimed

```text
performance             not measured as a gate. The large-mesh recovery is
                        inside a 29 s test that also solves; no recovery
                        timing was isolated, no baseline was taken and no
                        speedup is claimed
bitwise cross-preset
  equality              NOT claimed. The tests assert the PROPERTIES -- the
                        analytic strain, the independent Dref, the fitted
                        gradient, Cardano, the deviatoric invariant, the
                        equilibrium equality -- in each preset independently.
                        No preset's recovered field was carried to another run
                        and compared. WITHIN a run, five repeats are bitwise
                        identical and that IS claimed
peak memory             not instrumented. The storage claim is the exact
                        cardinality at two mesh sizes plus a compile-time
                        per-entry cost
continuum accuracy      not claimed beyond the equilibrium equality and the
                        one-sided compliance bound. P17-REFMOD-001 owns it
P17 as a whole          not qualified. P17-QUAL-001 is a separate milestone
reactions               retained, not computed. P17-REACTION-001 owns them
StructuralResult        still has no production caller
```
