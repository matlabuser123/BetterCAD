# ADR-033 — The volume mesher is a backend behind an enforced boundary

```text
STATUS:    Accepted
DATE:      2026-09-30
MILESTONE: P16-ARCH-001
TOUCHES:   tests/architecture/CheckLayering.cmake (a new containment rule and a
           new module layer), ADR-006 and ADR-015 (inserting a module),
           ARCHITECTURE.md's OCCT-behind-an-adapter rule
EVIDENCE:  docs/verification/P16-ARCH-001/BACKEND_MATRIX.md
```

## Context

The audit settled the question this ADR exists to answer:

```text
BetterCAD contains no engineering mesher and no volume mesher.
OCCT 8.0.1 ships no volume mesher -- its whole Poly_* family is surface,
and the 14 headers matching "tet" are DateTime, Trihedron and Tangence.
```

So a volume mesher must come from outside, and BetterCAD has never taken a dependency for a
core engineering capability before. Two facts make the choice unlike every previous dependency
decision in this project.

**BetterCAD has no licence.** `LICENSE`: *"No license has been chosen for BetterCAD yet. Until
one is added to this file, no permission is granted to use, copy, modify or distribute this
software."* Everything shipped today is weak copyleft, dynamically linked — OCCT
LGPL-2.1-with-exception, Qt LGPL-3. A GPL or AGPL mesher linked in would not merely add a
dependency: it would **decide BetterCAD's own unchosen licence**, or forbid distributing the
result. That is the project owner's decision, not an engineering one.

**The rule that is supposed to contain a backend does not work.** `CheckLayering.cmake` catches
an OCCT header by its file extension —

```cmake
if(header MATCHES "\\.hxx$" AND NOT file MATCHES "${occt_allowed_regex}")
```

— and every candidate backend's entry header is a `.h`: `nglib.h`, `tetgen.h`, `gmsh.h`,
`CGAL/...`, `mmg/...`. Rule 2 matches only Qt's shape, rule 3 only `bettercad/...` headers. So a
backend header could be included from `src/features/`, from a public header, or from the GUI, and
the architecture test would pass. P16's invariant *"backend-specific behaviour must remain behind
a meshing interface"* is, as the tree stands, **unenforceable**.

## Constraints

- OCCT is confined to `src/(.+/)?occt/` and Qt to the GUI and renderer, both enforced. A third
  dependency with a third rule must follow the same pattern, not a new one.
- The layering table refuses an unregistered module outright: `"unknown module '<x>' (add it to
  the layer table)"`. Creating `src/meshing/` without a table entry fails the build.
- The rule is strictly-lower, and two modules may share a layer — `renderer` and `scripting` are
  both 6.
- Meshing needs `core` (0) for `Body`, `Document` and `FaceName`, and `features` (2) for
  regeneration and `checkFaceName`. `io` (5) must serialize its controls.
- ADR-032 requires the backend to return a mesh whose boundary facets can be attributed to CAD
  faces. A backend that returns a bare tetrahedralization cannot satisfy it.
- Determinism must be measured, not assumed, and a backend is where non-determinism enters.

## Options

### Which backend

Set out in full, with primary-source licences, in `BACKEND_MATRIX.md`. In summary: OCCT cannot;
CGAL's `Mesh_3` is GPL **and** re-discretises the boundary so facets need not lie on the CAD
face, which makes ADR-032's mapping unsound; MMG is LGPL-3 but its own licence calls it software
for tetrahedral mesh *modification* — it adapts an existing mesh rather than generating one;
Gmsh is GPL-2+; TetGen is AGPL-3. Netgen is LGPL-2.1 with an OCC front end. Writing our own means
robust boundary recovery with Steiner points, a research-grade problem whose realistic outcome
here is a mesher that works on the reference models and fails on real parts.

### Where the boundary sits

**1. No interface — call the backend where it is needed.** Rejected by P16's invariants and by
every reason OCCT sits behind an adapter.

**2. An interface, contained by convention and review.** This is what the tree has today for
mesh backends, and the audit showed it amounts to nothing: no rule fires.

**3. An interface whose containment is enforced by the architecture test**, like OCCT's and Qt's.

## Decision

**A backend-neutral interface, option 3, with containment enforced now rather than asserted.**

**No backend type appears in any BetterCAD API.** Not in a public header, not in a `MeshControl`,
not in a `Mesh`, not in a diagnostic, not in a persisted file. A backend's own error text may be
*quoted inside* a structured BetterCAD diagnostic; its types, enums and handles may not cross the
interface. The `Mesh` a backend produces is BetterCAD's `Mesh`, translated at the boundary.

**Backend code lives in `src/meshing/<backend>/`, and `CheckLayering.cmake` gains a rule that
enforces it** — the same shape as the OCCT and Qt rules, matching the candidate backends' entry
headers. The rule is inert today, because no such include exists anywhere in the tree, and fires
the moment one appears outside an adapter directory. It is added now, in this milestone, because
an invariant that only a reviewer enforces is one the next milestone breaks, and because P16-VOL
is where the temptation arrives.

**`meshing` is layer 4, sharing the layer with `drawing`, and nothing is renumbered.** It needs
`core` (0) and `features` (2), so it must sit above 2. Placing it at 4 rather than 3 costs
nothing today and leaves `assembly` (3) reachable, so meshing an assembly occurrence later does
not force the renumbering that ADR-006 and ADR-015 each had to do. `drawing` and `meshing` are
siblings — both derive a secondary representation from the same geometry, and neither needs the
other — so sharing a layer states a real relationship rather than papering over one. `io` (5)
still serializes meshing controls and `renderer` (6) can still display a mesh.

**Kernel triangulation stays where it is.** `core/geometry`'s `triangulate()` and its
`occt/OcctMesh.cpp` adapter are surface meshing of a `Body` and follow the precedent the layering
table states for projection and hidden-line removal: "kernel work … in core/geometry behind the
occt adapter". P16-SURF-001 extends it additively — per-face triangle grouping, so a higher layer
can attribute triangles to faces — and does not move it. What lives in `meshing` is the domain:
controls, the mesher service, regions, quality, validation and the volume backend.

**Admission rule for a backend**, derived from obligations the project has already accepted so
that it can be applied without pre-empting the licence decision:

```text
A volume-meshing backend may be admitted only if USING IT BY ANY MECHANISM imposes
no obligation BetterCAD does not already accept for OCCT and Qt: weak copyleft,
satisfiable by dynamic linking and by offering the backend's own source, with no
condition on BetterCAD's own source.

It must also return, or allow the derivation of, a boundary whose facets are
attributable to CAD faces (ADR-032), and it must be deterministic for a fixed
input and a fixed configuration.
```

"By any mechanism" is deliberate and closes a loophole this rule had when it said "linking".
Driving a GPL mesher as a **subprocess** is the standard way to argue around a link-based reading,
and it is rejected here on two independent grounds. The licence question is at best unsettled and
is not BetterCAD's to gamble on while its own licence is unchosen. And the engineering is worse
in every respect that this project cares about: geometry and results marshalled through temporary
files, a mesh that depends on an executable's version found on a `PATH` rather than on a pinned
dependency, failures that arrive as an exit code and text on stderr instead of a structured
diagnostic, and determinism that cannot be pinned because the tool is outside the build. A
subprocess backend is judged by exactly this rule, and fails it.

Applied: **Netgen is the only candidate that meets it.** Gmsh, CGAL and TetGen fail on licence;
CGAL and MMG fail on engineering grounds independent of licence, so relaxing the licence rule
would not readmit them.

**Introducing the dependency is not this milestone's to do.** `P16-ARCH-001` defines the
boundary, the rule and the outcome of applying it — which is what P16's gate asks for ("backend
boundary explicit"). Adding a third-party library to BetterCAD, and with it a constraint on the
project's licence, is the owner's decision. `P16-VOL-001` therefore carries an entry condition:
the backend dependency is approved, or the fallback is chosen deliberately. `P16-DATA-001`,
`P16-GEOM-001` and `P16-SURF-001` do not depend on it and can proceed with OCCT and BetterCAD
alone.

## Consequences

- The invariant is a build failure instead of a sentence, and it is enforced before the first
  backend line is written rather than after.
- Adding `meshing` to the layer table costs no renumbering, and assembly meshing later will not
  either.
- A translation layer at the boundary costs a copy of the mesh from the backend's representation
  into BetterCAD's. That is accepted: it is the price of the backend being replaceable, and it is
  paid once per mesh generation, not per solve.
- The backend can be swapped, or removed, without touching the mesh data model, the controls, the
  mapping or P17.
- `P16-VOL-001` is gated on a decision outside this milestone. The gate is visible three
  milestones early, which is the point of auditing first.
- A cost of the containment rule: it names the candidate backends' headers, so it must be updated
  when a backend is actually admitted. That is recorded in the rule's own comment.

## Validation

```text
P16-ARCH-001     the containment rule is added and the architecture test passes
                 on the unchanged tree in Debug, Release and Debug-shared
P16-ARCH-001     the rule is shown to FIRE: a temporary backend include outside an
                 adapter directory fails the architecture check, and the tree is
                 restored and re-verified. A rule never shown to fail is not
                 known to work
P16-VOL-001      no backend type in any public header; no backend header outside
                 src/meshing/<backend>/
P16-VOL-001      determinism MEASURED: identical mesh from repeated runs, and
                 across Debug, Release and Debug-shared
P16-QUAL-001     the mesh data model compiles with the backend interface stubbed
```

## Invariants

```text
No backend type, enum or handle appears in any BetterCAD API, document or file.
A backend's message may be quoted inside a structured diagnostic; its types may
not cross the interface.

Backend code lives only in src/meshing/<backend>/, and the architecture test
enforces it rather than a reviewer.

meshing is layer 4. It may use core, sketch and features, and must not be used by
anything below io.

Surface triangulation of a Body stays kernel work in core/geometry behind the
occt adapter.

A backend is admitted only under the licence, attributability and determinism
rule above, and admitting one is a project decision recorded outside this ADR.
```
