# P16-ARCH-001 — volume-meshing backend matrix

## Why this document decides a rule and not a vendor

BetterCAD has **no licence** (`LICENSE`: "No license has been chosen for BetterCAD yet …
no permission is granted to use, copy, modify or distribute this software"). Everything it
ships today is weak copyleft, dynamically linked: OCCT LGPL-2.1-with-exception, Qt LGPL-3.

So a backend choice is not a like-for-like engineering comparison. A GPL or AGPL mesher
linked into BetterCAD **determines BetterCAD's own future licence** — or, if the project
later wants a different one, has to be torn out along with everything built on it. That is
a decision for whoever owns the project.

This document therefore does two separable things:

1. establishes, from primary sources, what each candidate **is** — so the decision is made
   on facts and not on reputation; and
2. derives an **admission rule** that can be applied now, without pre-empting the licence
   choice, and that keeps the interface honest whichever backend is admitted.

## Licences, each read from the project's own text

Verified on 2026-09-30 by fetching the primary source, not from memory or a package index.

| Candidate | Version seen | Licence | Source read |
| --- | --- | --- | --- |
| **OCCT** (already a dependency) | 8.0.1 | LGPL-2.1 **with the Open CASCADE exception** | `Standard_Version.hxx`; `LICENSE` |
| **Netgen** | — | **LGPL-2.1** | repository `LICENSE` |
| **MMG** | — | **LGPL-3-or-later** | repository `LICENSE`: "free software … under the terms of the GNU Lesser General Public License … version 3 … or any later version" |
| **Gmsh** | 4.15.2 | **GPL-2-or-later** | gmsh.info |
| **CGAL** — `Mesh_3` / 3D Mesh Generation | 6.2.1 | **GPL** (or commercial) | CGAL Package Overview, per-package licence line: `License: GPL` |
| **TetGen** | 1.6 | **AGPL-3.0**, dual commercial | repository `LICENSE` |

CGAL is mixed by package — cgal.org: *"the Kernel and Support libraries are under the LGPL,
and most geometric algorithms and data structures are under the GPL"* — so the licence that
matters is the **meshing package's**, and that one is GPL. Taking "CGAL is LGPL" from the
front page would have been wrong.

## What each candidate actually does

Read against what P16 needs: **a CAD BRep solid in, a boundary-conforming linear
tetrahedral mesh out, deterministically, with each boundary facet traceable to the CAD face
it came from.**

| Candidate | Meshes a BRep? | Boundary-conforming? | Suits P16? |
| --- | --- | --- | --- |
| OCCT 8.0.1 | surface only | n/a | **No — it has no volume mesher.** Audit §4 |
| Netgen | **yes** — it has its own OCC geometry front end | yes | **Yes** |
| Gmsh | **yes** — OCC front end | yes | Yes, technically |
| CGAL `Mesh_3` | via an oracle, not a BRep; isotropic Delaunay refinement that **approximates** curved boundaries | **not exactly** — the boundary is re-discretised, so a mesh facet need not lie on the CAD face | **No** — the facet-to-`FaceName` mapping P16 requires is unsound if the boundary moves |
| MMG | **no** — its own licence calls it software "for the tetrahedral mesh **modification**" | n/a | **No for P16** — it adapts an existing tet mesh. A candidate for later adaptive refinement, not for the first mesh |
| Write our own | would have to | would have to | **No** — see below |

### Why "write our own" is rejected as the first implementation

It is the only option with no licence consequence at all, so it deserves a straight answer
rather than a dismissal.

Constrained Delaunay tetrahedralization of an arbitrary CAD solid is not a large version of
the 2D problem. Its hard part is **boundary recovery**: unlike 2D, a set of points and
constraint facets in 3D need not have *any* tetrahedralization that respects the facets
(Schönhardt's polyhedron is the standard counterexample), so Steiner points must be inserted
on and near the boundary, and doing that robustly with exact predicates is a research-grade
problem that the existing backends each represent years of work.

Attempting it here would violate two of this project's own rules at once — "Do not
overbuild. Build the smallest correct subsystem" and "When choosing between more features and
better correctness … choose the second" — because the realistic outcome is a mesher that
works on the reference models and fails on real parts, which is precisely the "single-case
hack" `CLAUDE.md` forbids counting as an implementation.

It stays on the record as the fallback that needs no permission from anyone.

## The admission rule

Derived from the status quo rather than from a preference, so that it can be applied without
making the licence decision:

```text
A volume-meshing backend may be admitted only if linking it imposes no obligation
BetterCAD does not already accept for OCCT and Qt: weak copyleft, satisfiable by
dynamic linking and by offering the backend's own source, with no condition on
BetterCAD's own source.
```

Applied to the table:

```text
Netgen    LGPL-2.1      ADMISSIBLE -- the same licence and the same class of
                        obligation as OCCT, which BetterCAD already links
MMG       LGPL-3+       admissible by licence, but it is not a mesh GENERATOR
Gmsh      GPL-2+        NOT ADMISSIBLE under this rule -- it would require
                        BetterCAD to be GPL-compatible, a licence BetterCAD
                        has not chosen
CGAL      GPL           NOT ADMISSIBLE under this rule, and unsuitable anyway
TetGen    AGPL-3        NOT ADMISSIBLE under this rule -- the strongest
                        condition of the set
```

**One candidate survives: Netgen.** That is a finding, not a preference — it is what remains
when the rule is the project's existing, already-accepted obligations.

The rule is deliberately reversible. If the owner chooses GPL for BetterCAD, Gmsh becomes
admissible and the rule relaxes; nothing in the architecture changes, because ADR-032 makes
the interface backend-neutral and forbids backend types from reaching a BetterCAD API.

## What this milestone does and does not settle

```text
SETTLED    an external volume-meshing backend is REQUIRED (nothing in BetterCAD
           or OCCT 8.0.1 can do it)
SETTLED    the interface is mandatory, backend-neutral, and ENFORCED by the
           architecture test rather than asserted (ADR-032)
SETTLED    the admission rule above, and that exactly one candidate meets it
SETTLED    CGAL and MMG are excluded on ENGINEERING grounds independent of
           licence, so relaxing the licence rule does not readmit them

NOT SETTLED, and deliberately   introducing the Netgen dependency itself.
           Adding a third-party library to BetterCAD is the owner's decision,
           and doing it silently inside an architecture milestone would be
           exactly the kind of scope widening CLAUDE.md forbids.
```

`P16-VOL-001` therefore carries an **entry condition**: the backend dependency is approved,
or the fallback is chosen deliberately. `P16-DATA-001` through `P16-SURF-001` do not depend
on it — the mesh data model, the geometry-preparation boundary and the engineering surface
mesh are all reachable with OCCT and BetterCAD alone. The blocker surfaces three milestones
before the milestone it blocks, which is the point of doing the audit first.
