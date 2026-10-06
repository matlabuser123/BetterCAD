# P16-QUAL-001 — backend default audit

```text
SUBJECT:  every Netgen parameter that could affect what a BetterCAD document
          MEANS, and whether its value is BetterCAD's or an accident
RULE:     each must be a canonical BetterCAD control, an explicit fixed adapter
          value, or proven irrelevant to current semantics
SOURCE:   src/meshing/netgen/NetgenBackend.cpp, read in the committed tree
          docs/verification/P16-SIZE-001/AUDIT.md for the pinned-source reading
```

## The rule, and why it is not satisfied by "we set maxh"

`Ng_Meshing_Parameters::Transfer_Parameters()` is the only route from that
struct into the mesher, and it copies **exactly fifteen fields**. Any field it
copies and the adapter does not set carries whatever Netgen's constructor
happened to put there — so a future Netgen release could change what an
unchanged BetterCAD document produces. That is the failure mode this audit
exists to close, and it is closed by setting all fifteen.

Seven further fields the header declares are **not transferred at all**, so
setting them would be theatre. They are named here so a later reader does not
"fix" their absence.

## The fifteen transferred fields

```text
| PARAMETER         | VALUE   | BETTERCAD MEANING                | USER-FACING | WHY THIS VALUE |
| maxh              | from    | upper bound on element size      | YES, via    | the only field driven by
|                   | request | everywhere                       | global      | canonical intent; the
|                   |         |                                  | target size | mesher always resolves a
|                   |         |                                  |             | bound, from the request or
|                   |         |                                  |             | from defaultGlobalTargetSize,
|                   |         |                                  |             | so Netgen's own default
|                   |         |                                  |             | never decides it
| uselocalh         | 1       | GATES the local size function    | no          | at 0, every local sizing
|                   |         |                                  |             | control silently does
|                   |         |                                  |             | nothing (meshing3.cpp reads
|                   |         |                                  |             | it). The one pinned value
|                   |         |                                  |             | this milestone actively
|                   |         |                                  |             | depends on
| minh              | 0.0     | lower clamp on element size      | no          | RestrictLocalH SILENTLY
|                   |         |                                  |             | clamps any request below
|                   |         |                                  |             | minh; 0 disables the clamp
|                   |         |                                  |             | so a local control means
|                   |         |                                  |             | what it says
| grading           | 0.8     | how fast size grows away from a  | no          | RestrictLocalH hardcodes
|                   |         | refinement                       |             | 0.8 when it has to create
|                   |         |                                  |             | the size tree, and
|                   |         |                                  |             | CalcLocalH only creates one
|                   |         |                                  |             | when ABSENT -- so without
|                   |         |                                  |             | pinning, the grading
|                   |         |                                  |             | depended on whether a local
|                   |         |                                  |             | control existed, and merely
|                   |         |                                  |             | adding one changed the whole
|                   |         |                                  |             | mesh
| elementsperedge   | 2.0     | surface-side discretisation      | no          | BetterCAD supplies an
| elementspercurve  | 2.0     | surface-side discretisation      | no          | already-triangulated
|                   |         |                                  |             | boundary, so these cannot
|                   |         |                                  |             | act; pinned at Netgen
|                   |         |                                  |             | 6.2.2604's values so they
|                   |         |                                  |             | stay inert by decision
| optsteps_2d       | 3       | surface optimisation passes      | no          | same: the surface is fixed
|                   |         |                                  |             | before the backend sees it
| optsteps_3d       | 3       | VOLUME optimisation passes       | no          | this one DOES affect the
|                   |         |                                  |             | result. Pinned, so element
|                   |         |                                  |             | shape is not at the mercy
|                   |         |                                  |             | of a dependency default
| check_overlap     | 1       | backend's own refusal of a bad   | no          | left ON deliberately: it is
| check_overlapping_| 1       | input                            | no          | the backend refusing
|   boundary        |         |                                  |             | something BetterCAD would
|                   |         |                                  |             | otherwise have to detect
|                   |         |                                  |             | later
| meshsize_filename | nullptr | external size-field file         | no          | BetterCAD's sizing is
|                   |         |                                  |             | canonical; a file would be a
|                   |         |                                  |             | second, invisible authority
| second_order      | 0       | emit Tet10 instead of Tet4       | no          | Tet4 is P16's only element
|                   |         |                                  |             | (ADR-031). Pinned OFF rather
|                   |         |                                  |             | than defaulted: a Tet10
|                   |         |                                  |             | would arrive as ten nodes
|                   |         |                                  |             | the translator does not
|                   |         |                                  |             | expect
| quad_dominated    | 0       | quad-dominated surface meshing   | no          | not a volume concern, and
|                   |         |                                  |             | not P16's element family
| invert_tets       | 0       | flip tetrahedron orientation     | no          | the boundary is already
| invert_trigs      | 0       | flip triangle orientation        | no          | closed, manifold and
|                   |         |                                  |             | coherently oriented by
|                   |         |                                  |             | P16-SURF-001, so NOTHING
|                   |         |                                  |             | here may flip it. These two
|                   |         |                                  |             | are the orientation audit's
|                   |         |                                  |             | anchor
```

**15 of 15 explicit.** One is driven by canonical BetterCAD intent (`maxh`), and
fourteen are fixed adapter values with a stated reason.

### The count, checked from both ends

`P16-SIZE-001` recorded this list correctly and then said "fourteen", and this
audit repeated the word before counting the rows. The figure is **fifteen**,
and it is now derived two ways that must agree rather than asserted once:

```text
Ng_Meshing_Parameters fields, from the installed nglib.h        22
  the seven nglib never transfers                              - 7
                                                               ----
  therefore transferred                                          15

parameters.<field> = assignments in NetgenBackend.cpp            15
  names identical to the 15 above, no extras, none missing
```

So the property the gate requires — every field that reaches the mesher has a
value BetterCAD chose — is **true, and was always true**: the adapter sets all
fifteen. What was wrong was the arithmetic in the prose, in both milestones.
Corrected rather than carried, and recorded in `ADVERSARIAL_REVIEW.md` as a
finding against this audit's own method.

## The seven inert fields

Declared by the header, never copied by `Transfer_Parameters`, so they cannot
affect anything on this pathway:

```text
fineness            closeedgeenable      closeedgefact
minedgelenenable    minedgelen           optsurfmeshenable
optvolmeshenable
```

`fineness` and `optvolmeshenable` read as though they matter, which is exactly
why they are named: a reader who set them would believe they had changed the
mesh. P16-SIZE-001 established this by reading the pinned Netgen source, and
this audit re-confirms the adapter does not set them.

## What this closes

```text
"Can backend defaults change engineering meaning?"
```

No. Every parameter that reaches the mesher has a value BetterCAD chose, and the
three that would silently defeat a user's instruction if left alone —
`uselocalh`, `minh` and `grading` — are the three with the longest reasons in
the source. A Netgen upgrade can change its own defaults freely without
changing what a BetterCAD document means; what it cannot do silently is change
`Transfer_Parameters`' field list, and `INFRA-NETGEN-001` pins the backend
version that was qualified.
