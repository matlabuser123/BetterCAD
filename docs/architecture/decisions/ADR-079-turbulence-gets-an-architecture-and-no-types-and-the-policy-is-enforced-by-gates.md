# ADR-079 — Turbulence gets an architecture and no types, and the policy is enforced by gates

```text
STATUS:    Accepted
DATE:      2026-11-10
MILESTONE: P19-TURB-ARCH-001
TOUCHES:   ADR-058 (the v1 scope, which names this milestone and defers turbulence
           in full), ADR-059 (the cfd layer), ADR-061 (the definition holds intent
           only, and Decision 4 forbids an algorithm selector), ADR-066 (the field
           layout and the frozen variable table), ADR-067 (the frozen boundary
           treatment table, and the generic Robin form it rejected), ADR-073
           (momentum is a sum of qualified operators), ADR-075 (the supplied port,
           the one strategy seam in cfd), ADR-077 / ADR-078 (the two orchestrators,
           and the hard-coded-scheme finding both carry)
```

## Context

`ADR-058` fixed the scope of P19 v1 as laminar incompressible flow and named this
milestone in the same breath:

> "None in v1. P19-TURB-ARCH-001 defines the interface (mode, model fields, wall
> treatment, boundary conditions, results, persistence, commands, CLI,
> validation) **before** any model; `k-epsilon` and `k-omega SST` are separate
> milestones and are deferred in full if v1 stays laminar."

So the question this ADR answers is not *which turbulence model* — none — and not
*whether* to define the architecture, which is already decided. It is **what form
the definition takes in a tree that has no turbulence anything**, when the next
two milestones (`P19-KEPS-001`, `P19-SST-001`) are unauthorized and may never be
authorized at all.

The tree's own answer to "how do you state a rule about work that has not
happened" turns out to be unusually well developed, and it is not prose. Seven
`tests/architecture/Check*Ownership.cmake` checkers exist, every one of them
thermal's, each a source-text gate run as a ctest that fails the build when a
decision is taken in the wrong place. (This paragraph first said NINE, in a
section that claims to have measured rather than assumed, and an adversarial
review counted them: the two extra are `CheckLayering.cmake` and
`CheckTargetDependencies.cmake`, which are neither ownership checkers nor
thermal's. A wrong number here is the same class of failure the section exists
to prevent, so it is corrected in place and the correction is stated.) `P19-ARCH-001` — the only other
architecture-only milestone of this phase — shipped exactly one type whose
possession proves a check ran, plus a battery of negative fixtures that must fail
to build. An architecture milestone here is therefore not a document; it is a
document plus the machinery that makes the document falsifiable.

## Constraints

These are not preferences. Each is an accepted decision or a rule written into a
header, and each was read first-hand rather than taken from a summary.

**C1 — No algorithm selector.** `ADR-061` Decision 4: *"There is no field for the
coupling algorithm, the time scheme, the convection scheme or the linear solver.
Each has one qualified value or none yet, and **a selector would be a field with
one legal value**."* A `turbulenceModel` field whose only legal value is `None`
is precisely the field this forbids.

**C2 — An enumerator no input can produce is worse than no enumerator.**
`CfdBoundary.hpp:159-161`, written about a state the DESIGN excludes: *"An
enumerator no input can produce is worse than no enumerator, so the value is
absent rather than documented as unreachable."*

**C3 — But an enumerator excluded by a table in ANOTHER module is kept.**
`CfdGradient.hpp:107-113`: *"The refusal is a defence against that table changing
under it, and it is documented as such rather than as something a test
reaches."* C2 and C3 are not in conflict; they distinguish *excluded by this
design* from *excluded by someone else's table*.

**C4 — A mode value whose physics is absent is legitimate only when selecting it
does something real today.** `StructuralData.hpp:69-74`:
*"`LinearStaticWithGravity` … SELECTABLE, NOT YET ASSEMBLED. P17-LOAD-001 owns
the body-force vector. **A material can be resolved for this mode today, which is
what proves the data is there before the physics needs it.**"* The justification
is the second sentence, not the first.

**C5 — `CfdVariable` is a closed table that names turbulence as the forbidden
change.** `CfdFields.hpp:124-128`: *"NOT AN OPEN SET. A fourth variable is a
formulation change -- turbulence, a species, an energy equation -- and ADR-058
refuses all of them in v1."*

**C6 — No silent downgrade and no warning beside a number.** `ADR-058`: an
unsupported request is *"an explicit diagnostic: there is no best-effort answer,
no silent downgrade, and no warning printed beside a number"*, and a
beyond-laminar solve is *"refused by name at validation … never answered by
quietly enabling a model"*. The Reynolds threshold is `P19-VALID-001`'s, from a
reference. **This milestone may not fix a threshold.**

**C7 — Derived state with no consumer is not built.** `ADR-067` rejected a
generic Robin form `a q_f + b dq/dn = c` with per-face coefficients, *"though a
later one may want it"*, because *"the coefficients ARE a discretization …
Storing (a, b, c) per face would be derived state with no consumer, built before
the scheme that defines it."* A generic wall-treatment record is the same object
under a different name, and it is the obvious thing this milestone would reach
for.

**C8 — Each later milestone's first checklist item is its own.**
`P19-KEPS-001`'s first item is literally "Define model variant";
`P19-VALID-001` owns seventeen acceptance items including the warning policy;
`P19-CMD-001`, `P19-PERSIST-001` and `P19-CLI-001` own commands, persistence and
the CLI **for cfd at all** — none of the three exists today. Taking any of those
items here would allocate another milestone's work to this one.

## What is already enforced, measured before deciding anything

The temptation is to claim enforcement that predates the milestone. These were
measured:

```text
ALREADY ENFORCED, and not this milestone's to claim
  the definition has exactly twelve members     a sizeof mirror plus a
                                                structured binding of twelve
                                                (CfdDataTests.cpp:682-711)
  the fingerprint covers every field            a 40-row truth table and two
                                                hand-written golden texts
  CfdResultFields is exactly three arrays       the PermittedFields mirror
                                                (CfdDataTests.cpp:1389-1394)
  a cfd analysis cannot be saved                a named refusal, pinned by
                                                CfdData_SavingADocumentWith...
  a 4th CfdVariable enumerator alone is refused isRecognised over kCfdVariables

NOT ENFORCED TODAY -- prose, and closable
  G1  kCfdVariables has no COUNT PIN, and this entry is NARROWER THAN IT FIRST
      READ. See the correction below: the table is already guarded, and what is
      missing is only the last step of it.
  G2  CfdBoundaryTreatment has NO SIZE OR MEMBER MIRROR anywhere in tests. A
      fourth member -- a turbulence policy -- could be added unnoticed.
  G3  THERE IS NO CFD OWNERSHIP CHECKER AT ALL. All seven are thermal's.
      Nothing in the tree would notice a wall function in the GUI, a turbulent
      viscosity in src/io/json/, or k and epsilon arriving in canonical intent.
```

**G1'S FIRST STATEMENT WAS WRONG, AND A MUTATION CAUGHT IT.** This section first
read *"widening BOTH arrays to four passes every assertion in the file -- the
frozen table is frozen by a comment"*. It is not. `CfdVariable` is switched on
exhaustively in two places, and the build is `-Werror`:

```text
M1   add a fourth enumerator, widen kCfdVariables only
     -> CAUGHT ALREADY, and not by the new pin: `-Werror=switch` fails at
        src/cfd/CfdFields.cpp:64 and src/cfd/CfdInitialField.cpp:297
M2   widen BOTH tables consistently
     -> CAUGHT ALREADY, by the same two switch errors, before any assertion
M2b  widen both tables AND add the enumerator to both switches
     -> the new count pin is the ONLY failure: two static assertions, in
        tests/cfd/CfdTurbulenceArchitectureTests.cpp, and zero switch errors
```

So the pin does not stop a fourth variable arriving; `-Werror=switch` does that,
and it was there before this milestone. What the pin adds is the last step: once
an author has satisfied every switch — which is the first thing the compiler
tells them to do — nothing else made the widening state itself. That is a real
difference and a modest one, and it is recorded as the latter.

G2 and G3 are the unqualified gaps. G1 is a narrow one. Everything else this
milestone has to say was already true and already guarded, and the measurement
is what separates the three.

## Options

### A — Declare the types now

`CfdTurbulenceMode` in the definition, a model interface, turbulence variables in
`CfdVariable`, a wall-treatment record, turbulence arrays on the result.

Violates C1 (a one-legal-value selector), C2 (unreachable enumerators), C5 (the
frozen table, whose comment names turbulence), C7 (a treatment record with no
scheme behind it) and C8 (it takes `P19-KEPS-001`'s first item). It would also
cost the three definition mirrors and the published golden digest
`cfd-definition-v1:20707079b32e4964` — a change to persisted-intent identity, for
a field with nothing behind it.

### B — Documents only

An ADR and evidence documents stating the ten policies, with no code and no
gates.

This is what `P19-ARCH-001` looks like from a distance and is not what it did: it
shipped `CfdAnalysis.hpp`/`.cpp` and eight negative architecture fixtures.
Rejected on this phase's own hardest-won lesson, recorded twice in
`P19-TRANSIENT-001`: a negative claim with no test is prose. Under B nothing
stops `P19-KEPS-001` putting `k` in the definition, and nothing stops a wall
function appearing in the CLI.

### C — A derived flow-regime type, refused at validation

No field and no selector. Instead a derived question — `cfdFlowRegime(...)`
answering `Laminar` / `BeyondLaminar` from the Reynolds number — with
`BeyondLaminar` refused by name. This reads "define laminar/turbulent mode" as
*the regime is derived, not chosen*, which fits C1 exactly and makes the
enumerator reachable, satisfying C2.

Rejected, and it was the closest call. The Reynolds threshold is
`P19-VALID-001`'s by name (C6), so the type would ship with no threshold it is
allowed to apply: a regime function whose only honest answer is "I may not say".
That is C7's object — derived state with no consumer, built before the thing that
defines it. It is also unreachable in the C2 sense for the opposite reason to A:
not because no input produces it, but because no *threshold* exists to produce it
from. Kept in this ADR as the shape `P19-VALID-001` should adopt when it fixes
the threshold, so that the regime arrives with its number rather than before it.

### D — The architecture in an ADR, enforced by gates — CHOSEN

No new public type, no enumerator, no field, no variable, no array. The
architecture is decided here, in this document, which is the authority
`CLAUDE.md` names for an architecturally significant decision. Its enforcement is
mechanical and closes G1, G2 and G3:

- a count pin for `CfdVariable`, so C5's frozen table is frozen by a mechanism;
- a member mirror for `CfdBoundaryTreatment`, so C7's record cannot grow quietly;
- `tests/architecture/CheckTurbulenceOwnership.cmake`, a source-text gate in the
  idiom of the seven thermal checkers, with negative fixtures proving it fires;
- a compile-fail group asserting the absences the policy claims, in the idiom
  `ADR-058` prescribed: *"each unsupported item stays absent from the types,
  which the compile-fail groups of those milestones assert."*

### The comparison

```text                      A types      B documents   C regime type   D gates
correctness             no model behind any of them; none computes anything
architecture fit        breaks C1,     consistent    breaks C7       consistent
                        C2, C5, C7, C8               (no threshold)
complexity              high           none          low             moderate
persistence             changes the    none          none            none
                        golden digest
determinism             n/a            n/a           n/a             gates are
                                                                     text scans
performance             n/a everywhere; nothing here runs in a solve
extensibility           pre-empts      leaves the    right shape,    leaves each
                        KEPS's first   seam          wrong milestone later item
                        item           undefended                    intact
failure modes           a field with   a policy      a function      a gate can
                        one value      nobody must   that cannot     false-positive
                        reads as a     obey          answer          on innocent code
                        capability
migration cost          three mirrors  none          none            none
                        + the digest
testability             a type with    nothing to    reachable only  every claim
                        nothing to     test          once VALID      is falsifiable
                        assert                       lands
```

## Decision

**Option D.** The ten architectural questions are answered here, and each answer
names the milestone that will implement it and the gate that holds it until then.

### 1. The mode

**There is no turbulence mode in v1 — not a field, not an enumerator.** The
analysis mode (`CfdAnalysisMode`) stays `Steady` | `Transient` and is orthogonal
to turbulence. A laminar/turbulent *selector* is forbidden by C1 while only one
value is qualified, and a turbulent enumerator is forbidden by C2 because the
only honest response to it today is refusal.

`C4` is why the structural precedent does not transfer: selecting
`LinearStaticWithGravity` resolves a density today, which is what licenses it.
Selecting a turbulent mode today would resolve nothing, assemble nothing, and —
if accepted — run laminar silently, which C6 forbids by name.

**When the mode arrives, it arrives with its model.** `P19-KEPS-001`'s first item
is "Define model variant"; the enumerator is added there, at the moment it becomes
selectable *and* assembled, which is exactly C4's condition. The cost is one new
definition field, the three mirrors and a new golden digest — paid once, visibly,
by the milestone that has something to put behind it.

### 2. The model interface

**The seam is a supplied port, not an abstract interface and not an enum
selector.** `cfd` has exactly one strategy seam today and it is `CfdLinearSolve`
— a `std::function` the caller supplies (`ADR-075`, chosen over linking `linalg`
and over assembling-only). There is no abstract physics interface anywhere in the
tree, and the only registry is the CLI verb table.

So the interface a turbulence model must take is fixed here by shape rather than
by declaration: **a free function over plain records that returns a contribution,
composed into momentum the way `ADR-073` composes convection and diffusion** — a
slot-wise sum of qualified operators, with sub-refusals predicted inside the
caller's own gate. A model is `assembleCfdTurbulentViscosity(...)` returning a
value, not a `TurbulenceModel` object with virtual methods. No `virtual` enters
`cfd`.

This is a decision about shape with no code, and that is deliberate: a declared
interface with no implementation is C7's object, and the one declaration that
would survive contact with a real model cannot be known before the model.

### 3. Model field ownership

**Already decided by `ADR-058`, and this ADR only states where it is enforced.**
`ADR-058`'s canonical/derived table lists *"turbulence fields"* under **DERIVED —
never persisted, never in history, never an authority**. Turbulence fields are
therefore the caller's own `std::span`, sized by a layout keyed to a `MeshStamp`,
exactly as `p`, `U` and `phi` are (`ADR-066`); they are never members of the
definition and never persisted.

Adding `k` or `epsilon` to `CfdVariable` is a visible change to a frozen table
(C5) and must stay visible: the count pin closing G1 is what makes the word
"frozen" mean something.

### 4. The wall-treatment interface

**No wall-treatment type, and `ADR-067`'s rejected generic form stays
rejected.** `CfdBoundaryTreatment` keeps exactly three members — one policy per
`CfdVariable`, by name — and the member mirror closing G2 holds it there.

A wall function is a *discretization*, not a boundary record: C7's reasoning
about per-face Robin coefficients applies unchanged. When a model lands, its wall
treatment is that model's own item (`P19-KEPS-001` — "Define wall treatment") and
it arrives as a function of the near-wall cell, not as stored per-face
coefficients built before the scheme that defines them.

The two existing wall refusals, `WallTargetNotPlanar` and
`WallVelocityNotTangential`, are the precedent for how a wall-treatment
capability limit is reported: refused rather than approximated.

### 5. The turbulence BC interface

**No new boundary-condition kind and no turbulence payload.** The six kinds stay
six. An inlet turbulence specification is `P19-KEPS-001`'s item by name ("Define
inlet turbulence specification") and would be a payload on the existing
`CfdBoundaryCondition` variant — reached through `treatmentOf`, whose table gains
a column only when a variable gains a row (C5).

Exactly-one-condition-per-facet is unchanged and unaffected: a turbulence
specification is part of the condition at a facet, never a second condition on
it.

### 6. Turbulence result fields

**`CfdResultFields` keeps exactly three arrays.** This is already enforced by the
`PermittedFields` mirror, which the tree's own comment says *"a fourth array could
not be added here without this line failing too"*. A turbulent viscosity is a
derived field that a later milestone publishes by widening the frozen variable
table, the layout, the factory, the validation arrays and that mirror — five
visible edits, which is the intended cost.

### 7. Persistence policy

**Turbulence intent is persisted only when it exists, and nothing turbulent is
ever persisted as derived state.** Today a cfd analysis cannot be saved at all —
`DocumentJson.cpp` refuses `'cfd-analysis'` by name, pinned by a test — and
`P19-PERSIST-001` replaces that refusal with a file representation of the
definition. The policy this milestone fixes for that milestone: a turbulence mode
or model parameter is canonical intent and is written; a turbulent viscosity, a
wall distance, a `y+` or any model field is derived and is never written, in any
form, under any key. `ADR-056`'s clause 8 — unknown keys are rejected — means a
turbulence key that arrives in a file written by a build that has no model is
refused rather than ignored.

### 8. Command policy

**A turbulence setting is edited by a command that carries a before-and-after
definition, and the history holds intent only.** No cfd command exists;
`P19-CMD-001` owns the nine it will add, following `ADR-055`'s one-base pattern.
The policy fixed here: no turbulence *field array* may enter the history, in the
same sense that `P19-CMD-001`'s own checklist already forbids pressure and
velocity arrays there. A model field is derived (decision 3), and derived state
is not undoable because it is recomputed.

### 9. CLI policy

**No CLI-only turbulence anything.** No cfd subcommand exists; `P19-CLI-001` owns
them, and `CLAUDE.md`'s rule — *"the CLI uses the same public API as everything
else"* — plus `ADR-009`'s transaction model govern. The policy fixed here: a
turbulence report prints what the core computed, and no CLI file computes a
turbulent viscosity, a `y+` or a regime verdict of its own. This is the thermal
CLI ownership checker's rule, applied to a module that does not have a CLI yet.

### 10. Validation policy

**The laminar regime limit is refused at validation, by `P19-VALID-001`, with a
threshold taken from a reference — and this milestone fixes no threshold.** C6 is
quoted rather than restated because the temptation to put a number here is the
whole risk.

What this milestone does fix is the *shape*: a beyond-laminar solve is a named
refusal in validation's ordered rule set, in the `Input` group, with a severity
that is a function of its code and not a field a caller chooses — the eleven-part
structure `ThermalValidation.hpp` already establishes. It is **not** a warning
beside a published number, and it is **not** answered by enabling a model. Option
C above records the derived-regime shape that milestone should adopt so the regime
function arrives together with its threshold.

## Consequences

- Three measured gaps close: `CfdVariable` gains a count pin, `CfdBoundaryTreatment`
  gains a member mirror, and `cfd` gains its first ownership checker.
- `P19-KEPS-001` and `P19-SST-001` inherit a decided shape and an unspent budget:
  no field to delete, no enumerator to make reachable, no treatment record to
  un-generalise. If P19 v1 stays laminar, this milestone leaves nothing behind
  that has to be removed.
- The turbulence ownership checker is the first gate in the tree that guards
  work which does not exist, and it will be widened TWICE, by two different
  milestones, for two different reasons. Saying so is the point: the first draft
  of this line said only that widening it "is the visible act of authorizing a
  model", and an adversarial review pointed out that this mis-describes the
  widening that happens FIRST.
    1. `P19-VALID-001` implements the refusal decision 10 demands. To refuse
       turbulence by name it must NAME it -- a diagnostic code naming turbulent
       flow, and a threshold constant -- so it trips rule 1 and rule 2 of a gate
       this ADR wrote. That is implementing a refusal, which is the OPPOSITE of
       authorizing the thing refused, and it needs an allow-path for the
       validation owner, not a relaxation. `L12` records the shape.
    2. `P19-KEPS-001` adds a model's quantities. THAT widening is the visible act
       of authorizing turbulence, and it also turns the fourteen absence gates
       red, so the milestone has to come and delete the gate that said there was
       none.
  Two edits, to two different parts of the checker, meaning two different things.
  A gate that could not tell them apart would make the second look like the
  first.
- No persisted identity changes. The golden digest
  `cfd-definition-v1:20707079b32e4964` is untouched, which is the check that
  decision 1 was really taken.
- `ADR-078` `L7` / `ADR-077` `L12` are NOT addressed here and are not made worse:
  the convection scheme and the gradient method remain hard-coded outside the
  fingerprint. This ADR deliberately does not fix them, because doing so would be
  the selector C1 forbids, for two settings with one qualified value each — and
  because that is the owner's open scope decision, recorded in `TODO.md`.

## Verification

The architecture is a claim about absence, so it is verified the way `ADR-058`
prescribed — *"each unsupported item stays absent from the types, which the
compile-fail groups … assert"* — plus:

- `architecture.turbulence-ownership`, a ctest over the source text with both
  block and line comments stripped, carrying three rules (a turbulence quantity
  attributed to its area, a laminar-regime threshold, and `virtual` under cfd),
  with ten fixture trees that must each make it fail or pass for one reason;
- the real-tree run asserts its own SCAN COUNT, not only its exit code, and the
  checker refuses a scan of zero files;
- rule 4 of the same checker bans the BARE turbulence symbols -- `nut`, `mut`,
  `epsilon`, `omega`, `tke` -- inside `cfd` only, where every one of them measures
  zero, which is what closes the hole `L2` calls this check's largest;
- count pins on `kCfdVariables`, `kCfdFields` and `kCfdBoundaryPolicies`, member
  mirrors on `CfdBoundaryTreatment` and on the three nested settings records, and
  the `toString`-of-one-past-the-end idiom for the enum cardinalities;
- a compile-fail group for the claimed absences, each case's regex naming its own
  member, enumerator, function or constant;
- the full regression. `include/` and `src/` are untouched, so every existing
  result must be bit-identical and every per-tag assertion count unchanged; the
  test COUNTS rise by the cases this milestone adds, which is the only thing
  about the suite that may move.

**A gate that cannot fail is not a gate.** Each one is verified by making it fail
on purpose before it is believed, which is the practice `P19-TRANSIENT-001`
established for its reconciliation.

### What an independent mutation analysis found, and what was done about it

The first version of this battery was reviewed by an independent mutation
analysis whose whole job was to find production changes that break a claim while
leaving its gate green. **It found ten, and four gates that changed meaning
instead of failing.** The findings are recorded here because `CLAUDE.md` makes a
documentation claim that exceeds its mechanism the same failure as a false
checkbox, and because two of them were defects in this ADR's own text.

Acted on, all of it in `tests/`:

```text
the compile-fail TU saw 11 of 23 cfd headers, and the 12 it missed included
CfdMomentum.hpp -- the seam decision 2 NAMES. The banned spelling declared
there left six gates green.                     -> all 23 headers included
a shared "is not a member of" regex could not tell a missing ENUMERATOR from a
missing ENCLOSING ENUM, so deleting CfdVariable left three cases green; and the
one parameter-taking case went green if CfdAnalysisDefinition was renamed. The
repository already owned the fix two hundred lines above.
                                                -> each regex names its own
                                                   identifier
the checker stripped only `//`, so an innocent `/* no kEpsilon here */` anywhere
in src/ would have turned the real-tree gate red -- the exact false-positive
failure L3 warns about, built into the gate that cites it.
                                                -> block comments stripped too
"0 violations" is what an EMPTY GLOB prints. Nothing asserted the scan read
anything, which is the defect tests/cli/ZeroMatchGuard.cmake exists for.
                                                -> a zero-scan refusal, plus a
                                                   STDOUT_REGEX on the count
the pattern set was camelCase ONLY, while every persisted key in this repository
is snake_case -- so a persistence violation would naturally be written in the
one dialect the persistence gate could not see. kCfdTransitionReynoldsNumber
also walked past rule 2.                        -> both dialects, and the
                                                   Reynolds-threshold forms
"No virtual enters cfd" was prose.              -> rule 3, measured at 0 of 46
                                                   cfd files first
a field added to a NESTED settings record defeated every gate AND the
pre-existing "the fingerprint covers every field": PermittedDefinition declares
the same member TYPES, so both sides of its sizeof grow together.
                                                -> mirrors and bindings for
                                                   CfdSteadySettings,
                                                   CfdOutputSettings and
                                                   CfdLinearSolverSettings
three assertions were tautological and one claim was simply false -- the
`cfd-definition-v1` check cannot catch a mode field, because the label is a
hardcoded literal independent of the field list.
                                                -> tautologies removed, the
                                                   label's weaker claim stated
```

Not acted on, and recorded as limitations rather than closed: `L8` to `L11`.

**Two things the analysis confirmed are right**, which is worth recording because
the same shape went wrong twice in this phase: the five member-naming gates are
correctly NARROW — rename, delete or forward-declare the enclosing type and each
goes red rather than green — and the three type-absence gates admit none of
`no matching function`, `is private` or `use of deleted function`, so the
arity-for-access meaning-change that cost `P19-PVCOUPLE-001` and
`P19-STEADY-001` cannot recur in them.

## Known limitations

```text
L1  THIS ADR DEFINES A SHAPE, NOT AN INTERFACE -- AND ONE HALF OF THAT BECAME A
    MECHANISM AFTER REVIEW. Decisions 2 and 4 say what form a model and a wall
    treatment must take and declare no types. The COMPOSITION shape -- a free
    function over plain records, summed into momentum the way ADR-073 sums
    convection and diffusion -- is still prose, and nothing stops P19-KEPS-001
    writing it some other way. But "no virtual enters cfd" is now rule 3 of the
    ownership checker, measured at 0 of the 46 cfd files first.
    THIS LIMITATION FIRST SAID THE CHECKER "GUARDS WHERE TURBULENCE MAY LIVE, NOT
    HOW IT IS SHAPED", AND CITED C7. Both halves were wrong once rule 3 existed:
    the checker does now constrain one aspect of shape, and C7 never applied --
    adding a text rule is not building a declaration before its model, so C7 was
    cited for something it does not cover.

L2  THE OWNERSHIP CHECKER IS A TEXT SCAN AND CAN BE FOOLED. It greps source with
    BOTH comment forms stripped, as the seven thermal checkers do with `//`.
    Spelling a turbulent viscosity `nut_` or computing one without naming it
    passes rule 1; INSIDE cfd rule 4 closes the bare-symbol half of that, which
    is where a model would have to live. It raises the cost of a violation; it
    does not make one impossible.

L3  AND IT CAN FALSE-POSITIVE. Identifiers like `k`, `omega` and `epsilon` are
    ordinary mathematics, so the patterns must be specific or innocent code
    fails the build -- which is worse than no gate, because it teaches people to
    widen it. The patterns chosen, and the words deliberately NOT matched, are
    recorded with the checker.

L4  NO THRESHOLD, SO NO REFUSAL YET. Decision 10 fixes the shape of the
    beyond-laminar refusal and C6 forbids fixing its number, so today a
    high-Reynolds laminar solve still converges to a possibly non-physical
    answer and nothing refuses it. That gap is real, it is P19-VALID-001's, and
    naming it here is not closing it.

L5  "DEFINE THE MODE" IS ANSWERED BY DECIDING THERE IS NONE. A reader expecting a
    CfdTurbulenceMode will not find one. The checklist item is satisfied by a
    decision about where the mode lives and when it may exist, which is what C1
    leaves available; it is recorded as an interpretation rather than presented
    as the only reading.

L6  THE CHECKER GUARDS A BOUNDARY THAT HAS NO FILES ON ONE SIDE. There is no cfd
    command, persistence or CLI file for it to scan, so decisions 7, 8 and 9 are
    enforced today only by the absence of their own milestones. The checker will
    begin to bite on the day those milestones add their first file, which is the
    day it is needed.

L7  NO PERFORMANCE BASELINE, ninth milestone running. Nothing here executes.

L8  THE PATTERN SET IS FINITE AND A DETERMINED AUTHOR EVADES IT, and the breadth
    is now measured rather than guessed. An independent analysis listed about
    thirty spellings that passed the first version; most are matched now, but
    five CANNOT be matched because they occur in innocent code in this tree:
    `nut` (6 files, via "minute"), `mut` (29, via "mutable"), `tke` (10, as a
    case-insensitive substring), `closure` (8, including cfd's own
    CfdContinuity.cpp) and `RANS` (1, via "TRANSient"). `nut` and `mut` are the
    conventional symbols for a turbulent viscosity, so the most idiomatic
    spelling of the thing being banned is the one the gate cannot see. It raises
    the cost of a violation; it does not make one impossible.

L9  THE MEMBER MIRRORS COUNT DATA MEMBERS ONLY. `sizeof` and a structured
    binding are both blind to a member FUNCTION and to a static member, so
    `CfdBoundaryTreatment::wallPolicy()` or `CfdResultFields::effectiveViscosityAt()`
    would add no storage and break no pin. What stands between those and the
    claim is the ownership checker's pattern set, which is `L8`.

L10 AN ENUM'S CARDINALITY CANNOT BE ASSERTED DIRECTLY IN C++, and the three
    guards on it do not compose into a proof. `-Werror=switch` catches a fourth
    enumerator -- until someone adds a `default:` label, which nothing forbids.
    The table pins catch a widening of the tables. The `toString`-of-one-past-
    the-end idiom catches an enumerator somebody named in `toString`. An inert
    enumerator that no table lists, no switch handles and `toString` ignores
    still passes all three; it can compute nothing, since `descriptorOf` returns
    none for it, but "CfdVariable is NOT AN OPEN SET" is violated in letter.

L11 MOST OF THE TESTS TEST THE CHECKER, NOT THE TREE. Of the 30 tests this
    milestone adds, eleven point `SOURCE_DIR` at a fixture and so cannot fail for
    any change under `include/` or `src/`. Testing the instrument is right --
    without it the checker's branches would ship unexercised -- but the count
    that matters for the architecture is the other nineteen: fourteen
    compile-fail cases, four runtime cases and one real-tree scan.

L12 THE CHECKER WILL FIRE ON THE REFUSAL THIS ADR PRESCRIBES, and the fix is
    deliberately NOT built here. Decision 10 requires a beyond-laminar solve to
    be refused by name at validation; C6 forbids this milestone fixing the
    threshold. So `P19-VALID-001` will write a diagnostic code naming turbulent
    flow and a constant such as `kCriticalReynolds`, and trip rule 1 and rule 2
    together.
    THE SHAPE OF THE FIX, so that milestone inherits a decision and not a
    surprise: rule 2 becomes "no file OUTSIDE the validation owner fixes a
    laminar-regime threshold", and rule 1 gains an allow-path for that owner's
    own file -- which is exactly how `CheckValidationOwnership.cmake` already
    works for thermal, banning the deciding names outside the module and
    REQUIRING them inside it. A model's QUANTITIES stay banned there; only the
    refusal's vocabulary is admitted.
    NOT BUILT NOW, and the reason is the same one decision 2 gives for declaring
    no model interface: `CfdValidation.hpp` does not exist, so an allow-path for
    it is C7's "derived state with no consumer, built before the scheme that
    defines it". The one allow-path that survives contact with a real validator
    cannot be known before the validator. This milestone was tempted into
    building ahead of its consumer three times and refused each time; this is the
    third.

L13 THE COMMENT STRIPPER IS NOT A LEXER, and one hole is measured. `//` inside a
    STRING LITERAL erases the rest of that line: `src/io/SvgWriter.cpp` holds an
    XML namespace URL, so everything after it on that line is invisible to the
    scan. A turbulence identifier placed after a `//`-bearing literal on one line
    therefore passes. Fixing it properly needs a tokenizer, which is more
    machinery than a policy gate justifies; it is recorded instead, and no
    fixture covers it because none can without pretending the hole is closed.

L14 ONE IDENTIFIER CAN STILL TRIP TWO RULES. `_count` is incremented per rule,
    not per file, so a cfd file naming a bare symbol AND a compound would report
    two violations. The redundancy that made this likely is gone -- rule 2's
    `turbulentOnset` alternative duplicated rule 1's `turbulen` and was removed
    after review -- but the arithmetic is still per rule. It overstates a count;
    it never hides one.
```
