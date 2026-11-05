# ADR-067 — CFD boundary conditions resolve through P16 alone, and the per-field treatment is a frozen table

```text
Status:     Accepted
Date:       2026-11-05
Milestone:  P19-BC-001
Supersedes: nothing
Amends:     nothing. It applies ADR-058's boundary set, ADR-063's patch and
            source-binding rules and ADR-066's field layout; none is changed.
```

## Context

`ADR-058` authorized six boundary condition kinds and said "P19-BC-001
qualifies each; a condition it cannot qualify is refused, not approximated".
`P19-DATA-001` then defined all six as canonical records in `CfdData.hpp`, each
naming a CAD face by `FaceName` and holding typed physical values — so the
vocabulary already exists, and this milestone does not declare it again.

What does not exist is the step from that intent to a particular mesh. Four
earlier milestones wrote down what this one owes, and those sentences are the
requirement, not a suggestion:

```text
CfdAnalysisObject.hpp:100  the input gate "refuses unassigned boundary facets
                           (P19-BC-001), by calling this function"
CfdAnalysisObject.cpp:150  "P19-BC-001 refuses a facet that two conditions claim"
CfdData.hpp:157            every boundary facet carries exactly ONE condition;
                           a velocity points INTO the domain on every facet; a
                           moving wall's velocity is TANGENTIAL
CfdData.hpp:190            backflow at a pressure outlet is "P19-BC-001's policy
                           to state, not a default"
CfdData.hpp:345            whether the reference point lies in the fluid is
                           "answered where the datum is resolved (P19-BC-001)"
CfdData.hpp:370            under gravity a prescribed boundary pressure varies
                           hydrostatically. "P19-BC-001 applies it."
CfdMesh.hpp:222            an unnamed patch can carry no condition, so this
                           milestone "will refuse the analysis if one exists"
ADR-063 Decision 3         one name may cover several patches, and one patch may
                           carry several names. NO NEAREST-FACE FALLBACK.
```

Two precedents already solved the resolution half, in two modules, the same
way: `structural::resolveFaceTarget` and `thermal::resolveFaceTarget`.
`ThermalTarget.hpp:14-18` records what it cost to defer extracting it — the
checks were written inside one consumer's anonymous namespace, the next
consumer needed them, and extracting them turned a qualified production file
into a changed one that had to be requalified. CFD is layer 42 and cannot
include `thermal/` (ADR-059), so reuse across the boundary is not available.

## Decision 1 — A prepared boundary is a derived value with one friend, and the gate is staged

```text
PreparedCfdBoundary   a VALUE. Possession proves every condition resolved, every
                      facet carries exactly one condition, every prescribed
                      value is valid against the mesh, and the whole set was
                      prepared against the mesh `source()` names.
                      Private default constructor, exactly one friend.
                      Never persisted, never a member of anything canonical.
```

Candidates:

```text
(a) a free function returning a plain struct anyone can build        rejected
(b) a prepared value with one friend, staged after mesh/topology/
    geometry, taking the definition by const reference                CHOSEN
(c) a member of CfdAnalysis, built on edit and cached                 rejected
```

(a) is rejected for the reason `ThermalModel`, `CfdModel`, `CfdMeshContract`,
`CfdTopology` and `CfdFieldMap` were each given the same device: a consumer
that receives a `const PreparedCfdBoundary&` must not have to re-check
anything, and a caller that wants to skip the checks must not be able to
construct the argument. This is the sixth use of the pattern in this codebase
and the reason has not changed.

(c) is rejected because it would put derived, mesh-bound state inside canonical
state, which `ADR-061` forbids and `ADR-066` Decision 7 re-proved for fields. A
remesh would orphan it, and the document would then hold something that claims
to describe a mesh it does not.

The gate's inputs are the four proofs and the definition:

```cpp
Result<PreparedCfdBoundary> prepareCfdBoundary(
    const CfdModel& model, const CfdTopology& topology,
    const CfdGeometry& geometry, const CfdAnalysisDefinition& definition);
```

`CfdGeometry` is required and is not an indulgence: the outward normal decides
whether an inlet points into the domain (Decision 5) and the facet centroid
decides a prescribed pressure under gravity (Decision 2). Deriving a normal
here from the topology's winding instead would be a second implementation of
the quantity `ADR-065` made primary, and two implementations of one normal can
disagree.

It takes the definition by const reference and reads `boundaryConditions`,
`referencePressure` and `gravity` and nothing else, so a solver, iteration,
output or initial-condition edit cannot change the answer.

THE TEST SEAM IS `bindCfdBoundary`, which takes a plain topology, a plain
geometry, the mesh, a span of already-resolved targets and the datum records —
not a span of conditions, as a first draft of this decision said. It does no
resolution, so a value it returns carries a DEFAULT source and `isCurrent`
against any real source is false; that is the conservative direction, and it is
stated in the header rather than left to be discovered. `bindCfdBoundaryFailure`
is its sibling and returns the problem TAG, because a review found that a seam
returning only an `Error` let eight refusals' tags be swapped for one another
with every message assertion still passing.

## Decision 2 — The per-field treatment is a frozen table with FOUR values, not two

The checklist asks for "zero-gradient policies" and "fixed-value policies". Two
values cannot express this boundary set, and the table is frozen here:

```text
kind              p                U                       phi
----------------  ---------------  ----------------------  ----------------
VelocityInlet     ZeroGradient     FixedValue  -v n_hat    FixedValue
PressureInlet     FixedValue  p_b  ZeroGradient            Computed
PressureOutlet    FixedValue  p_b  ZeroGradient            Computed
NoSlipWall        ZeroGradient     FixedValue  0           FixedValue  0
MovingWall        ZeroGradient     FixedValue  w           FixedValue  0
SlipWall          ZeroGradient     Symmetry                FixedValue  0
```

```text
FixedValue    the value is prescribed (Dirichlet)
ZeroGradient  the normal derivative is zero (homogeneous Neumann)
Symmetry      the NORMAL component is zero and the TANGENTIAL components are
              zero-gradient -- one condition on a decomposition of the vector
Computed      an OUTCOME of the solve, not an input
```

`Symmetry` exists because a slip wall's velocity is neither of the first two.
Calling it `FixedValue` would claim the tangential velocity is prescribed when
it is an outcome; calling it `ZeroGradient` would admit flow through the wall.
`Computed` exists because `ADR-058` says of a pressure inlet, in as many words,
that "the flow rate is an OUTCOME of the solve, not an input": a table marking
it `FixedValue` with a placeholder would be a fabricated boundary value, and
one marking it `ZeroGradient` would be a different and wrong discretization.

Candidates:

```text
(a) two values, with slip and phi special-cased in each operator by
    re-reading the condition kind                                     rejected
(b) four values in one frozen table, with the prescribed value carried
    per boundary face where it is FixedValue                           CHOSEN
(c) a generic Robin form a q_f + b dq/dn = c, coefficients per face    rejected
```

(a) is rejected because it does not represent a symmetry plane at all, so every
operator would re-derive the treatment from the kind — which is the table's
whole purpose. The first operator that forgot would silently admit through-flow
at a symmetry plane: a wrong answer that still converges.

(c) is rejected for this milestone though a later one may want it. The
coefficients ARE a discretization, and `src/cfd/CMakeLists.txt` records that
this module links no linear algebra and that admitting a matrix is
`P19-LINSOLVE-001`'s decision. Storing `(a, b, c)` per face would be derived
state with no consumer, built before the scheme that defines it.

### The prescribed values, and which of them are stored

```text
p    STORED PER BOUNDARY FACE. Under gravity ADR-058 puts the hydrostatic part
     inside p, so CfdData.hpp:370's rule applies per facet:
         p_f = p_b - rho |g| (h(x_f) - referenceHeight)
         h(x) = the height of x along -g from the model origin, in metres
         x_f  = the facet centroid
     Without gravity there is no height and the value is uniform. It is stored
     because it is genuinely per-face and not recoverable from the condition
     alone.
U    STORED PER BOUNDARY FACE for a velocity inlet, because `-v n_hat` varies
     over a curved face. Constant for the two wall kinds and stored uniformly
     with them, so one accessor answers for every FixedValue velocity.
phi  NOT STORED. Where it is FixedValue it is either exactly zero (both walls
     and slip) or `rho (U . S_f)` from the stored U and ADR-065's area vector.
     A stored copy would be a second thing to keep true, and ADR-065 made the
     area vector primary precisely so that consumers multiply rather than
     cache. Thermal took the same decision for a convection facet's area.
     A consumer that wants the number has the model, so it has rho.
```

The three locations of `ADR-066`'s frozen table are what these three rows are,
and no fourth variable is introduced: `CfdVariable` is not an open set, and a
boundary condition has to be expressible without extending it. This milestone is
the first consumer of the `BoundaryFace` location that table gave its own index
range, which is what `ADR-066` Decision 2 said it was for.

## Decision 3 — Exactly one condition per boundary face, and the four refusals that follow

`ADR-058`'s rule, which `CfdData.hpp:157` restates, is that every boundary
facet carries exactly one condition. That single rule generates four refusals,
and all four are this milestone's:

```text
FacetWithoutCondition   a boundary face no condition claims. A flux would leave
                        through a face nothing prescribes.
FacetClaimedTwice       a boundary face two conditions claim. Refused, never
                        resolved by order, kind or precedence: "not the first,
                        not the last, not the mean" is thermal's phrase for the
                        same situation and the reason is the same -- a silent
                        choice between two user intentions.
UnnamedPatch            a mapped CAD face with no FaceName. No GeometryReference
                        can reach it, so no condition can cover it, so coverage
                        is impossible. CfdMesh.hpp:222 already assigned this
                        refusal here and CfdMeshContract::unnamedPatches()
                        already counts them.
PressureDatumMissing    nothing fixes the pressure level (Decision 7).
```

Candidates for the overlap rule:

```text
(a) refuse any facet two conditions claim                              CHOSEN
(b) last-writer-wins in the definition's order                        rejected
(c) a precedence order over the six kinds                             rejected
```

(b) and (c) are rejected together: both silently discard one of two things the
user asked for, and both make the result depend on an ordering the user did not
know was significant. `CfdAnalysisObject.cpp:150` already anticipated (a).

NOTE THE DIFFERENCE FROM THERMAL, WHICH IS PHYSICS AND NOT TASTE. Thermal
permits a prescribed temperature and a convection on one face, because the two
are a constraint and a physical exchange and both are real. It also adds two
convections on one face, because two exchanges superpose. No two CFD boundary
conditions superpose: each of the six fully determines the velocity and
pressure treatment of a face, so two on one facet is a contradiction rather
than a sum. Identical values are NOT deduplicated either, unlike thermal's
identical temperatures, because two conditions with equal payloads still have
two identities and the user asked for two things.

## Decision 4 — Resolution is P16's alone, through a third copy of the three-check resolver

`cfd/CfdTarget.hpp` carries `cfd::TargetProblem` and the pair
`resolveFaceTarget` / `faceTargetProblem`, one pass behind both so they cannot
drift. It takes a `GeometryMeshMap` and a `FaceName` and nothing else.

```text
SelectorInvalid   core's own validate(FaceSelector) refused. Not a second opinion.
Unresolved        the reference names no face of the body as it is now.
                  NOTHING NEARBY IS SUBSTITUTED.
WithoutFacets     it resolved, and the mesh attributed no facet to it. Not an
                  empty condition: a model whose attribution chain lost a face.
```

THREE CHECKS AND NOT TWO, because `BoundaryFacetSet::fullyResolved()` inspects
`state` only: a `FaceName` that lands on a real CAD face the mesher gave no
facet comes back `Resolved` with an empty `facets`. An unresolvable reference
is a STATUS and not an error — `boundaryFacetsOf` returns success — so a
resolver that treated a refusal as the only failure mode would misclassify it.

A third copy is deliberate and is the established precedent, not a violation of
it: `ThermalTarget.hpp:9-12` records that `structural::resolveFaceTarget` is the
same function and could not be reused across the layer boundary either. Writing
it now, before the first consumer needs it twice, is what that file's own
header advises.

**No nearest-face fallback anywhere.** No distance to a plane, no normal
comparison for the purpose of matching, no centroid test, no tolerance. If P16
says a name resolves to nothing, it resolves to nothing. The outward normal
Decision 5 computes is used to VERIFY a condition the user gave, never to
choose which face a condition belongs to.

## Decision 5 — Orientation IS verified, PER TARGET, which departs from thermal

Thermal computes no normal at all: both its boundary terms depend on the
unsigned area, so winding cannot change them. CFD cannot inherit that. Two of
the six kinds make a claim about direction that the geometry can refute:

```text
VelocityInlet   normalSpeed is positive INTO the domain, so the prescribed
                velocity is `-v n_hat`. There is nothing to refute per facet --
                the direction is CONSTRUCTED from the outward normal rather
                than supplied -- so what is verified is that every facet of the
                target has a usable outward normal, and the inflow sign is then
                true by construction. Recorded as a finding of this milestone:
                the checklist's "boundary orientation verified" is satisfied for
                an inlet by construction plus a usable-normal check, not by
                comparing a user vector against a normal.
MovingWall      velocity must be TANGENTIAL (CfdData.hpp:211: the wall does not
                move, there is no moving mesh, so a normal component would be
                flow through a wall). This IS refutable and is refused by name
                (`WallVelocityNotTangential`) when |w . n_hat| exceeds the
                tolerance below, on any facet of the target.
```

### The correction a review forced, and why the check is per target

A FIRST DRAFT TESTED TANGENCY PER FACET at `1e-9`, with the reason that the
tolerance had to absorb a meshed facet's geometric approximation. An adversarial
review showed the reason refutes the number. On a tet-meshed cylinder the axial
component of a facet's unit normal is of the order of the TESSELLATION ANGLE,
not of round-off: at a mesh angle of 0.3 rad it is about 9e-2, seven orders of
magnitude above the threshold. So the one constant velocity that IS tangential
to a cylinder — an axial one — would have been refused, with a diagnostic saying
the velocity is not tangential when it is.

Either the tolerance or its rationale had to go. Candidates:

```text
(a) loosen the tolerance until a meshed cylinder passes          rejected
(b) require the target to be PLANAR, refuse it by name when it
    is not, and decide tangency against its one normal           CHOSEN
(c) test the NET flux through the target instead of the
    pointwise projection                                         rejected
```

(a) is rejected because the tolerance that admits a cylinder's tessellation
also admits a velocity tens of degrees off tangential, which is through-flow at
a wall — and `ADR-058` forbids approximating what cannot be qualified.

(c) is rejected because `sum_f (w . S_f)` over a closed-ish curved patch is
nearly zero for ANY constant `w`, so it would accept a velocity that points
straight through the wall at two opposite points. It measures the wrong thing.

(b) CHOSEN. `WallTargetNotPlanar` refuses a curved target by name, and nothing
in v1's scope is lost: `ADR-058` authorizes a moving wall for a driven cavity's
lid and a Couette plate, and both are planar.

```text
planarity   1 - n_f . n_0  <=  1e-9, over the target's facets
tangency    |w . n_0| / |w|  <=  1e-9, against the common normal
```

Both numbers now sit between measured margins rather than beside a sentence.
A planar CAD face's facets lie in its plane, so `1 - n_f . n_0` is about 1e-16;
two facets of one tetrahedron differ by more than 1e-3. A velocity in the plane
gives a projection below 1e-15; one degree off tangential gives more than 1e-2.
The suite asserts all four of those numbers, which is what the earlier draft of
this section claimed and did not do.

A facet whose outward normal is absent (a degenerate facet `ADR-065` could not
normalise) is refused by name (`DegenerateFacet`) rather than skipped, for the
reason thermal gives for a zero-area triangle: never a zero contribution.

## Decision 6 — Cardinality is forward, assigned and REVERSE-ATTRIBUTED, and the third is the one that can disagree

Following `renderer/ThermalOverlay.hpp:213`'s shape, which exists for the same
purpose one layer up:

```text
expected   the facets P16's mapping resolved the condition's FaceName to
           (the FORWARD answer, through boundaryFacetsOf)
assigned   the boundary faces the prepared binding actually gave that
           condition (the REVERSE reading, off the binding)
```

A THIRD COUNT, BECAUSE A REVIEW SHOWED THE FIRST TWO CANNOT DISAGREE:

```text
attributed  of the boundary faces the binding gave the condition, how many sit
            on a CAD face whose names include the condition's own FaceName --
            read through `sourceFaceOf`, the REVERSE direction of P16's map
```

The draft of this decision said "the two must agree for every condition", and
that is true in the empty sense: `assigned` is counted off the same claim array
that `expected` filled, and a repeated facet is refused by `FacetClaimedTwice`,
so the facet-to-boundary-face map is injective on the claimed set and
`expected == assigned` holds identically on every success. A record that cannot
be wrong is not a check, and the adversarial review was right to say so.

`attributed` is the count that can differ, because it comes from the other
direction of the map. A binding that gave a condition facets its own target does
not contain shows up here as a smaller number — which is the failure the draft
claimed to exclude and did not. It is ABSENT on a seam-built value, which has no
mapping to ask, and `agrees()` ignores it when absent.

`agrees()` is a predicate of the counts, and this decision no longer claims
otherwise: an earlier draft said `complete()` was "a predicate of the recorded
status and not of a count", naming a function that does not exist. The counts
are recorded per condition so a consumer can present them and a reviewer can
see the arithmetic, and the suite asserts the three-way agreement on every
reference mesh.

## Decision 7 — The pressure datum: refuse BOTH arms here, validate the records, and locate the point

`pressureDatumProblem` already exists and already distinguishes the two cases;
`CfdAnalysisObject.hpp:100` says the gate that refuses unassigned facets is the
one that refuses a missing datum. So:

```text
Missing          REFUSED HERE. An incompressible flow with no boundary fixing
                 the pressure determines p only up to a constant.
Overdetermined   REFUSED HERE TOO. A review found a first draft refusing only
                 the Missing arm "because validate(definition) refuses the
                 other" -- but this gate takes a PLAIN CfdAnalysisDefinition and
                 nothing on the path calls validate(definition), so an outlet
                 PLUS a reference pressure was accepted and the point located,
                 contradicting this milestone's own claim about when
                 referenceCell() is present. A consumer applying both datums
                 would have double-fixed the level.
present          the reference point is LOCATED IN A CELL, and a point outside
                 the fluid is refused by name (ReferencePointOutsideFluid).
```

THE DATUM RECORDS ARE VALIDATED HERE, for the same reason: this gate reads them
and does not assume anyone validated them. `validate(CfdReferencePressure)` and
`validate(CfdGravity)` are P19-DATA-001's own rules, and `DatumInvalid` carries
their messages. The review found a zero gravity vector — which
`validate(CfdGravity)` refuses, because no gravity is the ABSENCE of the record
and not a zero one — reaching the hydrostatic arithmetic and being reported as a
non-finite product, naming the wrong cause. `HydrostaticDensityMissing` is the
same correction for the other missing input.

The locator is new. Candidates:

```text
(a) a linear scan over cells, barycentric sign test, lowest CellIndex wins  CHOSEN
(b) an acceleration structure (BVH / octree) over the cells               rejected
(c) do not locate it; refuse only Missing and defer containment           rejected
```

(b) is rejected because the question is asked once per gate and `(a)` is
`O(cells)` with no structure to build, keep current or invalidate. (c) is
rejected because `CfdData.hpp:345` assigned the containment question to this
milestone, and a datum whose point is never checked is a treatment in name
only — the reference pressure would be accepted on a point in empty space and
fail, or silently not apply, in a later milestone.

Determinism and tolerance: the lowest `CellIndex` containing the point wins, so
a point on a shared face, edge or vertex resolves to one cell and the same cell
in every build. The test is on the four barycentric sub-volumes against a
tolerance proportional to the cell's own volume (`1e-9 * |V|`) rather than an
absolute length, because a cell's size is set by the mesh and an absolute
epsilon would be generous in a coarse mesh and meaningless in a fine one. A
point outside every cell by more than that is outside the fluid.

## Decision 8 — Source binding is a value with field-complete equality

```text
CfdBoundarySource
    ObjectId body                which solid
    meshing::MeshStamp mesh      WHICH MESH AND WHICH GENERATION
    std::size_t cells            a stamp identifies the BUILDER, not the
    std::size_t boundaryFaces    snapshot, so the counts are compared as well
    meshing::GeometryRevision geometry
    std::vector<CfdBoundaryCondition> conditions   by value
    std::optional<CfdReferencePressure> referencePressure
    std::optional<CfdGravity> gravity
    std::optional<Density> density    present EXACTLY when it entered a value
```

Equality is `= default` and field-complete, so a dependency added here without
being compared is not possible, and `staleReasons` can say WHICH moved. The
counts are carried for the reason `ThermalBoundary.cpp:443` records and
`MeshDofMap` learned first: a stamp identifies the builder rather than the
snapshot.

Gravity and the reference pressure ARE dependencies, unlike thermal's
material: both enter the prescribed values directly (Decision 2 and 7).

THE DENSITY IS A DEPENDENCY TOO, AND THIS CORRECTS AN EARLIER DRAFT OF THIS
DECISION. That draft said the density was deliberately absent because it only
scales `phi`, which this milestone does not store. That was wrong, and writing
the header is what exposed it: Decision 2 stores a per-face prescribed pressure,
and under gravity `p_f = p_b - rho |g| (h - h_ref)` contains `rho`. A material
edit therefore CAN change a prescribed pressure, and a source binding that
omitted the density would call a prepared boundary current after the fluid
changed under it.

It is carried as an OPTIONAL, present exactly when it entered a value — that is,
when gravity is present and some condition fixes the pressure. Carrying it
unconditionally would stale a gravity-free analysis on a material edit that
provably cannot change any prescribed value, which is the fault thermal names
when it refuses to carry the analysis revision. The suite asserts both arms: a
material edit stales a hydrostatic case and leaves a gravity-free one current.

Also absent: the mode, the iteration, convergence, linear-solver and output
settings, and the analysis revision — which moves on any edit and would stale
this value for a tolerance change that cannot affect it. The viscosity is absent
because no boundary value contains it.

There is no `Stale` enumerator in the problem enum, because possession of a
`CfdModel` already proves the mesh and mapping are current; staleness of a
PREPARED value is a different question, answered by `staleReasons`.

## Decision 9 — What this milestone does not build

```text
no matrix, no source vector, no operator, no solve
    src/cfd/CMakeLists.txt: "There is no matrix in this milestone. Admitting one
    is P19-LINSOLVE-001's decision." The TODO's Derived block names
    "matrix/source contributions" as derived; they are derived by a LATER
    milestone, and this one produces the resolved faces, the policy and the
    prescribed values an operator will read.
no backflow sign claim at a pressure boundary
    CfdData.hpp:190 left the policy here, and the policy is that a pressure
    outlet does not constrain the sign of phi: backflow is admitted, never
    clamped. Clamping would be the silent correction ADR-058 forbids. Whether a
    converged solution with backflow is acceptable is P19-VALID-001's question.
    This is the same statement as `Computed`.
no flux-balance check
    ADR-058 refuses incompatible prescribed fluxes, and P19-MASSBAL-001 fixes
    the numbers. Not on this checklist and not built here.
no turbulence, no temperature, no species boundary condition
    There is no field for them (ADR-058, ADR-066 Decision 2's closed table).
```

## Consequences

- `P19-VALID-001` reads a `PreparedCfdBoundary` and may add policy on top; it
  does not re-resolve names.
- The operator milestones read `treatmentOf` and the per-face prescribed
  values. An operator that ignores `Symmetry` admits through-flow at a symmetry
  plane, which is why the value exists rather than being special-cased.
- `P19-CMD-001` builds definitions one condition at a time, so a definition
  with no pressure-fixing condition is an ordinary intermediate state; only
  this gate refuses it.
- A remesh or a patch re-attribution invalidates a prepared boundary.
  `ADR-066`'s `BoundaryLayoutChanged` is the field layout's analogue, and the
  two agree because both compare the patch partition.
- `cfd::TargetProblem` is the third copy of three checks. If a fourth module
  ever needs them, the right move is a layer-0 home, not a fourth copy.

## Verification

`tests/cfd/CfdBoundaryTests.cpp` (hand-built seams, which can be shown the
configurations a document cannot produce) and
`tests/cfd/CfdBoundaryMappedTests.cpp` (the authority tests, on P16 reference
meshes), plus `tests/compile_fail/CfdBoundaryMisuse.cpp` for the claims that
should fail at build time.

- The table of Decision 2 asserted entry by entry, and `Symmetry` and
  `Computed` shown to be reachable and distinct from the other two. The
  unrecognised-kind fallback is exercised through a cast-in value, so its
  "cannot become an open boundary by accident" claim is held rather than stated.
- A prescribed pressure under gravity compared against the hydrostatic formula
  recomputed from the MESH's own nodes, on a face that is not level, with the
  spread bounded by the measured centroid heights rather than by the body's
  extent. A second gravity magnitude, so hardcoding 9.81 would fail.
- Each refusal of Decision 3 on a fixture built to fail it, including an
  unnamed patch on the bored body `CfdMesh` already uses.
- Cardinality: `attributed`, the reverse reading through `sourceFaceOf`,
  asserted equal to the forward count per condition on every reference mesh.
- Orientation: an oblique moving wall refused, a tangential one accepted, a
  curved target refused by name, and all four margins MEASURED. The inlet's
  direction is checked against the owner cell's interior — two centroids and no
  normal — because dotting it with the normal it was constructed from is a
  tautology.
- The datum: all four combinations compared against `pressureDatumProblem`, a
  point located and confirmed by the independent barycentric oracle, a point
  just outside by a measured fraction of the cell's volume refused, and a point
  on a shared face shown to resolve to the lower cell.
- No nearest-face rebind: a reference carrying a foreign `ObjectId` does not
  fall back, and one naming an entity the sketch does not have stays
  unresolved. A DELETED face is not exercised; `P16`'s own suite covers that,
  and it is recorded here as not covered rather than claimed.
- Determinism: repeated preparation bit-identical with equality shown to
  discriminate, and the diagnostics independent of the order the conditions
  were listed in.
- Source binding: every stale reason reached, three of them through a
  hand-built `CfdBoundarySource` because two documents built alike share their
  object ids and two meshes of one body share their counts. The hand-written
  comparison is checked against the field-complete `operator==`, so a
  dependency added without being compared would be caught.

## What the reviews changed

Two independent reviews ran on the frozen candidate, plus the author's own
pass. Between them they found nine defects that changed behaviour or evidence,
and every one is fixed above rather than recorded as a limitation:

```text
the declared check order was not the implemented one      fixed, mutation-verified
TargetInvalid was dead, reachable only via that disorder  enumerator removed
one resolution per target, not two, and no .error() on
  a Result that had not been checked                      fixed
the cardinality record could not disagree with itself     `attributed` added
the tangency tolerance refused a valid curved wall        per-target, planar-only
the gate accepted an Overdetermined definition            both arms refused
the datum records were never validated                    DatumInvalid added
a repeated facet was reported as two conditions           TargetFacetsNotUnique
the seam discarded the problem tag                        bindCfdBoundaryFailure
```

Three further findings were about the evidence rather than the code, and are
fixed in the tests: an inlet-orientation assertion that was a tautology, a
hydrostatic spread guard that measured distance from zero because both extremes
were seeded to zero, and a mapped fixture that picked its wall by
`listFaces` order while reasoning about which plane that face lay in.

Two of the author's own assertions were wrong about the system rather than the
code, and are corrected with their measurements recorded: a different
extrusion depth does NOT move the `GeometryRevision` (it is a mix of logical
dependency revisions, never a hash of geometry), and a facet centroid sits at
the one-third point of its facet, so the centroids of the end cap span 13.3 mm
of the channel's 40 mm rather than all of it.
