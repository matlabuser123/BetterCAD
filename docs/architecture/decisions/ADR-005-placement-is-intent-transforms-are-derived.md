# ADR-005 — Placement is intent; the solved transform is derived

Status: Accepted
Date: 2026-09-19

## Context

A component sits somewhere. Two different things could be persisted: what the
user asked for, and where the solver put it.

The project already answers the general form of this question. Invariant 3:
"Generated B-Reps are derived state." Features "store inputs, not outputs"
(`ARCHITECTURE.md:146`), and a native file that stored geometry rather than
intent is listed as an anti-pattern (`ARCHITECTURE.md:485`). A solved
transform is to an assembly what a B-Rep is to a part: the output of
evaluating intent.

There is a close precedent for the intent side.
`features::CoordinateSystemDefinition`
(`include/bettercad/features/Datums.hpp:146`) stores `kind` (`Fixed` or
`Offset`), an optional base, and translation and rotation triples where each
component is **either a literal or a parameter**:

```cpp
std::array<Length, 3> translation{};
std::array<std::optional<ParameterId>, 3> translationParameters{};
std::array<Angle, 3>  rotation{};
std::array<std::optional<ParameterId>, 3> rotationParameters{};
```

It resolves recursively through `drivingValue<Q>` with a depth limit of 64
(`src/features/datum/DatumResolution.cpp:109`), composing
`RigidTransform3D::rotation` turns and then offsetting the origin. That is
exactly the shape a fixed component placement needs, and it is already
parameter-driven, already configuration-aware and already tested.

There is also a warning in the precedent for the *derived* side. The sketch
solver warm-starts from the geometry the sketch currently holds, so its
converged point depends on the path taken. `P12-PARAM-002` measured the
consequence at **1.3e-15** relative over a configuration cycle — bounded and
non-accumulating, but real, and it is why `TODO.md` carries a known
limitation about configuration switching. An assembly solver that seeded
itself from a persisted previous solution would import the same
path-dependence, and would do it through the *file*, so the answer could then
depend on save and load history.

## Constraints

- Invariant 2: engineering intent is persistent. Invariant 3: generated
  results are derived.
- Regeneration must be deterministic: the same document must give the same
  model, and `P12-QUAL-001` measured the twelve reference models as
  bit-identical across three presets.
- A failed evaluation commits nothing (invariant 10).

## Options

1. **Persist the solved transform** as the component's position, and treat
   mates as a tool that edits it.
2. **Persist placement intent only; derive the transform every
   regeneration**, from intent and mates, with no seed.
3. **Persist intent, and also persist the last solved transform as a solver
   seed**, the way the sketch solver warm-starts from current geometry.

## Decision

Option 2. A component persists a **placement intent** —
`Fixed`/`Offset`-style, literal or parameter-driven, modelled on
`CoordinateSystemDefinition` — plus whether it is grounded. The **solved
`RigidTransform3D` is derived state**, recomputed by regeneration and held
beside bodies, never written to `.bcad`.

## Rationale

Option 1 is the anti-pattern in assembly clothing. It makes the file store
the answer instead of the question: mates become a historical note about how
a number was once produced, the transform cannot follow a parameter change,
and reopening a file gives a position that no longer corresponds to its
constraints. It is the same mistake as storing a B-Rep instead of a feature
tree.

Option 3 is more tempting, and is what the sketch solver actually does — so
it deserves a real answer rather than dismissal. It is rejected for two
reasons. First, it puts derived state in the persistent file, which
invariant 3 forbids, and once there it is load-bearing: the solution would
depend on save history, so two documents with identical intent could solve
differently. Second, the project has *measured* what warm-starting costs, at
1.3e-15 per cycle, and has had to document it as a limitation. Choosing to
repeat a known wart in a subsystem being designed from scratch, when nothing
yet requires it, would be choosing it deliberately.

The honest cost of option 2 is that the solver must converge from placement
intent alone, which is a stronger requirement: intent must be a good enough
starting point, and the solver must be robust from it. That is a real
constraint on `P13-SOLVE-001`, and it is better as a stated requirement than
as a hidden dependency on file history. If it later proves genuinely
impossible, the way to revisit this is a superseding ADR with the
measurement attached — not a quietly added field.

Reusing `CoordinateSystemDefinition`'s shape for the intent gives
parameter-driven placement, configuration awareness through
`effectiveParameterValue`, and a resolution path that is already qualified,
for free.

## Consequences

- `.bcad` contains no transforms for components — only intent. A file cannot
  encode a position its constraints do not produce.
- Regeneration must be deterministic from intent alone, so the solver needs a
  deterministic starting configuration and a deterministic iteration order.
  The dependency graph already breaks ties by ascending ID
  (`src/core/document/DependencyGraph.cpp:138`); the assembly solve must be
  equally order-free.
- A grounded component is the datum the rest solve against. At least one
  component must be grounded, or the assembly is free to translate and rotate
  as a whole — an under-constrained state the solver must *report*, not
  silently pin.
- Opening a large assembly costs a solve. No cached positions means no fast
  path; if that ever matters it is a measured performance decision, not a
  reason to persist derived state.
- Solved transforms live where bodies live — beside the regenerator's
  results, keyed by `ComponentId` — and are dropped when a component's
  regeneration fails, exactly as a body is.

## Verification

When `P13-XFORM-001` and `P13-SOLVE-001` implement this: a saved and reloaded
assembly solves to the same transforms as one built in memory; no transform
appears in the `.bcad` JSON; a parameter change moves a component and
restoring the parameter restores the transform to the tolerance the reference
models already hold; solving twice gives identical transforms; and an
assembly with no grounded component reports an under-constrained state rather
than choosing one.
