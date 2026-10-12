# P17-VIZ-001 — a defect in committed, qualified code

```text
FOUND:     2026-10-12, while designing the glyph overlay (ADR-045)
IN:        src/renderer/StructuralInspection.cpp, inspectNode
COMMITTED: b04a9e1, and qualified by that run and by ec68076's
SEVERITY:  a UI drawing a support symbol from it drew the WRONG support
STATUS:    reproduced, root-caused, fixed, regression test added
```

## How it was found

Not by a test and not by a probe. The glyph work needs a per-node answer to
"which components are held here", so I went looking for the authoritative
source — and read `inspectNode`'s existing answer on the way.

Its comment and its code disagree.

```cpp
// WHICH components, accumulated over every restraint that names
// this node -- a union, which is what a constrained set is.
for (const structural::RestraintResolution& resolution : restraints->resolutions()) {
    out.restrainedComponents = out.restrainedComponents.unionWith(resolution.components);
}
```

**The comment says "every restraint that names this node". The loop iterates
every resolution unconditionally.** There is no check that the resolution names
the node, and there could not be: `RestraintResolution` carries a mask, a facet
count and a node *count* — never a node list.

## The reproducer

`StructuralViz_ANodesRestrainedComponentsAreItsOwn`.

Two restraints whose masks **differ** — a fixed start cap and a Uz-only side
face — then inspect a node that is on the side face and *not* on the cap. Its
only constraint is Uz.

```text
measured, before the fix:

  node 2 reports 3 constrained component(s)
      CHECK( restrainedComponents.count() == 1 )          FAILED
      CHECK_FALSE( ...holds(DofComponent::Ux) )           FAILED
      CHECK_FALSE( ...holds(DofComponent::Uy) )           FAILED
      CHECK_FALSE( ...isFixed() )                         FAILED

after the fix: 1 component, and it is Uz.
```

### Why every existing test missed it

**With one restraint, or with two of equal mask, the union is the right answer
by accident.** Every fixture in the suite had one restraint — a fixed support —
so the union over one resolution is that resolution, and the answer was
correct for the wrong reason.

The one case that *did* use two restraints with different masks was
`StructuralViz_RestraintSurvivesANonZeroDisplacement`, written earlier in this
same milestone to close a mutation gap. It asserted `restrained` and the
displacement magnitude — **not the component mask.** It walked past the defect.

That is worth stating plainly: a fixture rich enough to expose a defect does
not expose it unless something asserts the affected quantity.

## Root cause

`PreparedRestraints` holds the `ConstraintSet` but **not** the
degree-of-freedom numbering, and `RestraintResolution` holds no node list. So
the question is unanswerable from what `inspectNode` was given. The union was
an approximation standing in for data the function did not have.

## The fix

`inspectNode` takes the `MeshDofMap`, and the mask is read one DOF at a time
from the owners:

```cpp
const auto indices = numbering->indicesOf(node);          // P17-DOF's numbering
for (std::size_t which = 0; which < kDofsPerNode; ++which) {
    if (restraints->constraints().contains((*indices)[which])) {   // P17-BC's set
        out.restrainedComponents.add(kDofComponents[which]);
    }
}
```

Nothing is unioned and nothing is inferred.

```text
THE PARAMETER IS OPTIONAL, and null leaves the mask EMPTY -- the same
"unknown" that a null `restraints` already means for `restrained`. An absent
answer is honest; a plausible wrong one is not.

THE NUMBERING IS CHECKED against the mesh, so a map from another generation
is refused rather than used to index the wrong node.

AND THE TWO SOURCES ARE CROSS-CHECKED: a node that `nodes()` names must hold
at least one constrained DOF. They come from different objects, so
disagreement means they were built from different inputs -- refused with
ErrorCode::Internal rather than drawn.
```

## What it cost, and what it says

The affected box, **"Node displacement inspection"**, was ticked at `b04a9e1`
and stays ticked — the inspection was and is correct in everything except this
mask, and the mask is now exact. But it was ticked over a defect, and the
qualification that certified it did not catch it, because nothing asserted the
quantity.

```text
83 mutation probes did not find this. None of them mutated the union loop,
because I wrote the probe list from the code's STRUCTURE -- the refusals, the
guards, the arithmetic -- and a loop whose body is correct in isolation does
not look like a target. What was wrong was its EXTENT.

The adversarial review did not find it either. Its questions were about the
layer's own invariants, and this is a disagreement between a comment and the
data a neighbouring module exposes.
```

What did find it: needing the same answer for a second purpose, and reading
the existing one rather than calling it. **A comment that states a stronger
property than the code implements is a defect with a certificate attached** —
and the certificate is why nobody re-derived it.

## Verification

```text
reproducer added and failing before the fix        4 assertions
passing after                                      1 component, Uz
the complement asserted                            a cap node still reports
                                                   all three, so the fix is
                                                   not "report fewer"
unit.StructuralViz_                                48 / 48
unit.Structural + renderer + architecture         608 / 608
0 warnings
```

A three-preset requalification is owed and is run with the glyph work, since
both touch this file.
