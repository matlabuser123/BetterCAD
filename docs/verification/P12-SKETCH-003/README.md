# P12-SKETCH-003 — Sketches on Arbitrary Planar Faces: Blocker Record

## Status

**BLOCKED.** Nothing was implemented, no test was changed and no checkbox was
ticked.

A sketch on a model face must stay on that face when the model changes.
BetterCAD has no reference that can do that. The only persistent reference
to a face is its geometric signature (its plane and outward side), and that
matches nothing once a parameter moves the face. Keeping the sketch on its
face needs to know which feature made the face and in what role, and the
reference architecture does not record that. Recording it is the semantic
topology layer, which the P12 scope excludes (`TODO.md`, *Stable-reference
risk*; the P12 instructions, *Stable references*). The milestone therefore
stops here, as both require.

Date: 2026-09-17. `main` at `fe20b0f` (`P12-DATUM-001`, qualified). The
reproducer was built against that revision's qualified Release libraries.

## The Requirement That Fails

`TODO.md` lists `P12-SKETCH-003` as *sketches on arbitrary planar faces*,
after `P12-DATUM-001`, with this rule: if the milestone cannot be completed
safely within the current reference architecture, it stops, documents the
exact failing requirement and proposes a minimal prerequisite.

The rule is to use the current architecture only if its correctness can be
demonstrated. For a sketch on a face, correct means all three of:

1. The sketch is placed on the face it was put on.
2. After any change upstream (a parameter, an edited feature), it is on the
   **same** face, wherever that face now is.
3. When that face no longer exists or cannot be identified, the sketch fails
   with a diagnostic. No other face is substituted.

Requirement 2 is what distinguishes a sketch on a face from a sketch on a
datum plane, which `P12-DATUM-001` already provides. The current
architecture cannot meet it.

## What the Architecture Offers

`bettercad/core/geometry/Faces.hpp` documents it:

- A face is referred to by `FaceSignature`: the plane through a point, with an
  outward normal. The header says: "This is geometric matching, not
  persistent topological naming."
- "If the plane moves (e.g. the block gets taller and its top face rises),
  the reference matches no face."
- `FaceInfo`, the description of a body's face, holds surface kind, area,
  centroid and signature. Nothing names the feature that made the face, and
  nothing links it to the face of the previous regeneration.

Kernel identities (`TopoDS_Face`, enumeration order) change with every
regeneration and must not be persisted.

P11 accepted and pinned this limitation for holes, chamfers and fillets:
`HoleFeature_DoesNotSubstituteFaceAfterTopologyChange` and the shaft
reference model's chamfer (`tests/reference/ShaftTests.cpp`). A hole fails
with NotFound when the extrude under it gets taller. For a sketch on a face,
that same edit is the most basic parametric change there is.

## Minimal Reproducer

`face_reference_reproducer.cpp` (public API only) and its output
`face-reference-reproducer.log`:

```text
Base  100 x 60 rectangle, x 0..100, extruded by `height` (20 mm)
Step  50 x 60 rectangle, x 100..150, extruded 35 mm, joined to Base
```

| Step | Result |
| --- | --- |
| Base's top face as a reference | `plane through (0, 0, 20) mm facing (0, 0, 1)`, matches 1 face |
| `height` 20 → 40 mm, regenerate | matches **0 faces**: a sketch on it fails with NotFound, although Base's top face exists (now at z = 40) |
| The faces facing +Z at 40 mm | z = 35 (area 3000, Step's) and z = 40 (area 6000, Base's). Neither carries anything that identifies it. |
| A geometric guess: the parallel face nearest the old plane | z = 35, **Step's** top face: a silent substitution of the kind the P12 rules forbid |
| Base's own definition (end face at z = `height`) | z = 40, the right face |

Only the feature knows which face is its end face. The reproducer exits 0
when all four observations hold, and it did (`REPRODUCED: yes`).

## Why P12 Cannot Safely Solve It

- A signature reference stores a fixed plane. A sketch on it is a fixed datum
  plane with an existence check. It fails requirement 2 on the first edit
  that moves the face.
- Any rule that finds "the face it became" from geometry alone (nearest
  plane, same area, same centroid, largest face) guesses, and the reproducer
  shows such a guess choosing the wrong face. That breaks requirement 3 and
  the rule "Never silently select a different face/edge after regeneration."
- Persisting a kernel face, or its index, is forbidden, and would not survive
  a rebuild anyway.
- Keeping a face across rebuilds needs each feature to name the faces it
  makes (Base's *end cap*, *side from line entity 7*, …) and every later
  feature to carry those names through its booleans: a face split by a cut,
  merged by a join, trimmed by a fillet, copied by a pattern. That is the
  semantic topology layer described in `ARCHITECTURE.md` (source feature,
  generated entity class, semantic role). The P12 instructions list "full
  Semantic Topology" as not authorized, and `TODO.md` says P12 "does not
  expand into the semantic-topology phase".
- Limiting sketches to faces whose role is easy to derive (for example an
  extrude's end caps) would still need those role names to be persisted,
  resolved and carried through later features. It would also not be
  *arbitrary* planar faces. Shipping it as `P12-SKETCH-003` would claim more
  than it delivers.

## Minimal Prerequisite

A stable face-reference layer, authorized as its own milestone before
`P12-SKETCH-003`:

1. **Provenance at generation.** Each solid feature names the faces it
   creates by role. Examples: extrude start cap, end cap, and the side face
   from each profile entity; revolve caps and sides; the corresponding faces
   of sweeps, lofts, holes and blends. Pattern and mirror instances carry an
   instance index.
2. **Propagation through booleans and modifiers.** Using the kernel's
   modification history within one regeneration (generated, modified,
   deleted), every face of a result body keeps the names of the faces it came
   from. Nothing about the kernel identity is persisted.
3. **Reference = (feature ID, role, optional entity or instance ID).**
   Resolution finds the faces carrying that name in the target body. Zero
   faces: NotFound. Several faces (a split face): an ambiguity error, unless
   the user's selection is refined. Never a nearest-geometry fallback.
4. **Migration.** The existing `FaceSignature` and `EdgeSignature`
   references keep working as they are. Whether holes, chamfers and fillets
   move to the new references is a separate decision.

With that layer, `P12-SKETCH-003` is a small addition. A sketch attachment
becomes a face reference resolved to the face's plane, like the datum
attachments `P12-DATUM-001` added. The same layer is what `TODO.md` flags
for shell, draft and variable fillets.

## Proposed Next Action

A scope decision by the project owner, one of:

- **(a)** Authorize a minimal stable face-reference milestone (the
  prerequisite above) inside or ahead of P12. Then resume `P12-SKETCH-003`
  on it.
- **(b)** Redefine `P12-SKETCH-003` to accept signature references with the
  P11 limitation, failing with NotFound when the face moves, and record that
  limitation. The reproducer shows this adds little beyond datum planes.
- **(c)** Defer `P12-SKETCH-003` and let P12 continue with `P12-FEAT-001`.

Until then, P12 stops at this milestone, as the P12 instructions require:
"STOP at the first blocked milestone. Do not skip it and continue
downstream."

## Evidence Files

- `README.md`: this record.
- `face_reference_reproducer.cpp`: the minimal reproducer (public API; build
  line in its header).
- `face-reference-reproducer.log`: its output against the `fe20b0f` Release
  libraries.

## Result

```text
TASK:            P12-SKETCH-003 Sketches on arbitrary planar faces
IMPLEMENTATION:  none
TESTS:           none added or changed; reproducer run (REPRODUCED: yes)
VALIDATION:      a signature reference to a moved face matches 0 faces; the
                 nearest-geometry guess selects another feature's face; only
                 the feature definition identifies the face
RESULT:          BLOCKED — needs stable face references (semantic topology),
                 outside the authorized P12 scope
EVIDENCE:        docs/verification/P12-SKETCH-003/
TODO:            no checkbox changed; the milestone is marked blocked
```
