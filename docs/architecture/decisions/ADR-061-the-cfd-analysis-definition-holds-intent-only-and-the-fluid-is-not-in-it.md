# ADR-061 — The CFD analysis definition holds intent only, and the fluid is not in it

```text
STATUS:    Accepted
DATE:      2026-10-27
MILESTONE: P19-DATA-001
TOUCHES:   ADR-058 (the scope, the pressure datum and the "reference height"
           this refines), ADR-060 (the fluid is the document's P15 material),
           ADR-030 / ADR-032 (document objects; geometry references, never mesh
           ids), ADR-055 and P18-QUAL-001 finding D1 (the two currentness
           regimes, left undecided)
```

## Context

P19-DATA-001 defines the canonical and derived CFD types. P18-DATA-001 is the
template, and most of it carries over unchanged: a document object holding a
definition, identities that do not convert, a result that records its source by
value, a slot that holds one accepted result and one latest failure, and a state
machine derived from them. Seven questions do not carry over, because CFD differs
from conduction in ways the thermal template never had to answer. Each is a place
where the obvious choice creates a second authority or a hidden default.

## Decisions

### 1. The definition does not name the fluid

ADR-060 made the fluid the document's single P15 material assignment. A
`MaterialId` field in the definition would be a second statement of the same fact,
and the two could disagree: the assignment changed, the definition not, or the
reverse. Which one would the solver believe? So **the definition has no fluid
field**. The fluid reference *is* the assignment; the result's source records
which material was consumed and its revision. When per-body materials exist the
reference becomes the assignment of the analysed body, and the definition still
does not repeat it. Two tests hold this. The definition's member list is pinned
by size and by count (a structured binding of exactly twelve), so no member of
any name or size can be added unnoticed. A compile-fail case
(`THE_DEFINITION_HAS_NO_FLUID`) proves that the most likely name for one does not
exist.

The fluid *model* is a derived value type, `CfdFluidProperties`: a density and a
dynamic viscosity, resolved from P15 each time (the viscosity by P19-FLUID-001),
with the kinematic viscosity computed and never stored.

### 2. The reference pressure is a value at a point; the reference height belongs to gravity

ADR-058 made the reference pressure canonical, required if and only if no boundary
condition fixes the pressure. It also said a reference height is canonical when
gravity is on. These are two different jobs, and they get two homes.

**The reference pressure** is `CfdReferencePressure { Pressure value; Point3D
location; }`: the full static gauge pressure, hydrostatic part included, at a point
in model space.

- A **cell** would be mesh-local and orphaned by a remesh (ADR-032).
- A **height alone** does not fix the level of a pressure field that also varies
  horizontally in a moving fluid, and without gravity there is no height.
- A **point** is CAD-space intent every mesh of the body can resolve, and it fixes
  the level with or without gravity.

Whether the point lies in the fluid is a geometry question, answered where the
datum is resolved (P19-BC-001).

**The reference height** is `CfdGravity::referenceHeight`, a length measured along
`-g` from the model origin. It is always carried when gravity is present, because it
exists exactly when gravity does. It defaults to 0 m (the origin's height), so a
gravity record built from the vector alone takes that datum. Requiring the user
to state it is the command layer's job (P19-CMD-001). The hydrostatic part is inside `p` (ADR-058), so
a *uniform* prescribed pressure over a face that is not level would contradict
hydrostatics. A static-pressure inlet or outlet therefore prescribes the static
gauge pressure **at the reference height**, and along its face the pressure
varies hydrostatically, `p_b - rho |g| (h(x) - h_ref)`. Without gravity there is
no height and a boundary pressure is uniform. P19-BC-001 applies this. The
independent review (finding 1) caught that the first draft of this milestone had
no height under a pressure boundary and claimed the point covered it. It did not,
and the height was added before the freeze.

**The one inert value.** In a domain no boundary pressure fixes (a closed cavity
under gravity), the reference height has no effect, because the reference
point fixes the level. It is carried rather than made conditional: making the
shape of gravity depend on the boundary set would put a second datum rule in a
second place. This is the only canonical value made inert by the *boundary set*. Others are
inert only at particular settings, such as the residual history under a
one-iteration budget, or the sampling when the end time is one step. It is
recorded here so it is not mistaken for an oversight.

**Where the rule is enforced.** `pressureDatumProblem(definition)` is the one
implementation of "required if and only if". `validate(definition)` refuses
**overdetermined**: a reference beside a pressure boundary is inert or
contradictory whatever else is added. It does **not** refuse **missing**,
because a definition is built one condition at a time and "nothing fixes the
pressure yet" is the state of every definition before its outlet is added. A
definition with a missing datum is **storable but not Ready**:
`currentResultSource` refuses it with the named reason `pressure_datum_missing`,
by calling the same function, so the analysis is `InputsUnavailable`. That was
review finding 9, fixed before the freeze.

### 2a. A velocity inlet is a uniform normal speed in v1

ADR-058 lists a "velocity inlet". This ADR narrows it to a **uniform speed normal
to the face, positive into the domain**. A zero speed is refused, because it is a
no-slip wall with a second spelling. A negative one is refused, because that would
be an outlet that prescribes its outflow. A vector inflow (an angled jet) needs a
per-facet check that it points inward. That check belongs to P19-BC-001, and v1
does not offer a vector inflow. Widening it is its own decision.

### 3. The initial condition exists in both modes and has no identity

A steady conduction solve is linear, so P18 refuses an initial temperature on a
steady analysis as inert. A steady flow solve is a nonlinear iteration: the
starting point decides the path and the bits it lands on. So the uniform
initial velocity and pressure are **canonical in both modes**: the starting
guess of the steady iteration, and the physical state at t = 0 of a transient
one. There is one initial condition per analysis, so it has **no identity**.
`CfdInitialConditionId` is not defined, because an id would distinguish nothing.

### 4. Every setting is canonical, and there is no algorithm selector

The definition holds the iteration settings (two under-relaxation factors), the
convergence settings (two residual tolerances and a divergence factor), the
linear-solve tolerance, a steady iteration budget **or** transient settings
(step, end time, per-step budget, sampling), and the output settings (the residual
history). They are canonical for the reason ADR-047 gave: a value a user chooses
must be undoable, redoable and persistable. There is no field for the coupling
algorithm, the time scheme, the convection scheme or the linear solver. Each has
one qualified value or none yet, and a selector would be a field with one legal
value. **Convergence is not acceptance.** These settings decide when the loop
stops. Whether its result is accepted is validation's decision (P19-VALID-001),
with thresholds that are policy, not settings.

The **output settings do not select fields**. A result always carries p, U and
phi, because a result without phi could not have its mass balance checked. The
engineering integrals are requests that name CAD faces, and P19-FORCES-001 adds
them.

### 5. A semantic fingerprint records the content; the revision still decides currency

`fingerprint(definition)` is FNV-1a over a canonical text of every field
`operator==` compares (shortest exact decimals, -0.0 written as 0, enumerators by
name). Equal definitions give equal fingerprints in every preset. The object's
name, id, revision, any address and any clock are excluded.

The result's source records **both** the analysis revision and this fingerprint.
Currency compares the whole source, so a result is current only when both match.
That is the revision-keyed regime P18 uses for its result, and an edit typed back
leaves the result stale with the single reason `analysis`. So does a **rename**:
core's `Document::rename` moves the object's revision, so a renamed analysis's
result is stale for `analysis` although its fingerprint is unchanged. P18's
analyses behave the same way, and the test that pins this is
`CfdData_ARenameStalesTheResultByRevisionOnly`. This ADR does **not**
take P18-QUAL-001's open owner decision D1 (content keys versus revision keys).
The fingerprint makes content keying possible, and the test that pins the current
regime (`CfdData_AnEditTypedBackKeepsTheResultStale`) fails first if it is
adopted without that decision.

### 6. The result holds provenance and metadata now, its fields with P19-DOF-001

A finite-volume solution is p and U per cell and phi per face, keyed by
`CellIndex` and `FaceIndex`, types P19-TOPO-001 and P19-DOF-001 define from a
face enumeration that does not exist yet. Two alternatives were rejected:

- A container with p and U but no phi would claim to be a solution without being
  one.
- Keying p by a tetrahedron's position would make P19-DOF-001's numbering decision
  here, by accident.

So `CfdResult` holds only its source and its metadata: outer iterations, the
final residuals, ADR-058's two mass-balance measures, and the time and step of a
transient sample. P19-DOF-001 adds the arrays and extends the size mirror that
pins the container.

The **latest failed attempt** is structured: a stage (input refused, mesh
refused, ill posed, linear solve failed, diverged, iteration limit, non-finite
field, continuity not closed, mass balance failed), the error, the source when
the inputs resolved, and the iterations reached. Recording one keeps the held
result untouched. A failure is never a result: the types do not convert, and
the slot accepts a result only through `recordSuccess`.

**The state outranks the result's currency.** While a failure is held, the
analysis is `SolveFailed`, even when the surviving result's source still equals
the current one. In that case `resultCurrency` says `Current`, which is a statement
about that result's inputs, not about the analysis. The failure's source is
diagnostic and is not compared. The state lasts until a success or a reset. This
is P18's rule, kept (review finding 10).

## Consequences

- P19-BC-001 resolves faces, checks cardinality and orientation, resolves the
  reference point, and refuses a missing datum through `pressureDatumProblem`.
- P19-FLUID-001 fills `CfdFluidProperties.viscosity` from P15. The definition
  does not change.
- P19-DOF-001 extends `CfdResult` with the fields.
- P19-PERSIST-001 replaces today's named refusal ("objects of type 'cfd-analysis'
  cannot be saved") with a file representation of the definition.
- Two boundary conditions on the same face are not refused by the definition: it
  cannot know whether two `FaceName`s overlap. P19-BC-001 refuses a facet that two
  conditions claim.
- P19-BC-001 applies the gravity reference height to prescribed boundary
  pressures (§2).
- P19-TRANSIENT-001 must bound the number of time steps. The data level accepts
  any finite `0 < dt <= end`, as P18's does, and P18 caps the step count in its
  transient solver.
- A boundary condition's face must name a known `FaceRole` and a valid feature.
  Core's selector check enforces neither, and an out-of-range role would let two
  different conditions write the same canonical text (review finding 5). The CFD
  validator refuses both.

## Verification

`tests/cfd/CfdDataTests.cpp` (`[cfd][data]`) and the `compile_fail.cfdids` group;
evidence in `docs/verification/P19-DATA-001/`.
