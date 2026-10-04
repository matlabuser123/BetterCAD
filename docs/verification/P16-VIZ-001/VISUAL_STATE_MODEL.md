# P16-VIZ-001 — the visual state model

```text
SUBJECT:  what "current", "stale", "missing" and "generation failed" mean, where
          each is decided, and how each is made visible
DATE:     2026-10-04
```

## The requirement, and why it is the hard part

```text
A stale mesh must be visually obvious.
A current mesh must not look stale.
```

Both halves matter and the second is the one that gets dropped. A UI that
marks everything suspect is not informative; a UI that marks nothing is
dangerous. And the failure mode worth designing against is specific: a user
looks at a mesh, believes it describes the model in front of them, and it does
not.

## Four states, and nothing in between

```text
NoMesh             nothing has been generated
Current            built from the geometry the document has NOW
Stale              built from geometry that has since changed; still
                   inspectable, and does not describe the model
GenerationFailed   the most recent attempt failed. An earlier mesh may still
                   be held, and if so it is stale by definition
```

`GenerationFailed` is a state of its own rather than a flag on the others, and
that is the whole point of having four. Three would force the ambiguous case —
an old mesh surviving a failed regeneration — to be reported as either
`Current` (false) or merely `Stale` (true but incomplete: it hides that the
attempt to replace it failed).

`MeshStatus` carries the state, whether anything is still inspectable, and the
core's own diagnostic:

```cpp
struct MeshStatus {
    MeshVisualState state;
    bool inspectable;              // a mesh is held and can be examined
    std::optional<Error> failure;  // the core's diagnostic, verbatim
};
```

## Where the decision is made

**In the core, from the document's own geometry revision.** Not from a GUI
timestamp, not from a dirty flag the GUI maintains, and not from anything the
GUI can forget to update:

```cpp
const GeometryRevision current = meshing::geometryRevision(document, mesh->source());
status.state = (current == mesh->revision()) ? Current : Stale;
```

`meshing::geometryRevision` is `P16-GEOM-001`'s, and it exists precisely for
this: *"mesh invalidation needs to ask 'has the geometry moved?' without first
asking 'may I mesh it?' — a mesh of a body that has since become ineligible is
still stale, and saying so should not require the body to be meshable again."*

So the GUI asks a question and renders the answer. It has no opinion about
currency, which is why it cannot disagree with the engine about it.

## One indicator, not several

The panel shows the state in exactly one place:

```text
No engineering mesh
Mesh: current
Mesh: STALE — does not correspond to the current geometry
Mesh: GENERATION FAILED — showing the previous, stale mesh
Mesh: GENERATION FAILED
```

Scattering currency across several widgets is how a UI ends up contradicting
itself, so there is one label and everything else — counts, quality, selection
— is subordinate to it. The core's diagnostic appears beneath it as a notice
when there is one, so a failure says what failed rather than only that
something did.

## How it is made visible in the 3D view

A stale mesh is drawn in a desaturated, much darker facet colour with a warmer
edge colour. Chosen to survive a small viewport and a bad monitor rather than
to look tasteful: the alternative to a large difference is a user trusting a
mesh of geometry that has changed.

```text
current    facets 0.55 0.62 0.72   edges 0.12 0.14 0.18
stale      facets 0.33 0.30 0.26   edges 0.52 0.34 0.10
```

**The viewer does not decide staleness; it renders the decision.**
`Viewer::setMeshStale` is a setter, and the window calls it from the status it
read out of the core. A viewer that worked out currency for itself would be a
second opinion about it.

### And it is asserted, not eyeballed

`MeshDisplay_AStaleMeshDoesNotLookLikeACurrentOne` is a claim about pixels, so
it is checked as one: the same mesh, the same camera, drawn current and drawn
stale.

```text
the images must DIFFER          -- otherwise "visually obvious" is false
the covered area must NOT       -- staleness recolours; it does not change
                                   what is drawn, because a stale mesh is
                                   still the mesh that was generated and must
                                   still be inspectable
and clearing it must restore the earlier image EXACTLY
```

That last clause is what stops a regenerated mesh from going on looking stale.

## The sequence the milestone is judged on

Driven end to end by the GUI smoke test, with **no 3D view at all** — the
inspection layer is viewless, so this runs on a machine with no OpenGL:

```text
mesh: state before generating:          No engineering mesh
mesh: generated:                        9 nodes, 12 Tet4, 12 boundary facets
mesh: state when current:               Mesh: current
mesh: state after editing the geometry: Mesh: STALE — does not correspond ...
mesh: state after regenerating:         Mesh: current
mesh: selection after regenerating:     Nothing selected
```

Each line is a separate CTest case (`gui.mesh.*`), so a failure says which
capability broke rather than only that the GUI printed something unexpected.

The last line is Finding 2 of the [adversarial review](ADVERSARIAL_REVIEW.md):
a selection does not survive a remesh, because `ElementId` values are reused
across generations and element 13 of the new mesh is a different element.

## What is deliberately NOT inferred

```text
"THE MESH IS GOOD"
    is not a state. Quality is P16-QUALITY-001's report, with its own
    categories -- Valid, Warning, Failure, Invalid -- and a mesh can be
    Current and full of failures, or Stale and perfect. Mixing the two would
    let a threshold nobody chose decide whether a mesh exists.

"THE MESH IS SOLVER READY"
    is not a state either, and not this layer's to claim. ADR-030's
    ValidatedMesh is the token a solver takes and is constructed by the
    mesher's validating path.

STALENESS FROM A FILE TIME OR AN EDIT COUNTER
    never. The comparison is between two GeometryRevision values, both the
    document's.

A MESH OF A STALE BODY
    cannot be produced at all: P16-GEOM-001 refuses it, which is why
    generateMesh regenerates the model first (Finding 3). The GUI cannot
    create the state it would most like to misreport.
```

## Revision

First issue, 2026-10-04.
