# P19-TURB-ARCH-001 — Turbulence Architecture

```text
TASK:            P19-TURB-ARCH-001 -- Turbulence Architecture
IMPLEMENTATION:  ADR-079, and 30 gates in tests/. include/ and src/ UNTOUCHED.
TESTS:           6995 x 3 presets, 17715 repeat executions x 2, 0 failures
VALIDATION:      no numerical claim; two independent reviews, 14 sabotages
RESULT:          PASS
EVIDENCE:        this directory, FREEZE.md, qualification/ (19 logs)
TODO:            13 boxes ticked on this PASS
```

## TASK

`TODO.md`'s thirteen-item checklist, authorized by the owner's *"implement and
verify"* over the section pasted into the instruction, and diffed against this
file's own repository section before any implementation: **byte-for-byte
identical**, 22 lines, 13 unticked and 0 ticked — counted, not assumed — and
`sha256 0dee4f73ba12fa54aa5fa01d9ac7376610a825f31d83f19cfb48e9b5926577b8` over
the section as the previous commit carries it. The same section was pasted and
re-diffed before the work resumed, and matched again.

```text
Define laminar/turbulent mode          Define persistence policy
Define turbulence model interface      Define command policy
Define model field ownership           Define CLI policy
Define wall-treatment interface        Define validation policy
Define turbulence BC interface         No turbulence model implemented here
Define turbulence result fields          unless explicitly included
                                       Regression PASS
                                       Evidence recorded
```

## SCOPE

### What the repository had already decided, and what it left to this milestone

`ADR-058` — the decision that fixed P19 v1 as laminar incompressible flow —
**names this milestone by name** and states its brief:

> "None in v1. P19-TURB-ARCH-001 defines the interface (mode, model fields, wall
> treatment, boundary conditions, results, persistence, commands, CLI,
> validation) **before** any model; `k-epsilon` and `k-omega SST` are separate
> milestones and are deferred in full if v1 stays laminar."

So two of the thirteen items were not open questions. `ADR-058`'s canonical /
derived table already lists **"turbulence fields"** under *DERIVED — never
persisted, never in history, never an authority*, which settles "model field
ownership" and the derived half of "persistence policy". This milestone's job for
those two was to express a decision already taken and prove it, not to retake it.

`ADR-058` also fixed the refusal rule — *"by **absence** where it can be (there is
no field, no enumerator and no function …), and by a named diagnostic where the
request is expressible"* — and the mechanism: *"each unsupported item stays absent
from the types, which the compile-fail groups of those milestones assert."* The
compile-fail group below is that instruction carried out.

### What is NOT authorized, named so the drift stays visible

Three items read like invitations to build something another milestone owns, and
the reading that matters is `TODO.md`'s own:

```text
P19-KEPS-001     its FIRST item is "Define model variant", plus k, epsilon, the
                 turbulent viscosity, the production and source terms, ITS wall
                 treatment, the inlet specification and positivity. Unauthorized,
                 and deferred in full if v1 stays laminar.
P19-SST-001      k, omega, blending, near-wall, freestream, cross diffusion.
                 Unauthorized.
P19-VALID-001    seventeen acceptance items including the mesh-acceptance policy,
                 the WARNING policy and the structured report. Unauthorized.
                 ADR-058 assigns the Reynolds threshold to it BY NAME, "from a
                 reference", so this milestone may not fix a number.
P19-CMD-001      nine commands. Unauthorized -- and there is no cfd command at
P19-PERSIST-001  all, no cfd persistence and no cfd CLI in the tree, so items 7,
P19-CLI-001      8 and 9 cannot mean "implement them": there is nothing to add
                 to. They mean state the policy in a form those milestones
                 cannot violate silently.
```

**That last reading is the milestone's central scope judgement**, and it is
recorded as a judgement. A reader expecting `CfdTurbulenceMode` will not find one;
`ADR-079` `L5` says so, and says it is an interpretation rather than the only one.

## BASELINE

Measured before deciding anything, because the temptation in a milestone made of
gates is to claim enforcement that predates it.

```text
ALREADY ENFORCED, and not this milestone's to claim
  the definition holds exactly twelve members    a sizeof mirror plus a
                                                 structured binding of twelve
  the fingerprint covers every TOP-LEVEL field   a 40-row truth table and two
                                                 hand-written golden texts
  CfdResultFields is exactly three arrays        the PermittedFields mirror
  CfdResultMetadata is exactly five members      the PermittedMetadata mirror
  a cfd analysis cannot be saved                 a named refusal, pinned by a test
  a fourth CfdVariable enumerator                -Werror=switch, at two sites
  the six boundary condition kinds               a static_assert in CfdConvection.cpp

NOT ENFORCED, which is the measurable difference this milestone makes
  G1  kCfdVariables had no COUNT PIN -- NARROWER than it first read, see below
  G2  CfdBoundaryTreatment had NO size or member mirror anywhere in tests
  G3  there was NO cfd ownership checker at all; all seven are thermal's
  G4  the three NESTED settings records had neither a mirror nor a binding
```

**G1's FIRST STATEMENT WAS WRONG AND A MUTATION CAUGHT IT.** It read *"widening
both arrays to four passes every assertion in the file — the frozen table is
frozen by a comment"*. It is not: `CfdVariable` is switched on exhaustively at
`src/cfd/CfdFields.cpp:63` and `src/cfd/CfdInitialField.cpp:296` under `-Werror`.

```text
M1   widen kCfdVariables only            CAUGHT ALREADY, not by the new pin
M2   widen BOTH tables consistently      CAUGHT ALREADY, same two switch errors
M2b  widen both AND fix both switches    the new pin is the ONLY failure
```

So the pin does not stop a fourth variable; `-Werror=switch` does, and did before
this milestone. What the pin adds is the last step: once an author has satisfied
every switch — the first thing the compiler tells them to do — nothing else made
the widening state itself. A modest claim, recorded as the modest one.

## ARCHITECTURE

`ADR-079`. Four candidates were compared on the axes `CLAUDE.md` names, and the
one chosen adds **no production type at all**.

```text
A  declare the types now        rejected: ADR-061 Decision 4 forbids a selector
                                with one legal value; CfdBoundary.hpp forbids an
                                enumerator no input can produce; CfdVariable's
                                own comment names turbulence as the change it
                                refuses; and it takes P19-KEPS-001's first item
B  documents only               rejected: P19-ARCH-001 did NOT do this -- it
                                shipped one type and eight negative fixtures --
                                and a negative claim with no test is prose
C  a derived flow-regime type   rejected, and it was the close call: ADR-058
                                gives the threshold to P19-VALID-001, so the type
                                would ship with no number it may apply. Kept in
                                the ADR as the shape that milestone should adopt
D  the decision in an ADR,      CHOSEN
   enforced by gates
```

The binding constraints are quoted in the ADR as `C1`–`C8`, each read first-hand.
The two that decided it:

```text
ADR-061 Decision 4   "There is no field for the coupling algorithm, the time
                     scheme, the convection scheme or the linear solver. Each has
                     one qualified value or none yet, and A SELECTOR WOULD BE A
                     FIELD WITH ONE LEGAL VALUE."
CfdBoundary.hpp:159  "An enumerator no input can produce is worse than no
                     enumerator, so the value is absent rather than documented as
                     unreachable."
```

The structural precedent that looked like a way through does not transfer, and the
reason is its second sentence. `StructuralAnalysisMode::LinearStaticWithGravity`
is marked *"SELECTABLE, NOT YET ASSEMBLED"* — licensed because *"a material can be
resolved for this mode today, which is what proves the data is there before the
physics needs it."* Selecting a turbulent mode today resolves nothing, assembles
nothing, and if accepted would run laminar silently, which `ADR-058` forbids by
name.

## BLAST RADIUS

Drawn before implementing and then **measured in hashes** by the freeze.

```text
seven of the eight frozen paths are BIT-IDENTICAL to the committed tree
tests              47658ea5 -> bf65a34a      the only subtree that moved
```

`include` and `src` bit-identical is not a convenience — it is the milestone's
central claim measured rather than asserted. A decision to add no production type
either shows up as two unchanged subtree hashes or it was not taken.

What was considered and found not to be reachable: no module boundary is crossed
(nothing is added to a module); no layer changes (`cfd` stays at 42); no public
header changes, so no dependent recompiles for a semantic reason; no persisted
format changes, which the unchanged golden digest
`cfd-definition-v1:20707079b32e4964` is the check on; and no reference model
moves. What IS reachable, and was: the suite's test counts, the repeat set, the
zero-match guard's minimums, and three `CMakeLists.txt` registrations.

## IMPLEMENTATION

Nothing under `include/` or `src/`. Inside the frozen surface:

```text
tests/architecture/CheckTurbulenceOwnership.cmake   new, FOUR rules
tests/architecture/fixtures/turbulence-*/           new, ELEVEN trees
tests/architecture/CMakeLists.txt                   +12 registrations
tests/cfd/CfdTurbulenceArchitectureTests.cpp        new, 4 cases / 34 assertions
tests/compile_fail/CfdTurbulenceMisuse.cpp          new, 14 absence gates
tests/compile_fail/CMakeLists.txt                   +14 registrations
tests/CMakeLists.txt                                +1 source
tests/cli/ZeroMatchGuard.cmake                      +3 minimums
```

### The checker's four rules, and why each exists

```text
1  no file names a turbulence model quantity, ATTRIBUTED TO ITS AREA so that
   canonical intent and the frozen table (decisions 1, 3), persistence (7), a
   command (8), the CLI and a view (9) and anywhere else each get a message that
   NAMES the decision it breaks rather than one anonymous ban
2  no file fixes a laminar-regime threshold -- P19-VALID-001 owns it (decision 10)
3  no `virtual` under cfd -- decision 2's one checkable half, measured at 0 of
   the 46 cfd files before the rule was written
4  the BARE symbols -- nut, mut, epsilon, omega, tke -- inside cfd ONLY, where
   every one measures zero, which closes what the ADR calls its largest hole
```

Rule 4 exists because of a review and the measurement behind it is the whole
point: those symbols occur in innocent code tree-wide (`mut` 9 files, `tke` 10,
`closure` 8, `nut` 3, `LES` 2, `epsilon` 1) and **zero times inside the 46 cfd
files** — the one scope where the ambiguity resolves in favour of the ban, and
the scope where a model would have to live.

### What is deliberately NOT matched, and the measurement that decided it

A gate that fires on innocent code is worse than no gate, because it teaches
people to widen it. The exclusions are counted over exactly the files the glob
matches, with both comment forms stripped:

```text
                 in the 600 scanned   in all 1830 incl. tests
tke / TKE                10                  44
mut                       9                  30
closure                   8                  17   cfd's OWN word: worstClosure
nut                       3                   7
LES                       2                   9
epsilon                   1                  11
RANS                      1                   9
omega                     0                   6   excluded on MEANING, not count
Reynolds                  0                   2   excluded on MEANING, not count
```

Three patterns were **removed** after review because measuring zero does not make
a word mean turbulence: `[dD]amping` (`ROADMAP.md:423` describes the *shipped*
`P13-SOLVE-001` as a Gauss-Newton loop with damping), `[dD]issipation` (viscous
dissipation is a laminar term) and `[kK]ineticEnergy` (ordinary mechanics). The
compound forms that do mean it are still matched.

## TESTS

```text
unit.CfdTurbulenceArchitecture_      4 cases, 34 assertions, tag [cfd][turbarch]
compile_fail.cfdturb                14 absence gates + one control target
architecture.turbulence-ownership   12 ctests: the real tree plus eleven fixtures
```

### The gates were each made to fail on purpose before they were believed

```text
M1   widen kCfdVariables only           caught by -Werror=switch, NOT by the pin
M2   widen both tables consistently     same
M2b  widen both AND fix both switches   the count pin is the ONLY failure
M3   a fourth treatment member          the mirror AND its binding both fire
M4   canonicalText emits a turbulence   the absence test fails at runtime
     line
the checker, eleven fixture trees       each exactly its registered exit code,
                                        violation count and message
```

Each mutation was applied to a copy-protected production file, built, and
restored with the restore verified by `sha256` rather than assumed.

**AND THE RESTORE EXPOSED A HAZARD THAT READ AS A PRODUCT DEFECT.**
`shutil.copy2` preserves mtime, so a byte-identical restore left the sources
looking OLDER than the objects built from the mutated ones; ninja ran three edges
and the linked binary still held a four-variable `isRecognised` and a
`canonicalText` that emitted a turbulence line. Two of this milestone's own tests
failed. Restoring the content is not restoring the tree. Both scripts now
`os.utime` after every restore, with the note explaining why.

### The valid fixture is a test and not a decoration

It only means something if the excluded patterns would fire on it:

```text
would the BARE patterns have flagged it?   mut 5   nut 1   epsilon 2   omega 2   LES 2
the shipped patterns, in code              0
the shipped patterns, in the raw file      6    (the comment is really there)
```

And rule 4 **failed that fixture the moment it was added**, because the fixture
was at `src/cfd/CfdSteady.cpp` stuffed with exactly those symbols. That is the
rule biting, not the fixture being unlucky: it now holds a non-cfd file for rule
1's tree-wide exclusions and an innocent cfd file carrying `closure`, which stays
deliberately unbanned.

## INDEPENDENT VALIDATION

**There is no numerical claim in this milestone and therefore no analytic
reference.** Nothing here computes anything; saying so is more useful than
inventing a validation section. What stands in its place is two independent
reviews on concerns the author should not judge alone, and precedent checked
first-hand rather than taken from a summary — every `C1`–`C8` citation in
`ADR-079` was read in the file it comes from, and the one dispute a reviewer
raised about an example was re-measured and resolved in the ADR's favour.

## ADVERSARIAL REVIEW

Two independent reviews ran against the first version of the battery. **Between
them they found ten surviving mutations, four gates that changed meaning instead
of failing, six tautologies, and two false MEASURED claims in `ADR-079`.** All of
it in gates this evidence had already called verified one pass earlier.

Acted on:

```text
the compile-fail TU saw 5 of 23 cfd headers, and the 18 it missed included
CfdMomentum.hpp -- the seam decision 2 NAMES -- so the banned spelling declared
there left six gates green                      -> all 23 headers included
a shared "is not a member of" regex could not tell a missing ENUMERATOR from a
missing ENCLOSING ENUM, and the one parameter-taking case went green if
CfdAnalysisDefinition was renamed. The idiom was 200 lines above in the same
file                                            -> each regex names its own
                                                   identifier
the checker stripped only `//`, so an innocent /* no kEpsilon here */ anywhere
in src/ would have turned the real-tree gate red -- the false-positive failure
L3 warns about, inside the gate that cites it    -> block comments stripped
"0 violations" is what an EMPTY GLOB prints, and nothing asserted the scan read
anything; ZeroMatchGuard.cmake exists for that  -> a zero-scan refusal plus a
                                                   STDOUT_REGEX on the count
the pattern set was camelCase ONLY while every persisted key here is snake_case,
so a persistence violation would be written in the one dialect the persistence
gate could not see                              -> both dialects
"No virtual enters cfd" was prose               -> rule 3
a field in a NESTED settings record defeated every gate AND the pre-existing
"the fingerprint covers every field"            -> mirrors for all three records
the bare symbols could not be banned tree-wide, but measure ZERO inside cfd
                                                -> rule 4
three assertions were tautological and one claim was false: the cfd-definition-v1
check cannot catch a mode field                 -> removed, weaker claim stated
"nine ownership checkers" -- there are SEVEN    -> corrected in four places
the false-positive counts were measured over 1830 files including tests/ and
quoted against "the 600 scanned", wrong in 7 of 9 rows
                                                -> re-measured, and two rows
                                                   restated as excluded on
                                                   MEANING rather than on a count
the Consequence said widening the allow-list is "the visible act of authorizing
a model", which mis-describes the widening that happens FIRST
                                                -> two distinct widenings named
```

**One of the review's own corrections was wrong and is recorded as checked rather
than conceded.** It said `closure` does not occur in `src/cfd/CfdContinuity.cpp`;
it does, as `worstClosure`, after both comment forms are stripped. Its recount
also put `closure` at 1 and `tke` at 0 over the scanned set where the measurement
gives 8 and 10.

Not acted on, and recorded as limitations instead: `ADR-079` `L8` to `L14`.

**Two things the reviews confirmed are right**, which matters because the same
shape went wrong twice before in this phase: the five member-naming gates are
correctly NARROW — rename, delete or forward-declare the enclosing type and each
goes red — and the three type-absence gates admit none of `no matching function`,
`is private` or `use of deleted function`, so the arity-for-access meaning-change
that cost `P19-PVCOUPLE-001` and `P19-STEADY-001` cannot recur in them.

## DETERMINISM

The checker is a text scan over a `list(SORT)`ed glob with no clock, no hash and
no unordered container, so its output is a function of the tree. The four runtime
cases assert over frozen tables and pure functions and allocate nothing. The
compile-fail cases are compiler invocations whose diagnostics are matched by
regex. Nothing in the milestone reads a seed, a time or an environment variable.

## REGRESSION

**Clean from scratch on three presets, and nothing failed anywhere.** Every
figure below is parsed out of a harness log by
[`experiments/reconcile.py`](experiments/reconcile.py); none of it is
transcribed by hand.

```text
preset              full suite    ctest wall   build edges   cleaned   err / warn
debug-ext          6995 / 6995       4754 s   833 / 833    831     0 / 0
release-ext        6995 / 6995       3440 s   833 / 833    832     0 / 0
debug-shared-ext   6995 / 6995       3646 s   833 / 833    845     0 / 0
```

**The suite total is counted four ways, not read off one line.** ctest's own
`100% tests passed out of 6995`; 6995 `Passed` report lines; 6995 `Test #`
reports; and 6995 `Start` lines, which is the reading that matters — a test that
starts and never reports, or reports without starting, is a log a pass count
cannot see through. The fourth reading was added because the falsification
below found its absence.

```text
group                                   this run    baseline    new
architecture.*                               217         205    +12
compile_fail.*                              1090        1076    +14
unit.CfdTurbulenceArchitecture_                 4           0     +4
                                        -----------------------------
full suite                                  6995        6965    +30
```

**The repeat stage.** `3543` selected per preset — the harness logs its own
selection, so an empty filter counts as a failed stage — and `17715`
executions in each of the two repeat logs, which is `3543 x 5` exactly.
`compile_fail` stays out of the repeat set, confirmed at 0 occurrences in both
logs. `P19-TRANSIENT-001`'s voided attempt was caught by this stage selecting
five tests too few; it selected the right number here.

**The builds were clean before they were built.** The clean stage removed
831, 832 and 845 files from the three trees — NOT the same number from each, and
this sentence first said "831 files from each tree" because one log was measured
and three were described. The shared preset's extra 14 are its import libraries
and DLLs. Each configure log reports `Warnings as errors : ON`
and a `Dependency prefix` under `bettercad-deps` — the condition
`P19-TRANSIENT-001`'s failed second attempt established as necessary. Zero
`error:` and zero `warning:` lines in all three build logs under `-Werror`.

**Freshness: the no-op rebuild compiled and linked nothing.** Each
`rebuild-*.log` holds one edge, `Checking git revision`, and no `Building`,
`Linking` or `Compiling` line; ninja planned 8 edges on the static presets and
20 on the shared one and pruned the rest by restat once the generated revision
header came out unchanged. A stale binary cannot produce this log, which is the
failure mode that voided `P19-TRANSIENT-001`'s first attempt.

### The per-tag readout, off the three QUALIFIED executables

A pass count cannot see an assertion that stopped running. These can. Read from
`<preset>/bin/bettercad_tests.exe` after the gate, not from the frozen tree, so
it is the binary that actually ran:

```text
[cfd]               755077 assertions in   701 cases   = 755043 + 34 and 697 + 4
[cfd][turbarch]         34 assertions in     4 cases   the new suite
[cfd][steady]         1263 assertions in    19 cases   IDENTICAL to baseline
[cfd][transient]      1813 assertions in    34 cases   IDENTICAL to baseline
[linalg]              2560 assertions in    29 cases   IDENTICAL to baseline
[structural]        271414 assertions in   415 cases   IDENTICAL to baseline
[thermal]           595622 assertions in  1168 cases   IDENTICAL to baseline
```

**All three presets returned identical figures for all seven tags**, and five of
the seven are bit-identical to the pre-freeze baseline. That is the measurement
that makes "a tests-only change" a claim rather than an assertion:
`[cfd][steady]` and `[cfd][transient]` unchanged means this milestone touched
neither qualified solver, and `[structural]` and `[thermal]` unchanged means it
reached nothing outside CFD. Raw logs:
[`qualification/binary-readout-cfd.log`](qualification/binary-readout-cfd.log)
and
[`qualification/binary-readout-large-tags.log`](qualification/binary-readout-large-tags.log).

### The reconciliation, and the sabotages that make it worth reading

[`experiments/reconcile.py`](experiments/reconcile.py) runs **179 checks**
across `FREEZE.md`, all nineteen harness logs, the six harness files, the live
tree and the checker itself, and parses every figure out of `FREEZE.md` rather
than holding a literal.

[`experiments/reconcile-falsification.py`](experiments/reconcile-falsification.py)
then breaks one thing at a time in a scratch copy and requires the run to fail:
**the control passes and all fourteen sabotages are caught.** It found a real
hole on the first attempt — a renamed ownership test went unnoticed because
every count was taken off `Test #` reports and never off `Start` lines — and
the instrument was fixed rather than the sabotage weakened. Two further defects
in the reconciliation were found the same way and are recorded in `L15`.

## QUALIFICATION

**The verdict is the harness's own two statements, and nothing else.**

```text
run-qualification.out     "Qualification passed: every stage exited 0."
qualification-times.txt   "qualification finished Tue 11/10/2026 23:53:20.49,
                           0 stage(s) failed"
```

**The launcher's exit code is not read.** It was 0 here, which is worth nothing:
`P19-MOM-001`, `P19-STEADY-001` and `P19-TRANSIENT-001` each had a run where the
launcher reported exit 0 while the harness reported failed stages. Seventeen
stage exits are recorded individually — 3 presets x 5 stages plus 2 repeats —
and every one of them is 0, checked by enumerating them rather than by trusting
the summary line.

```text
started    Tue 2026-11-10 18:11:52.20
finished   Tue 2026-11-10 23:53:20.49
elapsed    5 h 41 m 28 s
candidate  9d89f4d5002e0724323cc7340ea30c3cb7dc8419
presets    debug-ext, release-ext, debug-shared-ext -- the three a production
           milestone requires, each configured, cleaned, built, no-op rebuilt
           and run in full
preflight  RESULT: OK, every distinct volume at least 20 GB free
self-check verify-harness.cmd deliberately failed two stages beforehand and
           confirmed the driver counted them
```

### The qualified tree is the tree that was run

The eight frozen paths were read **five times** by three different mechanisms:
at the freeze, by the harness before its first build, by the harness after its
last test run, independently after the gate with a scratch `GIT_INDEX_FILE`, and
out of the commit object itself. **All five agree exactly**, so the tree that
was qualified is the tree that is committed:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            4759b76839047d95cf096ae80bf071c52c7e3edb
src                c33a7b6fdbe7cb1b4deb66727152525e85dd4a48
tests              bf65a34a2027d1c3ceff8a3e8b3216c538a9165d
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**`include` and `src` are bit-identical to the previous commit's**, which is
`ADR-079`'s central claim stated as a hash rather than as prose: a decision to
add no production type either shows up as two unchanged subtrees or it was not
taken. Seven of the eight are unchanged; `tests` moved from `47658ea5` to
`bf65a34a` and is the whole deliverable.

```text
qualified and committed as   6c791579d3c442e284cdeaa63d06b43cf0f5e264
on top of                    9d89f4d5002e0724323cc7340ea30c3cb7dc8419
include                      4759b768   IDENTICAL to the previous commit
src                          c33a7b6f   IDENTICAL
tests                        bf65a34a   the only subtree that moved
```

`git rev-parse HEAD:<path>` is used for this reading and for nothing else.
Before the commit it reads the PREVIOUS commit's tree and would agree with
itself for the wrong reason, which is why the other four readings go through an
index. The comparison is made by
[`experiments/reconcile.py`](experiments/reconcile.py), which asserts that both
sides hold eight paths before reporting them equal — a comparison that cannot
fail is not a comparison, which was the defect in `P19-STEADY-001`'s first
reconciliation pass.

### The harness itself

Five of its six files are byte-identical to `P19-TRANSIENT-001`'s by `sha1`, and
`run-qualification.cmd` differs only in its `rem` header — its executable half,
from `@echo off` to EOF, is **byte-identical at 1306 bytes**, compared against
the previous milestone's file rather than merely counted. This is the
sixty-ninth milestone the harness has run with its executable content unchanged.
The reconciliation's first version measured that half as 1320 bytes by rewriting
LF to CRLF; the launcher was right and the instrument was wrong, which is
recorded in `L15` because a measurement defect in a verification script is the
same class of failure as a false claim.

## KNOWN LIMITATIONS

Fifteen. `L1`-`L14` are in `ADR-079`'s own list and `L8`-`L14` are the ones
this milestone chose not to close; `L15` is about this evidence directory rather
than about the product:

```text
L1   the composition shape is prose; "no virtual" became rule 3
L2   the checker is a text scan and can be fooled
L3   and it can false-positive, which is why the exclusions are measured
L4   no threshold, so no beyond-laminar refusal exists today
L5   "define the mode" is answered by deciding there is none -- an interpretation
L6   three branches guard paths that hold no cfd code yet
L7   no performance baseline, ninth milestone running
L8   the pattern set is finite; nut, mut, tke, closure and RANS cannot be matched
     tree-wide, and nut/mut are the conventional symbols of the banned thing
L9   the member mirrors count DATA members only; a member function is invisible
L10  an enum's cardinality cannot be asserted in C++, and the three guards on it
     do not compose into a proof
L11  eleven of the thirty tests test the checker, not the tree
L12  the checker will fire on the refusal decision 10 PRESCRIBES, and the fix is
     deliberately not built here -- the shape is recorded for P19-VALID-001
L13  the comment stripper is not a lexer; `//` inside a string literal erases the
     rest of that line, and src/io/SvgWriter.cpp has such a line
L14  one identifier can still trip two rules; _count is per rule, not per file
L15  FIVE DEFECTS WERE FOUND IN THIS MILESTONE'S OWN VERIFICATION, not
     in the product: it measured a 1306-byte file as 1320 by rewriting LF to
     CRLF, it read five identical harness files as five mismatches because msys
     sha1sum escapes a backslashed filename, and it counted test reports without
     ever reading the Start lines beside them. The first two were caught by the
     script failing against evidence that was sound; the third by the
     falsification. A verification script that cannot be falsified is not
     evidence, and these are the reason the falsification exists. A FOURTH was
     found in this README: the clean-stage sentence described three trees from
     one log's figure, and FREEZE.md undercounted the verify-harness.cmd copies
     carrying the inherited 0x08 byte as 73 where three independent counts say
     75. Both are corrected in place with the correction stated, because a
     number quoted as measured is either measured or it is a defect.
     A FIFTH IS THE MOST INSTRUCTIVE. The fifth identity reading was anchored
     to HEAD and HEAD~1, which made it true for exactly ONE commit: the record
     commit that follows moved HEAD on, HEAD~1 became the qualification commit
     itself, and "exactly one frozen path moved" went from true to false. It is
     now anchored to the commit README.md NAMES, read out of this file the way
     every other figure is read out of FREEZE.md, so it stays true as the
     branch moves; HEAD is checked separately and only for what HEAD can say,
     which is that nothing since has touched the frozen surface. The
     reconciliation FAILED LOUDLY rather than drifting green, which is the only
     reason it was caught, and a fifteenth sabotage -- the evidence naming the
     wrong commit -- now covers the path that reading created.
```

## RESULT

```text
RESULT: PASS
```

**What this PASS means.** `ADR-079` decides the turbulence architecture in ten
decisions and declares no type for it; thirty gates in `tests/` hold that
decision in place and fail the build when it is violated; the gates were made to
fail on purpose before they were believed; two independent reviews found real
defects and those were fixed; and the whole tree qualified clean on three
presets with `include` and `src` bit-identical to the previous commit.

**What it does not mean.** It does not mean turbulence works — nothing here
computes a turbulent anything, and `L4` records that no beyond-laminar refusal
exists yet because the Reynolds threshold belongs to `P19-VALID-001`. It does
not mean the architecture cannot be violated: `L2`, `L8` and `L13` record
exactly how a determined implementer could slip past a text scan. And it does
not authorize `P19-KEPS-001` or any other model. Fifteen limitations are listed
below and seven of them are open by choice.

## REVISION

```text
r1  this directory, written while the qualification gate ran, after two
    independent reviews had changed the deliverable substantially
r3  the commit recorded, and with it the fifth identity reading, which only
    exists once the commit does: all eight paths out of commit 6c79157
    equal FREEZE.md's eight, and include and src equal the PREVIOUS commit's.
    reconcile.py was changed to make that reading -- its pre-commit check
    asserted that exactly one path differed from HEAD, which was true before
    this commit and false after it.
r4  the fifth reading re-anchored to the commit this file NAMES rather than to
    HEAD~1, after the record commit made the HEAD~1 version fail. 186 checks,
    and a fifteenth sabotage added for the path that reading created: 15 of 15
    caught, control passing. L15 records it as the fifth defect found in this
    milestone's own verification.
r2  REGRESSION, QUALIFICATION and RESULT filled from the harness's own logs
    after the gate returned 0 stage(s) failed. The per-tag readout was taken
    off the three qualified executables, the reconciliation was extended from
    161 to 179 checks after its falsification found a hole, and L15 was added
    for the three defects the falsification found in the reconciliation itself.
```
