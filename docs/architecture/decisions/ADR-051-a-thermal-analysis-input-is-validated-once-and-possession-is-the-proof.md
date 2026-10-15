# ADR-051 — A thermal analysis input is validated once, and possession is the proof

```text
STATUS:    Accepted
DATE:      2026-10-16
MILESTONE: P18-ARCH-001
TOUCHES:   ADR-036 (the structural boundary this one mirrors), ADR-042 (where
           the staged gates landed in P17, and will land in P18), ADR-030 (a
           mesh is derived state), ADR-028 (a solver holds no material data),
           ADR-050 (why the boundary is not shared with structural)
```

## Context

P16's recorded gap -- nothing FORCES a holder of a mesh to call `isStale`,
because `Mesher::mesh()` returns a stale mesh deliberately -- is a gap about
CALLERS, and every new consumer of a mesh reopens it. P17 closed it for a
structural solver with one device: a validated-input type with no public
constructor, built only by `requireStructuralModel` (ADR-036). P17-QUAL-001
then verified every clause of that decision against the code and found the
invariant intact through sixteen further milestones.

P18 is the second consumer. The question is not whether to use the device --
ADR-036's reasoning applies without change -- but whether to SHARE P17's
boundary or build the same one again.

## Constraints

- `thermal` (45) cannot include `structural` (50) -- ADR-050 -- so
  `requireStructuralModel` is not reachable from the thermal module.
- `StructuralModel` carries `LinearElasticConstants`, which a conduction
  problem does not need and must not require: a material with a conductivity
  and no Poisson ratio is a perfectly good conductor.
- P15 remains the authority on the material: `requireThermalConductivity`
  refuses a missing, non-finite or non-positive `k` and names the material,
  and `requireTransientConductionProperties` names EVERY missing transient
  input at once.
- Geometry eligibility is P16's one boundary, including the configuration
  refusal; P18 must inherit it rather than reimplement it.
- Nothing in this milestone may perform a solve.

## Options

### A — share P17's boundary by extracting a generic `MeshedInput` below 45

One type, two consumers. The material resolution differs by consumer, so the
shared part is the geometry and mesh checks only, and the material half is
added by each solver. A change to a qualified module to save one hundred lines,
and a new seam exactly where ADR-036 argued the checks must be unsplittable
("taken from one lookup").

### B — a thermal boundary of the same shape, in the thermal module

`requireThermalModel` and `ThermalModel`: the same seven checks in the same
order, delegating to the same P16 and P15 functions, resolving the
conductivity instead of the elastic constants.

### C — a currency flag on the thermal analysis

Rejected for the reason ADR-036 rejected it: a flag records what WAS true.

## Decision

**Option B.** `requireThermalModel` is the single boundary of the thermal
module, and `ThermalModel` is the proof.

```cpp
Result<ThermalModel> requireThermalModel(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);
```

`ThermalModel` has no public constructor, one friend, and is move-only.
Possession proves, at the moment of construction:

```text
the control exists
the body is eligible -- regenerated, current, a solid, non-empty, valid, and
  NOT behind a configuration override
a mesh is held for the control and its currency is Current
the mapping and the quality report came from that same mesh, in one lookup
the document's material resolves and yields a usable thermal conductivity
```

### What the material half proves, and what it leaves to P18-MAT-001

The boundary resolves `k` and nothing else, because `k` is what EVERY thermal
analysis needs and is what P15's `requireThermalConductivity` is documented to
hand over. Whether the material can carry a transient -- `rho` and `cp` as
well -- depends on the analysis MODE, which this milestone does not define;
`P18-MAT-001` asks it per mode through `requireTransientConductionProperties`,
exactly as P17-MAT-001 asked for density only in the gravity mode. A material
with only a conductivity is therefore ACCEPTED here, and the test that proves
it is the one that distinguishes this boundary from the next milestone's.

### Where the later gates land

ADR-036 forecast that quality, load, restraint and sufficiency gates would be
added to `requireStructuralModel`. They were not; ADR-042 put them in a STAGED
validation entry that runs after the boundary and stops at the first refusing
stage, and P17-QUAL-001 amended ADR-036 to say so. This ADR adopts that shape
from the start rather than forecasting the other one:

```text
this boundary proves    the control, the geometry, the mesh, the map, k
P18-VALID-001 judges    mesh quality, load and BC resolution, the temperature
                        datum, transient completeness, time-step validity --
                        staged, after this boundary, reached only through the
                        one solve entry
```

Possession of a `ThermalModel` proves exactly the seven `InputProblem` checks,
and nothing more.

### Delegation, not reimplementation

```text
geometry currency   meshing::geometryIneligibility   -- P16's reason, unchanged
mesh currency       Mesher::currency + describesTheModel
mesh validity       NOT re-derived; P16 refuses to HOLD an invalid mesh
material            features::requireEffectiveMaterial +
                    requireThermalConductivity -- resolution AND range
                    validation are P15's
```

Nothing here stores a conductivity, a default or a typical value. `k` is held
BY VALUE as a derived view of P15's canonical data.

## Rationale

Option B duplicates roughly one hundred lines of P17's boundary. That was
weighed and accepted, for three reasons that are each sufficient.

The boundary is the ONE place a solver's prerequisites are listed, and
ADR-036 argued that its checks must come from one lookup so that a mesh and a
mapping from different generations cannot be combined; Option A's seam would
split exactly that lookup. The material half differs in kind, not in detail,
so a shared type would carry either a union of material views or a template
parameter -- both worse than two plain types. And a change to P17's qualified
boundary to serve P18 is the kind of edit that ADR-035 was written to avoid:
a qualified module touched for a reason internal to another.

What would have made Option A right: a third consumer. If `P19` or `P20`
needs the same seven checks, the extraction will have three callers and the
seam will be worth its cost; this ADR is where to start.

## Consequences

**Easy.** A later P18 milestone adding a prerequisite writes one check in one
function; every consumer gets it. A thermo-mechanical milestone can take a
`ThermalModel` and a `StructuralModel` side by side, each proving its own
half.

**Hard.** Two boundaries that must stay in step. The thermal boundary tests
mirror the structural ones case for case, so a change to P16's contract that
breaks one breaks both visibly.

**Committed.** To `requireThermalModel` being the only constructor of a
thermal input, and to its check list only ever growing.

## Verification

`tests/thermal/ThermalAnalysisBoundaryTests.cpp`, eleven cases:

```text
a current mesh with a usable material        ACCEPTED, and possession carries
                                             the mesh, a complete map whose
                                             stamp matches it, the quality
                                             report and P15's k in SI
a material with ONLY a conductivity          ACCEPTED -- and its transient
                                             properties are shown absent, so
                                             the acceptance is the boundary's
                                             and not the fixture's
a mesh of geometry that has since changed    REFUSED -- the sharp case: the
                                             document is REGENERATED, so only
                                             the mesh is stale
a mesh whose sizing intent changed           REFUSED
after a failed generation                    REFUSED as the failure, not as
                                             staleness
an ineligible body with a mesh still held    REFUSED on geometry, proving the
                                             check order
nothing meshed / unknown control             REFUSED
no material / a material without k           REFUSED, with P15's diagnostic
                                             naming "thermal conductivity"
                                             and the material
every declared InputProblem                  reached by a case above
```

Evidence: [docs/verification/P18-ARCH-001/](../../verification/P18-ARCH-001/README.md).
