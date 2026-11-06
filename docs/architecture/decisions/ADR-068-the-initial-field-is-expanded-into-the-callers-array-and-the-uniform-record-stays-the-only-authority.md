# ADR-068 — The initial field is expanded into the caller's array, and the uniform record stays the only authority

```text
Status:     Accepted
Date:       2026-11-05
Milestone:  P19-IC-001
Supersedes: nothing
Amends:     nothing. It DISCHARGES ADR-066 Decision 1, which deferred the
            ownership of field values to "the first milestone that computes a
            field, with a use in hand". This is that milestone, and the answer
            it reaches is that no ownership is added yet -- see Decision 2.
            It applies ADR-058's uniform shape, ADR-061's canonical record and
            ADR-066's layout; none is changed.
```

## Context

`P19-DATA-001` already defined the canonical initial condition, and this
milestone does not declare it again:

```cpp
struct CfdInitialCondition {
    Velocity3 velocity{};
    Pressure  pressure{};
};
```

It is a member of `CfdAnalysisDefinition`, it is validated for finiteness, it
is written into the definition fingerprint by `canonicalText`, and
`core/Id.hpp` records that there is deliberately no `CfdInitialConditionId`
because there is exactly one record per analysis. `ADR-061` authorized that
shape and gave the reason: "A per-cell field would be mesh-local state inside
canonical intent and a field from an earlier result would be derived state
promoted to authority; one value per quantity is neither."

So the canonical half of this milestone's checklist — initial velocity, initial
pressure, the uniform-field policy, SI units, NaN/Inf rejection — is **largely
pre-existing**, and the evidence for this milestone says so plainly rather than
claiming the work. What does not exist is everything downstream of that record:

```text
nothing expands the uniform record into an array a solve could read
nothing binds such an array to the mesh generation it was expanded for
nothing states which variables may carry an initial condition at all
nothing structurally prevents a per-cell array being cached as authority
```

`ADR-066` left exactly one question open, in these words:

> THERE IS STILL NO FIELD STORAGE HERE (ADR-066, Decision 1). This file holds
> counts, translations and a binding. It holds no pressure, no velocity and no
> flux, and it is not a container: a field is the caller's own `std::span`,
> whose LENGTH comes from `size(CfdVariable)` and whose SUBSCRIPT has a type.
> The first milestone that computes a field decides how the values are owned,
> with a use in hand.

This is the first milestone to produce a **layout-indexed field array** — one
whose length comes from `size(CfdVariable)` and whose subscript comes from the
layout — and so the first for which "who owns the values" is a live question.
The precision matters, because ADR-066's own Candidates section named the
expected first consumers as "a boundary condition filling a patch's run
(P19-BC-001), a momentum predictor wanting three contiguous component blocks
(P19-MOM-001)", and `P19-BC-001` shipped before this milestone. It did produce
per-boundary-face values, but it held them as `CfdBoundaryFace` **records**
inside `PreparedCfdBoundary`, never as an array the layout subscripts, so it
never faced this question. An earlier draft of this paragraph said "the first
milestone that computes a field", which a reader could check against ADR-066 and
find wrong.

The clause that decides the answer is **"with a use in hand"**, and the honest
position is that there is no use in hand: the only consumer of an initial field
is a solve, and `P19-STEADY-001`, `P19-TRANSIENT-001` and `P19-LINSOLVE-001` are
all later and none is authorized. A milestone that invented a container now
would be choosing an ownership model for a consumer whose requirements nobody
has read.

## Constraints

```text
ADR-061   the canonical initial condition is ONE uniform record per analysis
ADR-058   the initial field is derived state and is never persisted
ADR-066   a cell-centred vector is component-blocked, comp * cells + cell
ADR-066   an array's length comes from size(CfdVariable), not from arithmetic
          the caller performs
ADR-066   cell fields survive BoundaryLayoutChanged; boundary fields do not
ADR-032   a reference that does not resolve is a named refusal, never a rebind
cfd is layer 42 and may include neither thermal (45) nor structural (50)
```

Two constraints come from outside the architecture and are just as binding.
`P19-PERSIST-001` owns persisting initial conditions to a file — the words
"initial conditions" are in its own Persist block — and `P19-CMD-001` owns
"Undo exact" and "Redo exact". Both are later and **neither is authorized**, so
this milestone may not implement either, however much its own checklist line
"Exact undo / persistence semantics" might be read as inviting it. Decision 9
states what that line can honestly mean here.

## Options

### Who owns the expanded array

**A. A container inside the prepared object.** `PreparedCfdInitialField` holds
`std::vector<double>` for pressure and velocity, expanded at prepare time and
exposed as spans. Mirrors `PreparedCfdBoundary`, which holds its per-face
vector.

**B. Expansion into the caller's array.** The prepared object holds the uniform
values, the layout and the binding, and **no array**. A separate call writes the
derived values into a `std::span<double>` the caller owns and sized from
`size(CfdVariable)`.

**C. Return by value.** A free function returns `std::vector<double>`.

Compared:

```text
                      A container         B caller's span    C by value
correctness           equal               equal              equal
ADR-066 Decision 1    pre-empts it        DISCHARGES it      pre-empts it
field storage added   yes, per analysis   none               none, but
                                                             allocates per call
"expanded arrays      a promise -- a      STRUCTURAL -- cfd  a promise
 remain derived"      vector is one edit  owns no field
                      from being          memory at all
                      persisted
solver fit            unknown: the        the solve brings    unknown
                      solve's allocator,  its own storage,
                      alignment and       alignment and
                      lifetime are not    lifetime
                      known yet
peak memory           two copies for a    one                two during the
                      consumer that                          return, in the
                      holds its own                          general case
over/under-sized      not expressible     REFUSED by name    not expressible
 array                (it owns them)                         (it owns it)
```

B wins on the two that matter. It is the only option that **discharges**
ADR-066 Decision 1 rather than pre-empting it, and the only one under which
"expanded cell arrays remain derived" is a structural fact rather than a
promise: if the cfd module owns no field memory, no future edit can
accidentally persist a field, because there is no field to persist. Its cost is
real and is accepted — a consumer must size and own its array, and the
wrongly-sized array becomes a case that must be refused by name rather than
being impossible.

A would have been right if a solve existed and wanted the values cached across
iterations. It does not.

### Whether a uniform initial pressure is hydrostatic

**A. Uniform, as the record says.** Every cell gets `pressure`.

**B. Hydrostatic**, `p_0 - rho |g| (h - h_ref)`, matching what `ADR-067`
Decision 7 does to a *prescribed boundary* pressure.

B is physically the more plausible starting guess in a gravity-driven case and
was argued for on exactly that ground. It is rejected, for three reasons that
compound:

```text
the record is authorized UNIFORM (ADR-061). A derived field that varied cell to
  cell would not be an expansion of that record, it would be a different field
  wearing its name, and the checklist item "Uniform-field policy" could not
  then be ticked honestly
it makes density and gravity dependencies of the initial field, so a material
  edit or a gravity edit would stale it -- the exact cost ADR-067 Decision 8
  refused to pay in a case where the value provably cannot change
a boundary pressure is a CONSTRAINT the solve must satisfy, so it must be
  physically consistent along a face that is not level; an initial field is a
  STARTING GUESS, and its only requirement is to be a legal state
```

The asymmetry with ADR-067 is therefore deliberate, and it is recorded here
because a reader who knows that ADR will otherwise read this as an omission.
The consequence is accepted and stated: a gravity-driven case starts further
from its solution than it needs to. That is a convergence matter for the solve
milestones, which may add a hydrostatic *initialization policy* under Decision
8's rules; it is not a correctness matter here.

## Decision

**1. The canonical record is P19-DATA-001's, unchanged.** This milestone adds
no canonical type, no second initial-condition record and no identity. It
strengthens the *validation coverage* of the existing record where a gap is
measured (see Verification), and otherwise leaves it alone.

**2. No field storage is added. The expansion writes into the caller's array.**

```cpp
Result<void> expandCfdInitialField(const PreparedCfdInitialField& field,
                                   CfdVariable variable,
                                   std::span<double> values);
```

The caller sizes `values` from `PreparedCfdInitialField::size(variable)`, which
is the question the length gate itself asks — **not** `CfdFieldMap::size`, which
this ADR named in an earlier draft. The two differ for `MassFlux`: the layout
answers `faces`, the prepared field answers 0, because a mass flux is not a
variable an initial field can carry at all. Both end in a named refusal, so the
discrepancy was harmless, but a caller following the ADR would have sized an
array the gate then rejected.

This discharges ADR-066 Decision 1: the answer is that ownership stays with the
consumer until a consumer exists to state its requirements.

**3. `PreparedCfdInitialField` holds no array.** It holds the canonical record,
the layout and the source binding. A mirror `static_assert` in the tests fixes
its member list against a struct naming the members that MAY appear, so an added
member breaks the build — the instrument `CfdFieldMap` already uses, and the one
that makes Decision 2's guarantee enforceable rather than merely stated.

**4. A velocity expands component-blocked, through the layout's own
arithmetic.** The implementation asks `CfdFieldMap::vectorIndex(cell,
component)` for every slot it writes; it does not compute `comp * cells + cell`
itself. One copy of that arithmetic exists in this repository and it is
`CfdFields.cpp`'s.

**5. Only cell-centred variables carry an initial condition. `MassFlux` is
refused by name.** A mass flux is face-centred, and `ADR-067`'s frozen table
already makes `phi` `Computed` at every boundary kind: it is an outcome of the
velocity field, never a datum. Initializing it would be prescribing a value the
solve derives.

**6. A uniform initial field is uniform. Gravity does not enter it.** See the
Options comparison above.

**7. The source binding carries the body, the mesh stamp, the cell count and
the canonical record; `prepareCfdInitialField` additionally refuses a definition
that names another mesh control.** Every omission is a decision, and each has a
reason that is a provable statement about what cannot change the expanded array.

An adversarial review found that `CfdAnalysisDefinition::mesh` was a **seventh
omission with no such reason available**, and supplied the case: re-point an
analysis at another mesh control and the field reports itself CURRENT, because
the source is `{body, stamp, cells, record}` on both sides — for an analysis
whose field must now cover a different number of cells. It is canonical intent,
it moves the definition fingerprint, and it provably changes the array's LENGTH.
It is therefore not omitted: it is checked, as `CfdInitialFieldProblem::`
`DefinitionNamesAnotherMesh`, first in phase order, because a caller who handed
over the wrong analysis is not helped by being told its pressure is NaN. (The
sibling `CfdBoundarySource` omits it too, which is a pattern-level gap this
milestone does not reach into and records for the owner instead.)

The six remaining omissions, each with its reason:

```text
carried   body              the field's subject; a diagnostic that said "Mesh"
                            for a repointed analysis would mislead
carried   mesh stamp        a remesh of the same body with the same counts has
                            a different stamp, and that alone stales it
carried   cells             corroboration, as ADR-065 and ADR-066 use it: a
                            caller that got this wrong would index past the end
                            of an array
carried   initialCondition  the values themselves, by value, compared
                            field-complete by = default

omitted   geometry          the expanded array depends on the layout and the
                            values; a geometry edit that does not remesh
                            changes neither. CfdFieldMap::currency does not
                            compare the geometry revision either, for its own
                            reason, so carrying it here would let an initial
                            field be stale against the very layout it was
                            built from
omitted   boundary faces    ADR-066 Decision 5: a cell field survives
omitted   patch partition   BoundaryLayoutChanged. An initial field that staled
                            on a patch reorder would be claiming a dependency
                            it provably does not have
omitted   fluid             density and viscosity do not enter a uniform field.
                            This follows from Decision 6 and would be false
                            under the hydrostatic option
omitted   gravity           as above
omitted   reference         a pressure datum fixes the LEVEL the solve
          pressure          converges to, not where it starts
```

So the stale reasons are three — `Body`, `Mesh`, `InitialCondition` — and not
the boundary's six. Each is asserted ALONE, with the other dependencies held
equal, after a review found that the stamp operand of the mesh comparison had
never been separated from the cell-count operand.

**8. A future field-based initialization must enter as canonical,
mesh-independent intent.** The policy, stated now because the extension point is
being created now:

```text
PERMITTED   a named source resolved afresh for the current mesh: "initialise
            from analysis X's result", "hydrostatic from the gravity vector",
            "a function of position". Canonical, mesh-independent, and
            re-derivable for any mesh of the body
FORBIDDEN   an array of per-cell values in canonical intent, under any
            spelling. It is mesh-local state promoted to authority -- ADR-061's
            words -- and it would be orphaned by a remesh with no way to say so
```

What this milestone builds to make that cheap later: the expansion's signature
takes a `CfdVariable` and a span, so a field-based policy changes only *which
values are written*, not the ownership, the API shape or the binding. No enum
with one value and no policy hook is added today, because an abstraction with
one case and no second caller is the speculative kind this project's rules
refuse.

**9. "Exact undo / persistence semantics" means the value semantics and the
fingerprint, and this milestone implements neither a file format nor a
command.** `P19-PERSIST-001` owns the file round trip and `P19-CMD-001` owns
undo and redo; both are unauthorized. What is in scope, and what is verified
here:

```text
the canonical record is a pure value: two quantities, defaulted equality, no
  pointer, no handle, no mesh-local member, and a mirror static_assert fixing
  its size
canonicalText is LOSSLESS for it -- std::format("{}", double) is the shortest
  decimal that reads back as the same double -- and CONSISTENT with equality,
  because -0.0 is normalised to 0.0 exactly as operator== treats the two as
  equal. So two definitions differing in any initial-condition value have
  different canonical TEXTS, and two that compare equal never do.
  THE DIGEST IS NOT LOSSLESS AND IS NOT CLAIMED TO BE: `fingerprint` is 64-bit
  FNV-1a over that text, so it distinguishes two records only to the width of a
  hash. An earlier draft of this ADR put the lossless claim on the fingerprint,
  which a review corrected; the tests assert distinct canonical texts, which is
  where the strong claim can live
setDefinition is atomic and reports "no effective change" exactly, so there is
  nothing for a later undo to restore when nothing moved
the derived field is NOT in the definition, NOT in the fingerprint and NOT
  clonable into the document, so the canonical / derived boundary a later
  persistence milestone must respect is already exact
```

The distinction is the point: this milestone makes exact undo and exact
persistence *possible and testable*, and says plainly that it does not perform
them.

## Consequences

Easy: a solve brings its own storage and asks for the values once; a consumer
that already holds arrays writes into them with no copy; a wrongly-sized array
is a named refusal instead of undefined behaviour; a boundary reorder does not
stale an initial field, so a patch rename costs no reinitialisation.

Hard, and accepted: every consumer must size its own array, and the first one to
want the values cached across iterations will have to decide the ownership that
Decision 2 defers — which is the point, not a debt. A gravity-driven case starts
from a non-hydrostatic field, as Decision 6 records.

Committed to: one copy of the blocked-index arithmetic; the uniform record as
the only authority for an initial state; and the rule that a per-cell array
never becomes canonical intent.

## Verification

```text
Decision 2, 3  a mirror static_assert fixes the prepared object's member list;
               the expansion is exercised through a caller-owned span, and the
               over-sized and under-sized spans are refused by name
Decision 4     the COMPONENT-to-block mapping is pinned: every written slot is
               checked against CfdFieldMap::vectorIndex and independently
               against an oracle recomputing comp * cells + cell, and a
               mutation to interleaved indexing does fail the test (measured).
               The CELL-to-offset mapping within a block is NOT pinned and
               cannot be: an adversarial review replaced the loop's
               CellIndex::fromValue(c) with fromValue(cells - 1 - c) and all
               tests passed, because a UNIFORM field's array is byte-identical
               under any permutation of cells. That is inherent to this
               milestone's shape, not a gap in the suite, and it is recorded
               here rather than claimed away. What carries the cell arm is that
               the subscript comes from CfdFieldMap::vectorIndex, whose own
               numbering P19-DOF-001 qualified; the first milestone to write a
               NON-uniform field will be the first able to observe it
Decision 5     expanding MassFlux is refused by tag
Decision 6     the expanded pressure is asserted equal in every cell on a mesh
               whose cells span a height range, with gravity present in the
               definition
Decision 7     each omitted dependency is edited and the field asserted STILL
               CURRENT; each carried one is edited and the field asserted stale
               with the right reason
Decision 9     the canonical TEXT is asserted to change for a change in any one
               of the four initial-condition values -- including between two
               adjacent subnormals and between the two largest finite doubles,
               which is where "shortest round-trip" is least obviously true --
               and to be equal for two definitions that compare equal,
               including -0.0 in all four slots. The digest is asserted
               alongside but carries no exactness claim. "Not clonable into the
               document" is checked by cloning the analysis object and
               comparing definitions and fingerprints
Decision 1     and the validation coverage it claims to strengthen: all three
               velocity components and the pressure, against NaN, +Inf and
               -Inf, twelve cases; and -- after a review measured that weakening
               isFinite to a magnitude bound survived all twelve -- that a
               finite EXTREME is ACCEPTED and expanded bit for bit
```

Evidence: `docs/verification/P19-IC-001/README.md`.
