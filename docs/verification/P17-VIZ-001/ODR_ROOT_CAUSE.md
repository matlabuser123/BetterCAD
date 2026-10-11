# P17-VIZ-001 — a type-name collision that cost hours, and what found it

Recorded in full because the defect was mine, the build could not diagnose it,
and every cheap measurement said the code was fine.

## What happened

`StructuralInspection.hpp` introduced two structs into
`namespace bettercad::renderer`:

```text
NodeInspection
ElementInspection
```

**P16-VIZ-001 already owned both names**, in
`include/bettercad/renderer/MeshScene.hpp`, with entirely different layouts:

```text
P16-VIZ   struct NodeInspection {
              meshing::NodeId node;
              Point3D position;
              bool onBoundary;
              std::optional<meshing::FacetSource> source;
          };

P17-VIZ   struct NodeInspection {        <-- the collision
              meshing::NodeId node;
              Point3D undeformedPosition;
              Translation3D displacement;
              Length magnitude;
              bool restrained;
              structural::RestraintComponents restrainedComponents;
          };
```

One name, one namespace, two definitions: an ODR violation. **Neither header
includes the other, so no translation unit ever sees both definitions and
nothing warns.** The renderer library compiled against one and the test binary
linked both.

## The symptoms

```text
inside inspectNode()      out.displacement.x.si() == 0.0          correct
in the caller             inspection->displacement.x bits =
                          0x66b2dff300                            a STACK ADDRESS
                          inspection->node                        correct
```

and, from the hand-filled probe with no mesh and no solve involved:

```text
returned bare       node=42 ux=1.5 uy=2.5 uz=3.5 mag=4.5 px=0.1   all correct
returned via Result node=42 ux=0   uy=0   uz=3.5 mag=4.5 px=0.1   32..47 lost
```

ctest reported both failures as SEGFAULT.

## Why every measurement exonerated the code

This is the part worth keeping. **Each probe runs inside one translation unit,
and each TU was internally self-consistent**, so a per-TU measurement is
structurally incapable of detecting a per-TU disagreement. All of these agreed
and none of them meant anything:

```text
sizeof(RecoveredFields)                          136 == 136
sizeof(NodalDisplacement)                         40 == 40
sizeof(NodeInspection) / alignof                  72 / 8 == 72 / 8
offsetof(NodeInspection, displacement)            32 == 32
offsetof(NodeInspection, magnitude)               56 == 56
sizeof(Result<NodeInspection>)                    80 == 80
std::is_trivially_copyable_v<NodeInspection>      true in both
the full compile flag sets, diffed                byte-identical, 28 each
```

And these ruled out the obvious culprits:

```text
deleting the objects, the library and the exe, then rebuilding   no change
searching the tree and the build root for a duplicate header     only one exists
a plain 72-byte trivially-copyable struct through Result<>       WORKED
a standalone exe using the real headers and the real libraries   WORKED
```

The last two were the real clues and I read them too slowly. `NineDoubles`
worked because its **name was unique**; the standalone exe worked because it
included only the new header and **never `MeshScene.hpp`**.

## Two misleading signals

```text
Catch2 reported `REQUIRE(a == b)` failing with expansion `0.0 == 0.0`
    NOT impossible, and not a Catch2 bug. `Detail::fpToString` formats with
    std::fixed, so a denormal (2.2e-312) prints as 0.0. Several cycles were
    spent treating this as paradoxical. A memcpy of the raw bits settled it
    in one run and should have been the first move.

has_value() was true at one call site and the library asserted
_M_has_value was false at another
    Consistent with the same cause: std::expected<T,E> instantiated at two
    different sizeof(T) puts its discriminant at two different offsets.
    _GLIBCXX_ASSERTIONS is enabled in this build and caught it, which is the
    one diagnostic that pointed somewhere real.
```

## What found it

One command, after the ABI theories were exhausted:

```bash
grep -rn "struct NodeInspection\|class NodeInspection" include/ src/
```

Two hits. That was the whole diagnosis.

## The fix, and why it went this way

```text
NodeInspection     -> NodeResultInspection
ElementInspection  -> ElementResultInspection
```

**The newcomer yields.** P16-VIZ-001's names are qualified, shipped and
referenced by `MeshScene`'s own API; renaming them would have reached into
qualified code to accommodate a milestone that had not yet passed a gate.

`ReactionInspection`, `ScalarField`, `ChannelDescriptor`, `ExtremeEntity`,
`MetricObservation`, `LoadGlyph`, `NodalLoadArrow`, `RestraintGlyph` and
`ReactionArrow` were each checked against the tree before being named, and are
unique.

## A sweep for others

Every module namespace was swept for a type name defined twice:

```text
renderer meshing structural core features io drawing assembly sketch
-> no duplicate definitions
```

The only apparent hits were `std::hash` and `std::formatter` specialisations,
which are distinct template specialisations and legal, and repeated forward
declarations of `Document` and `Regenerator`, which are also legal.

## What this suggests, and is not doing here

A check in `tests/architecture/` that refuses a type name defined twice within
one namespace would catch this class of defect mechanically, the way
`CheckLayering.cmake` enforces containment and OCCT confinement. **It is not
being added in this milestone**: it is shared test infrastructure affecting
every module, which is its own scope decision and would owe its own
requalification. Recorded as a recommendation.
