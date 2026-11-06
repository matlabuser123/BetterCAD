# P19-IC-001 — CFD Initial Conditions

```text
TASK:            P19-IC-001 — CFD Initial Conditions
IMPLEMENTATION:  the uniform canonical record expanded into a caller-owned
                 array, bound to one mesh's field layout (ADR-068)
TESTS:           36 unit cases, 6589 assertions, identical in Debug and
                 Release; 20 compile-fail gates; 6240 tests per preset across
                 three presets, and 3075 x 5 in two repeat stages
VALIDATION:      an independent blocked-index oracle, P17's interleaved
                 numbering as a discriminator, and cell centroids from
                 CfdGeometry — an instrument the initial field never consults
RESULT:          PASS
EVIDENCE:        docs/verification/P19-IC-001/
```

## SCOPE

Authorized 2026-11-05 by the owner's "implement and fully verify", with the
twelve item checklist quoted in full. **The authorization was recorded in
`TODO.md` before any code was written**, in six places, which is the one
process deviation this milestone's predecessor had to record against itself.

What this milestone found already present, and did not build:

```text
CfdInitialCondition { Velocity3 velocity; Pressure pressure; }   P19-DATA-001
  a member of CfdAnalysisDefinition                              P19-DATA-001
  validate() rejecting a non-finite value                        P19-DATA-001
  written into the definition fingerprint by canonicalText       P19-DATA-001
  no CfdInitialConditionId, one record per analysis              ADR-061
```

So five of the twelve checklist items — initial velocity, initial pressure, the
uniform-field policy, SI units and NaN/Inf rejection — were **largely satisfied
before this milestone opened**. This evidence says so rather than claiming the
work. What IC-001 contributes to those five is one measured gap closed (see
TESTS) and the structural guarantees that keep them true.

What did not exist, and is this milestone's:

```text
nothing expanded the uniform record into an array a solve could read
nothing bound such an array to the mesh generation it was expanded for
nothing stated which variables may carry an initial condition at all
nothing structurally prevented a per-cell array being cached as authority
```

Out of scope, and named rather than attempted:

```text
P19-PERSIST-001   the file round trip. Its own Persist block names "initial
                  conditions". There is NO CFD serializer in src/io/json at
                  all -- thermal and structural have one, cfd does not -- so a
                  CfdAnalysis is not written to a .bcad file by any milestone
                  yet. IC-001 does not add one
P19-CMD-001       "Undo exact" and "Redo exact". There is no CfdCommands.cpp;
                  thermal has one. IC-001 does not add one
```

Both are later and neither is authorized. ADR-068 Decision 9 states what the
checklist's "Exact undo / persistence semantics" can honestly mean inside this
milestone, and the answer is the value semantics and the fingerprint — the
properties those two milestones will rest on — not their implementation.

## BASELINE

```text
HEAD at the freeze     cf6e37c  BetterCAD: record the P19-BC-001 commit
qualified candidate    cf6e37cc7cbbc59730a24878f795e124f4ed18bb
previous milestone     P19-BC-001 PASS 2026-11-05 (1e0858a)
cfd subsystem before   509537 assertions in 228 test cases
```

## ARCHITECTURE

`ADR-068`, nine decisions. The one that decided the shape of the milestone:

> **ADR-066 Decision 1** deferred the ownership of field values, saying the
> first real consumer "should make that decision with a use in hand".

P19-IC-001 is the first milestone to produce a **layout-indexed field array** —
one whose length comes from `size(CfdVariable)` and whose subscript comes from
the layout. `P19-BC-001` shipped first and did produce per-boundary-face values,
but it held them as `CfdBoundaryFace` records inside `PreparedCfdBoundary`, so
it never faced the array-ownership question. That distinction is itself a
review finding: the ADR first claimed "the first milestone that computes a
field", which ADR-066's own Candidates section contradicts.

And the answer reached is that **no ownership is added**: the expansion writes
into a `std::span<double>` the caller owns and sizes. Two things follow, and
they are why this option was chosen over a container:

```text
it DISCHARGES ADR-066 Decision 1 rather than pre-empting it. The clause that
  decides is "with a use in hand", and there is no use in hand: the only
  consumer of an initial field is a solve, and P19-STEADY-001,
  P19-TRANSIENT-001 and P19-LINSOLVE-001 are all later and unauthorized
"expanded cell arrays remain derived" becomes STRUCTURAL rather than a promise.
  The cfd module owns no field memory, so there is no field for a later edit to
  persist by accident
```

The cost is accepted and real: every consumer sizes its own array, and a
wrongly-sized array becomes a case that must be refused by name rather than one
that cannot be expressed.

The other decision a reader will challenge: **a uniform initial field is
uniform, and gravity does not enter it**, where `ADR-067` made a prescribed
*boundary* pressure hydrostatic. The asymmetry is deliberate. A boundary
pressure is a constraint the solve must satisfy along a face that is not level;
an initial field is a starting guess whose only requirement is to be a legal
state; the record is authorized uniform by ADR-061; and a hydrostatic expansion
would make density and gravity dependencies of a value they provably cannot
change. The accepted consequence is stated: a gravity-driven case starts further
from its solution than it needs to.

## BLAST RADIUS

Additive. No existing public API changed, and no existing behaviour moved.

```text
new        include/bettercad/cfd/CfdInitialField.hpp          406 lines
           src/cfd/CfdInitialField.cpp                        349 lines
           tests/cfd/CfdInitialFieldTests.cpp                1544 lines
           tests/compile_fail/CfdInitialFieldMisuse.cpp        221 lines
           docs/architecture/decisions/ADR-068-....md          393 lines

amended    src/cfd/CMakeLists.txt          one source added
           tests/CMakeLists.txt            one test file, in the
                                           reference-models block
           tests/compile_fail/CMakeLists.txt   the cfdic group
           tests/cli/ZeroMatchGuard.cmake  two group filters and 36 names
           TODO.md                         the authorization, then the record
```

Direct dependents: none. Nothing in the tree calls the new functions except the
new tests, because the consumer that will — a solve — does not exist.

Indirect reach, and why each is unaffected:

```text
CfdFieldMap      read only, through size() and vectorIndex(). Not modified, so
                 P19-DOF-001's qualification stands
CfdData          read only, through validate(CfdInitialCondition). Not modified
CfdAnalysisObject  read only, through canonicalText and fingerprint. Not
                 modified, so no definition fingerprint in any saved document
                 changes value
persistence      unreachable: there is no CFD serializer to change
the architecture layering test  205/205 pass, architecture.layering among them;
                 cfd (42) includes core, meshing and cfd only
```

The measurement that settles it: the cfd subsystem went from 509537 assertions
in 228 cases to **516126 in 264**. The deltas are **6589 and 36 exactly**, which
is what the new tests measure on their own — so no pre-existing cfd assertion
moved.

And the whole-suite arithmetic, reconciled per preset against the figure written
into `FREEZE.txt` before the run:

```text
6240 = 6184 + 36 + 20     and the run's own logs give rest = 6184 exactly,
                          in each of the three presets
```

## TESTS

```text
unit.CfdInitialField_*          36 cases, 6589 assertions
                                6589 in Debug and 6589 in Release -- identical,
                                so no assertion is configuration-dependent
compile_fail.cfdic.*            20 gates
cli.mesh.zero-match-guard       PASS with both group counts and 36 anchored
                                names; the 36 guard names and the 36 TEST_CASE
                                names were diffed and are the same set
architecture.*                  205/205, layering included
```

Every one of the 36 case names is anchored in the zero-match guard, so a rename
or a silent drop fails the build rather than reducing the count. The escaping
matches its neighbours — `\\.` for a literal dot, where the first version used
`\.`, which CMake reads as a wildcard. That was caught by comparing bytes
against line 525 rather than by reading the line.

The gap this milestone measured and closed, recorded because it is the only
change IC-001 makes to a checklist item that was already largely satisfied:
`P19-DATA-001`'s own test of `validate(CfdInitialCondition)` exercises **NaN in
`velocity.x` and `+Inf` in the pressure, and nothing else**. The implementation
is correct — `isFinite(CartesianVector<Q>)` does check all three components —
but the test would pass unchanged if it checked only `x`. The new case covers
four slots by three non-finite values, twelve in all, so the conjunction is
measured rather than assumed.

## INDEPENDENT VALIDATION

The expansion performs **no floating-point arithmetic**: it copies one SI value
into every slot. So validation is not about numerical agreement but about
*which slot* and *which value*, and the instruments are chosen so that none of
them is the implementation.

```text
the blocked subscript   comp * cells + cell written out again in the test file
                        rather than read from CfdFieldMap
the discriminator       P17's interleaved numbering, cell * 3 + comp, written
                        out too and asserted to DIFFER from the blocked one on
                        the mesh under test -- so the agreement is not a
                        coincidence of a one-cell mesh, and a mutation to
                        interleaved indexing fails
the expected values     computed from the canonical record directly, never by
                        asking the implementation what it meant to write
the height premise      cell centroids from CfdGeometry, which an initial field
                        does not consult at all, so the premise of the gravity
                        case rests on an instrument that case cannot influence
the cell count          read from every reference body that meshes, so the
                        array length is not a constant anywhere in the suite
the datum of comparison for the assertion totals: numbers written down before
                        the run, not a scan of the logs for the word "fail"
```

**One instrument was considered and NOT applied**: round-tripping each written
slot through `CfdFieldMap::cellOf` / `componentOf`, the inverses of
`vectorIndex`. It was identified in the author's own adversarial pass, and
declined *after the freeze* for two reasons — it duplicates coverage
P19-DOF-001 already qualified on those same functions, and applying it would
have voided a five-hour qualification for no new claim. It is recorded here
rather than quietly dropped, and listed again under REVISION.

## ADVERSARIAL REVIEW

Three passes: the author's own, written down before either independent review
reported so that what they confirmed and what they added stay distinguishable,
and two independent agents given the code and the ADR but **not** the author's
conclusions — one adversarial, one test-gap. Both were told to try to disprove
the milestone.

**Nineteen findings were acted on. Every one is fixed; none is deferred.**

Both independent reviews then found the same two things the author's pass had
found (the `Body` arm resting on a hand-built source, and the single-cell gap)
and added the rest.

### The findings that mattered, and what each was

**A seventh omission with no safety argument — the one substantive code
defect.** `CfdAnalysisDefinition::mesh` was omitted from the source binding and
was not in Decision 7's table, and unlike the other six no provable statement
was available for it: re-point an analysis at another mesh control and the field
reported itself CURRENT, for an analysis whose field must now cover a different
number of cells. It is canonical intent, it moves the fingerprint, and it
provably changes the array's LENGTH. Fixed by refusing it —
`DefinitionNamesAnotherMesh`, first in phase order. The sibling
`CfdBoundarySource` omits it too; that is a pattern-level gap this milestone
does not reach into, and it is recorded under KNOWN LIMITATIONS.

**A rationale that was simply false.** The header and the source both claimed
that checking the unknown variable before the length prevented a "silent no-op",
on the reasoning that `size()` answers 0 for an unrecognised variable. A review
MEASURED it and it is false twice over: with the checks swapped, `(unknown, 0)`
still reports the variable, and even with the check removed entirely the
expansion's switch has no `default`, so an unrecognised enumerator falls out to a
named refusal rather than to success. The order buys the honest TAG —
`(unknown, 17)` is the variable's fault, not the length's — and both comments now
say that instead.

**A claim the tests could not carry, and still cannot.** ADR-068 Decision 4 said
"every written slot is checked against `CfdFieldMap::vectorIndex`". A review
replaced the expansion's `CellIndex::fromValue(c)` with `fromValue(cells-1-c)`
and **all tests passed**, because a uniform field's array is byte-identical under
any permutation of cells. The component-to-block mapping IS pinned (the
interleaving mutation does fail). The cell-to-offset mapping is not and cannot
be, for a uniform field. The ADR now says so, and names what carries that arm
instead: the subscript comes from `CfdFieldMap::vectorIndex`, whose numbering
P19-DOF-001 qualified. The first milestone to write a NON-uniform field will be
the first able to observe it.

**A guarantee the header stated and nothing enforced.** `CfdInitialFieldSource`'s
`operator==` is documented as the reason "a dependency added here without being
compared is not possible" — but `staleReasons` compares the four members
individually and never called it, and no production code or test called it at
all. Rewriting it as `return true` broke nothing. Now `isCurrent` and the
declared equality are asserted to agree on every candidate source, so a fifth
member compared by one and not the other fails.

**A `CHECK` guarded by its own predicate.** The block the test file called "THE
DISCRIMINATING PART" read `if (misread != expected[k]) { CHECK(misread !=
expected[k]); }`, which cannot fail. Both reviews caught it. The real
discrimination was elsewhere all along — `CHECK(u[oracle] == expected[k])` in the
main loop, which a review confirmed does fail under interleaving — so the
attribution was corrected and the block replaced with an unconditional
comparison of the two readings.

**A premise printed instead of asserted.** `ARemeshStalesTheFieldAndNamesTheMesh`
commented that it was recording whether the counts changed "so the claim is not
silently resting on a count comparison", and then used `INFO`, which is exactly
what lets it rest silently. A review deleted the stamp comparison, watched the
case fail, and so MEASURED that the 3 mm remesh of the Block keeps the cell
count — the stamp really is what stales it. That is now a `REQUIRE`.

**A hole on the line this milestone claims to strengthen.** All twelve NaN/Inf
rejection cases still pass if `isFinite` is weakened to a magnitude bound, because
nothing asserted that a finite EXTREME is ACCEPTED. Fixed.

**A tag that could be swapped with every assertion still passing.**
`cfdInitialFieldExpansionFailure` was called by no test, so its `.problem` was
never read — precisely the failure mode this milestone's own header cites as the
reason a tag exists. An agreement loop over four variables by four lengths now
asserts the predictor's tag, the failure's tag and the path's message together.

**Two documentation claims that were simply wrong.** "The fingerprint is
LOSSLESS" — it is a 64-bit FNV-1a digest and cannot be; `canonicalText` is
lossless, and the strong claim now lives there, where the test already asserted
it. And the ADR told a caller to size from `CfdFieldMap::size` while the gate
asks `PreparedCfdInitialField::size`; they differ for `MassFlux`.

**A second copy of an abstraction that already existed.** `componentSi` wrote a
fresh switch over X/Y/Z and defended the duplication in a comment, while
`CartesianVector<Q>::component(VectorComponent)` was already there, already typed
and already `-Wswitch`-protected — in a file whose own stated discipline is that
one copy of the arithmetic exists. Deleted in favour of the existing accessor,
and the tree confirms zero remaining references.

**A disagreement between the two reviews, resolved by measurement.** The
test-gap review asserted that the 3 mm remesh "almost certainly" changes the
cell count; the adversarial review *measured* that it does not. The measurement
won, and the test now asserts the measured premise.

### Mutation verification — and an honest gap in this record

Six mutations were applied one at a time before the freeze, each required to be
killed by its named test, with the tree restored and verified at zero mutation
markers afterwards. `FREEZE.txt`, written before the qualification, records the
result in its PRE-FREEZE STATE section:

> six mutations applied one at a time, each killed by its named test, tree
> restored and verified at 0 markers afterwards

**The per-mutation table — which mutation was paired with which test name — is
NOT reproduced here, because its log did not survive.** The client restarted
mid-session and wiped the scratchpad that held the sweep's output; only four
draft documents were recovered. The sweep is not re-run to recover it: the
mutations edit files under the frozen surface, so re-running would void this
qualification and require another five-hour run to produce a table for work
already verified. This is recorded as a gap in the evidence rather than filled
from memory.

What *is* durably recorded, and is reproduced instead, are the **pre-fix
survival measurements** — the mutation that survived in each case, which is what
proved each gap real. These come from the contemporaneous triage written while
the reviews were being read:

```text
delete the stamp comparison in staleReasons       SURVIVED
operator== on the source rewritten to return true SURVIVED
swap the VariableIsNotCellCentred / ArrayLengthWrong tags
                                                  SURVIVED
weaken isFinite to |x| < 1e300                    SURVIVED all 12 reject cases
make the prepared field read the live definition  SURVIVED
PreparedCfdInitialField::operator== -> return true
                                                  SURVIVED
replace CellIndex::fromValue(c) with fromValue(cells-1-c)
                                                  SURVIVED -- and still does;
                                                  see Decision 4 above
rewrite the blocked subscript as interleaved      KILLED
```

Each survival above is closed by a test added in the fix pass, and the whole
suite then passed the qualification below. The interleaving mutation is the one
indexing claim the suite can carry, and it is killed.

### An artefact of the author's own tooling, recorded rather than hidden

The first mutation sweep reported `FieldEqualityDiscriminates...` failing on a
tree with no mutation in it. The cause was the harness, not the code:
`shutil.copy2` preserves mtimes, so a restored file looked OLDER than the object
compiled from the mutated one, ninja skipped the rebuild, and a **stale binary**
carrying the previous mutation produced a failure that was not there.
Fixed by copying without metadata and stamping a fresh mtime on restore, and the
whole sweep was re-run from the corrected harness.

It belongs in this record because it is the same family as the incidents this
project's evidence discipline already carries — `ctest -R` matching nothing and
exiting 0, the hyphen-stopping regex that counted 4 of 16, and the
`inherits[0]` walk that read `-Werror` as off. **A fourth instance occurred
while writing this document**: a check of whether the 20 compile-fail gates ran
used `[A-Za-z0-9_]+`, which stops at a hyphen, and the compile-fail test names
are hyphenated — so it reported 0 of 20 on a run where all 20 had passed. The
figure was wrong, the run was not. In every case working tooling returned a
plausible number and only an independent reconciliation caught it. The lesson
applied here: a mutation that "survives" is not evidence until the binary under
test is known to contain it, and a count of zero is a claim about the counter
first.

### What the reviews looked for and did not find

```text
expanded-array CONTENTS   no input found for which the values are wrong. The
                          array is a function of layout.cells() and the four
                          record values; all are carried
independence of oracles   genuine. Expected values come from the record
                          directly, the blocked subscript is written out
                          separately from CfdFieldMap, and the gravity case's
                          height premise comes from CfdGeometry, which an
                          initial field never consults
partial state             the variable and length gates run before the first
                          write; prepare mutates nothing; setDefinition is
                          atomic. The one exception is an unreachable internal
                          error, and the header now says so rather than
                          claiming the absolute
architectural boundaries  cfd (42) includes core (0), meshing (40) and cfd
                          only. No thermal, no structural, no OCCT, no Qt.
                          architecture.layering PASS
determinism               no unordered iteration, no accumulation, no
                          call-order dependence, no seed
```

## FAILURE PATHS

**Six** refusals, declared in phase order, each asserted by **tag** rather than
by message — the lesson P19-BC-001's review recorded after finding eight
refusals whose tags could have been swapped with every message assertion still
passing.

```text
DefinitionNamesAnotherMesh  prepare   the analysis names a different mesh
                                      control from the model's; added by
                                      adversarial review, and first in phase
                                      order because it invalidates the LENGTH
InitialConditionNotFinite   prepare   validate's refusal, passed through
LayoutUnavailable           prepare   requireCfdFieldMap's, passed through
VariableIsUnknown           expand    an enumerator this build does not define
VariableIsNotCellCentred    expand    MassFlux: face-centred, and an outcome
ArrayLengthWrong            expand    too short AND too long
```

**`VariableIsUnknown` is decided before the length, and the reason is narrower
than it first appeared.** The original claim — that a length-first order would
let an unknown variable with a zero-length array succeed silently — was measured
false by a review and withdrawn (see ADVERSARIAL REVIEW). What the order
actually buys is the honest tag: `(unknown, 17)` is reported as the variable's
fault rather than the length's. Tested at length 0 and at a non-zero length, so
a swapped order changes a tag a test reads.

Atomicity: the length is checked before the first write, so a refused expansion
leaves the caller's array untouched. Asserted on a **pre-poisoned** array, in
both the too-short and too-long directions, because a partial write would leave
a consumer holding a field initialised in part and garbage in the rest.

Unreachable branches, named as such in the code rather than claimed tested:

```text
expand's !index.has_value()          cell < cells and component comes from
                                     kVectorComponents, exactly the set
                                     vectorIndex accepts
expand's trailing refusal            a cell-centred variable added without a
                                     case would land there; -Wswitch catches it
                                     first, so this is the braces to that belt
the expansion failure's prepare-phase arms   all three are decided while
                                     preparing, and a PreparedCfdInitialField
                                     exists only if none happened
```

A state that cannot be constructed, and so is stated rather than tested: a
**zero-length array where zero is the right length**. `requireCfdFieldMap`
refuses a topology with no cells, so a prepared field has at least one cell and
`size(Pressure) >= 1`. The only zero-length answer is `MassFlux`, refused before
the length is read.

## PERSISTENCE

No file format exists for a CFD analysis, so there is **no round trip to run**,
and this milestone does not claim one. What is verified instead is the property
a round trip will need:

```text
the record is a pure value        two typed quantities, defaulted equality, no
                                  pointer, no handle, no mesh-local member; a
                                  mirror static_assert fixes its size
canonicalText is LOSSLESS         std::format("{}", double) is the shortest
                                  decimal that reads back as the same double.
                                  Two ADJACENT SUBNORMALS are asserted to
                                  produce different canonical texts, which is
                                  the strongest available test of that claim
the fingerprint is a DIGEST       64-bit FNV-1a over the canonical text, and
                                  so NOT lossless -- the earlier claim that it
                                  was is a review finding, corrected. Each of
                                  the four values perturbed on its own changes
                                  it, which is what it is relied on for
and CONSISTENT with equality      -0.0 is normalised to 0.0, exactly as
                                  operator== treats the two as equal, so a
                                  fingerprint never reports a change no
                                  comparison can see. The EXPANSION, by
                                  contrast, copies and so carries the caller's
                                  signed zero; the asymmetry is asserted with
                                  std::signbit per slot and stated in the ADR
the derived field is nowhere      not in the definition, not in the
 canonical                        fingerprint, and not a member of anything
                                  clonable -- held by compile-fail gates and a
                                  clone test, not by a review note
```

## DETERMINISM

No unordered iteration, no accumulation, no wall-clock, no random seed, no
locale, no temporary path. The expansion is a loop over `[0, cells)` and over
`kVectorComponents`, both ordered. Two expansions of one field are asserted
identical, and a field prepared twice from the same state is asserted equal —
and `PreparedCfdInitialField::operator==` is itself asserted to discriminate,
because a review measured that `return true` broke nothing and so that the
determinism claim had been resting on an equality that could not fail.

The strongest determinism evidence is the run itself: **3075 tests repeated
five times in each of two presets, 15375 Passed markers per log, zero failures**.

## REGRESSION

Full qualification, three presets plus two repeat stages. The harness is
byte-identical to P19-BC-001's, making this the fifty-eighth milestone run on an
unchanged harness — and it was **self-checked first**, because an unchanged
harness is not the same as a working one:

```text
QUALIFICATION FAILED: 2 stage(s) failed.
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

The verdict, which is `run-qualification.out`'s own sentence and
`qualification-times.txt`'s failed-stage count rather than any shell's exit
status:

```text
Qualification passed: every stage exited 0.
qualification finished Fri 11/06/2026 11:17:42.06, 0 stage(s) failed
```

**17 stages, every one exit 0**, total wall time 19066.21 s (5 h 17 m 46 s):

```text
preset             configure   clean    build   no-op rebuild      ctest
debug-ext               8.03    0.55  2674.34            1.09    4731.61
release-ext             7.10    0.51  1249.26            0.44    2314.84
debug-shared-ext       10.34    0.95  2191.43            0.54    2721.89
repeat release-ext                                                1489.13
repeat debug-ext                                                  1659.43
```

Counts, each reconciled against the figure written into `FREEZE.txt` **before**
the run:

```text
                        predicted   measured by ctest -N   the run
full set, per preset         6240                   6240   6240/6240, x3
repeat set, per preset       3075                   3075   3075/3075, x2
repeat markers per log      15375                      —   15375, x2
of which, new unit              36                     36   36, every log
of which, new compile-fail      20                     20   20, every full log
the rest                      6184                      —   6184, every log
```

`rest = 6184` is P19-BC-001's own full-set figure, so **no pre-existing test
moved**. Across all five logs: **0 Failed, 0 Skipped, 0 Not Run**.

Other gates read from the run rather than asserted:

```text
warnings                 0 lines matching warning/error in all three build logs
-Werror                  BETTERCAD_WARNINGS_AS_ERRORS:BOOL=ON, read from each
                         BUILT tree's own CMakeCache.txt, not from a preset walk
no-op rebuilds           each recompiled nothing ("Checking git revision" only)
architecture             205/205, architecture.layering among them
qualified candidate      cf6e37cc7cbbc59730a24878f795e124f4ed18bb == HEAD
```

**The qualified tree is the committed tree**, across SIX readings: the freeze
record, the harness's reading before the first build, its reading after the last
test run, a re-read from the live working tree with qualify.cmd's own method
(scratch `GIT_INDEX_FILE`, `read-tree HEAD`, `add -A`, `write-tree --prefix`),
the staged tree before the commit, and the commit object of `c7ce82a` itself.
All six are identical:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086   unchanged
include            f19541e865d37362d5088e3d0c857a36c1abdd35   moved
src                a467077a5ff861692449af407898e6adcb138112   moved
tests              7dcc4b8aabc3168f108b886b63556f063e4ae84e   moved
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Exactly three moved, and they are exactly the three this milestone edited.

### Two earlier attempts, both void, both recorded

```text
attempt 1   REFUSED BY THE PREFLIGHT, and the harness was right. The author
            copied the harness through a `grep -iE 'cmd|txt|out|err'` filter
            which silently dropped preflight-space.ps1, then reported "all five
            harness files byte-identical" -- true, and incomplete. Nothing was
            built and no number was produced. Both preflight hashes were then
            checked against the ones run-qualification.cmd records for itself
attempt 2   KILLED BY A SESSION TEARDOWN at test 5589 of 6240 in the second
            preset. The cause is in the exit codes, not the code under test:
            0x40010004 is DBG_TERMINATE_PROCESS (killed, not failed) and
            0xC000026B is STATUS_DLL_INIT_FAILED_LOGOFF. VOID, and its logs are
            kept under qualification/attempt-2-killed-by-a-session-teardown/
```

Two things from attempt 2 are worth keeping, both clearly from a voided run.
Its `debug-ext` passed **6240 of 6240**, independently confirming the predicted
count before the attempt this evidence reports. And its repeat stages, running
against a build tree that no longer existed, **selected 0 tests and were counted
as FAILED** — the P19-DOF-001 attempt-1 lesson holding under a condition nobody
arranged, where `ctest`'s "No tests were found!!!" would otherwise have passed
as success.

Attempt 3 was launched detached so a client restart could not take it down
again, and is the run reported above.

## PERFORMANCE

Not measured, and not claimed. The expansion is O(cells) copies with no
arithmetic; there is no baseline to compare against because nothing expanded a
field before this milestone.

## KNOWN LIMITATIONS

```text
L1  A SINGLE-CELL MESH IS UNREACHABLE BY CONSTRUCTION, and it is the one case
    where blocked and interleaved numbering COINCIDE -- so the discriminating
    assertion has no force there. prepareCfdInitialField needs a CfdModel,
    requireCfdFieldMap gates the topology's generation against it, the
    assembler is friend-private, and no reference model has one cell. The
    layout's own one-cell behaviour is already proven in CfdFieldTests.cpp
L2  THE CELL-TO-OFFSET MAPPING IS UNOBSERVABLE for a uniform field: the array
    is byte-identical under any permutation of cells, and a review measured
    that reversing it passes every test. The component-to-block mapping IS
    pinned. The first milestone to write a NON-uniform field will be the first
    able to observe the cell arm
L3  CfdBoundarySource OMITS definition.mesh TOO -- the same gap this milestone
    closed on its own type. It is a pattern-level finding, outside this
    milestone's authorized scope, and is recorded for the owner rather than
    reached into
L4  THE `wrong == 0` ITERATION'S "left untouched" HALF IS VACUOUS: all_of over
    an empty span is trivially true. The right-1 and right+1 iterations carry
    that claim; the length-0 iteration is still asserting the TAG, which is its
    point
L5  CfdInitialFieldStaleReason::Body IS EXERCISED ONLY THROUGH A HAND-BUILT
    SOURCE. An analysis's body comes from the CfdModel, not from the
    definition, so re-pointing one at another body is not reachable through the
    document path this suite uses
L6  THE PER-MUTATION PAIRING TABLE DID NOT SURVIVE the client restart that
    wiped the scratchpad. The sweep's result is recorded contemporaneously in
    FREEZE.txt and the pre-fix survivals are reproduced above, but the
    mutation-to-test pairing is not reconstructed, because re-running would
    modify the frozen surface and void this qualification
L7  NO PERSISTENCE AND NO UNDO COMMAND EXIST FOR A CFD ANALYSIS. The checklist
    item is satisfied as value semantics plus the fingerprint, per ADR-068
    Decision 9, not as a round trip. P19-PERSIST-001 and P19-CMD-001 are later
    and unauthorized
```

## RESULT

```text
RESULT:   PASS
```

All twelve checklist items are met, five of them standing on work
`P19-DATA-001` had already done plus one measured gap closed here, and
"Exact undo / persistence semantics" met as the properties those milestones
will rest on rather than as their implementation — stated plainly in ADR-068
Decision 9 and again under KNOWN LIMITATIONS, not claimed as a round trip.

## REVISION

```text
19 review findings acted on, none deferred, across three passes -- the author's
own (written before the independent reviews reported), one adversarial agent and
one test-gap agent, neither given the author's conclusions

one substantive code defect found and fixed: CfdAnalysisDefinition::mesh
omitted from the source binding, now refused as DefinitionNamesAnotherMesh and
first in phase order

three documentation claims withdrawn as measurably false: the silent-no-op
rationale for the unknown-before-length order, "the fingerprint is LOSSLESS",
and ADR-068 Decision 4's "every written slot is checked"

two of the author's own improvements DECLINED AFTER THE FREEZE, with reasons:
  asserting the source's stamp against the layout's rather than the topology's
  round-tripping every slot through cellOf / componentOf
both duplicate coverage P19-DOF-001 already qualified, and applying either
would have voided a five-hour qualification for no new claim. They are the
first candidates for whichever milestone next touches this file

one evidence gap recorded rather than filled: the per-mutation pairing table
(L6)
```
