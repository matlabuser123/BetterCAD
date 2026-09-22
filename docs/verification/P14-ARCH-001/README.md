# P14-ARCH-001 — Drawing Architecture and Contracts

```text
TASK:            P14-ARCH-001
SCOPE:           decide how technical drawings fit the existing architecture,
                 and record the decisions and what they rejected
IMPLEMENTATION:  none, deliberately — this milestone designs
OUTPUT:          ADR-010 … ADR-017, a supersession note on ADR-006, and the
                 Drawings section plus three new invariants in ARCHITECTURE.md
BASELINE:        27f83e3, clean, HEAD == origin/main
RESULT:          PASS
EVIDENCE:        this directory, and the ADRs it cites
```

## Scope, and what would have failed it

This milestone produces decisions, not code. **No executable source or test
file was changed**, and that is verified below.

The checklist asks for architecture contract tests "where contracts can be
pinned without implementing full drawings", and also says not to "pretend
`P14-SHEET`/`P14-VIEW` already exist". Those two pull against each other here,
and the second wins, for the reason `P13-ARCH-001` recorded when it faced the
same choice:

> "writing a stub, a placeholder type or an unused header to 'start' the
> implementation would be a failure of the milestone rather than progress in
> it."

Every test the checklist suggests needs types that do not exist: a `DrawingId`
to check distinctness, a scale type to check transform maths, a `drawing`
module in the layer table to check dependencies. Introducing them here would
put unused declarations in a public header and an unused module in the build,
covered by tests that only exercise the declarations themselves. So none was
introduced, and what could be verified without them **was** verified, against
the qualified APIs that already exist. That is the "Convention verification"
section below: 26 checks, all passing, on the two conventions most likely to
be decided wrongly by accident.

What this milestone would have failed on: adding `src/drawing/`, adding
`SheetId` to `Id.hpp`, or renumbering the layer table. The renumber in
particular is decided here (ADR-015) and **applied by the milestone that
creates `src/drawing/`**, exactly as ADR-006 was decided by `P13-ARCH-001` and
applied by `P13-COMP-001`.

## Method

The lifecycle's UNDERSTAND phase first, then ARCHITECT. The existing
architecture was traced before any design was written — identity, the document
object model, references, the dependency graph, regeneration, configurations,
commands, persistence, export, the layering rules and the CLI — and each
decision cites the code it rests on.

## Repository audit

**No drawing code exists.** Searched `include/`, `src/`, `apps/`, `tests/`,
`examples/` for `drawing`, `sheet`, `view`, `annotation`, `dimension` (as a
drawing concept), `projection`, `HLR`, `PDF`, `SVG`, `DXF`. The only code hit
in the whole tree is `apps/bettercad/MainWindow.cpp:28` —
`QLabel(tr("Viewport — not implemented yet"))`. Everything else is roadmap
prose.

Specifically absent, and each one is new work:

```text
projection, silhouette, hidden-line removal, section cutting
any 2D curve or edge container that reaches io
any vector output path (STL is mesh; STEP is B-Rep)
Vector2D, Direction2D, Frame2D, RigidTransform2D
a stable, persistent EDGE name
a Ratio or Scale type, or a dimensionless Quantity alias
a registry or factory for DocumentObject kinds
```

### What already exists and is reusable

This is the part that changed the design rather than confirming it.

| Existing, qualified | What it gives a drawing |
| --- | --- |
| `Frame3D::toLocal(Point3D) -> Point2D` (`core/math/Frame.hpp:50`) — "local coordinates of the point's orthogonal projection onto the plane" | **Orthographic projection itself.** No new primitive needed |
| `Frame3D::xy()`, `xz()`, `yz()` | The three principal views, under the convention ADR-013 fixes |
| `Point2D`, `BoundingBox2D` (`core/math/Point.hpp:10`, `BoundingBox.hpp:10`) | The start of a 2D vocabulary |
| `FaceName` + `FaceRole` + copy chain (`features/Faces.hpp`) | The only stable geometry reference in the codebase |
| `ObjectReference`, `ReferenceResolver`, `ResolvedReference` (P13-REF-001) | Reference identity, resolution states, recovery |
| `DocumentObject` + the `{id, type, name, data}` envelope | Persistence with no format version bump |
| `Command` + one `CommandHistory`; the `assembly/Commands.hpp` pattern | Undo, with no `core` change |
| `Configuration` + `activeSuppressionFor` (ADR-007) | One configuration system a drawing reads |
| `libTKHLR`, `HLRBRep_Algo`, `HLRBRep_PolyAlgo`, `HLRToShape` in the dependency prefix | **Hidden-line removal is a kernel call**, not a subsystem to write |
| `geometry::StepAssembly` → `Result<std::string>` → `io` writes atomically | The template for a neutral IR and a format writer |

### Four findings that changed the design

**1. There is no stable edge reference, and a drawing wants one.**
`geometry::EdgeSignature` (`core/geometry/Edges.hpp:55-65`) names an edge by
the curve it lies on. Its own header says "BetterCAD has **no persistent
topological naming yet**", and the failure envelope is explicit: "if the
supporting curve itself moves or changes size (e.g. the box gets taller,
moving its top edges), **the reference matches no edge**", plus an ambiguity
case when a cut splits an edge in two.

A box getting taller is the ordinary parametric edit a drawing exists to
track. ADR-004 already refuses `FaceSignature` as a mate target on exactly
this reasoning — "a mate may reference anything that moves with the model, and
nothing that merely sits where the model used to be" — and an `EdgeSignature`
fails the same test. So ADR-012 restricts drawing references to ADR-004's
vocabulary, and accepts a real capability gap rather than a silent wrong
answer.

**2. The final-pass mechanism cannot carry a drawing's result.** ADR-008 added
final passes for document-level derived state, which looks like the obvious
home for projected geometry. The signature is not generic
(`features/Regenerator.hpp:67-68`):

```cpp
using FinalPass = std::function<Result<std::map<ComponentId, RigidTransform3D>>(
    Document&, const Regenerator&, RegenerationReport&)>;
```

and `Regenerator` has exactly one store for it. Projected curves have nowhere
to go. Worse, passes run in **name order** from a `std::map`, so a drawing
pass follows `"assembly.solve"` because `a` < `d` — a real dependency resting
on alphabetical coincidence, declared nowhere and tested nowhere.

That is why ADR-014 keeps drawing geometry out of the regenerator entirely.

**3. The layer table has no room, for the second time.** Drawing must sit
above `assembly` (3) and below `io` (4), and `CheckLayering.cmake:90-91`
requires *strictly* lower. The renumber in ADR-015 is forced, exactly as
ADR-006's was — and ADR-006's own consequences predicted it: "Any later module
that consumes assemblies (drawings, BOM, simulation) sits above 3."

**4. A drawing subsystem is not one module.** The kernel-facing projection and
HLR work must live behind an `occt/` adapter, and every existing one is under
`src/core/geometry/occt/` — layer 0, *below* `assembly`. The thing that
decides *what* to project is above it. ADR-015 splits the subsystem across
`core/geometry` (0), `drawing` (4) and `io` (5).

## Decisions

Each was chosen against two or three serious candidates; the ADRs carry the
reasoning and what was rejected.

| ADR | Decision | Chosen over |
| --- | --- | --- |
| [ADR-010](../../architecture/decisions/ADR-010-drawings-live-in-the-document.md) | Drawing objects are `DocumentObject`s in the model's own `Document` | a separate `DrawingDocument`; a separate top-level type |
| [ADR-011](../../architecture/decisions/ADR-011-drawing-intent-is-canonical-projection-is-derived.md) | Drawing intent is persisted; every projected curve, edge classification and dimension **value** is derived | caching the projection in the file; a sidecar cache |
| [ADR-012](../../architecture/decisions/ADR-012-drawing-references-name-semantic-geometry-only.md) | A drawing references datums, `FaceName`s, objects and occurrences — never an `EdgeSignature` | also allowing `EdgeSignature`; building semantic topology now |
| [ADR-013](../../architecture/decisions/ADR-013-view-orientation-and-drawing-scale.md) | A view is a `Frame3D` whose normal faces the viewer; scale is an exact `paper:model` pair | normal as the viewing direction; direction + up-vector; a `double` scale; an enum of standard scales |
| [ADR-014](../../architecture/decisions/ADR-014-drawing-geometry-is-built-on-demand.md) | Drawing **objects** get regeneration handlers; drawing **geometry** is built on demand, never published by the regenerator | widening the final-pass contract; a drawing final pass via an out-parameter |
| [ADR-015](../../architecture/decisions/ADR-015-drawing-module-and-layer.md) | `drawing` at layer 4; `io` → 5; `renderer`/`scripting` → 6; projection in `core/geometry` | drawings inside `assembly`; drawing above `io` |
| [ADR-016](../../architecture/decisions/ADR-016-the-drawing-scene-is-the-export-boundary.md) | One neutral, self-validating drawing scene; writers transcribe and compute nothing | each writer deriving its own geometry; a scene per format |
| [ADR-017](../../architecture/decisions/ADR-017-drawing-identity-model.md) | `Sheet`, `View`, `Dimension`, `Annotation` are `DocumentObject`s | dimensions as sub-objects of a view or sheet |

### The three that carry the most weight

**ADR-012 is the one the phase turns on.** The failure it prevents is a
dimension that still resolves, to the wrong geometry — a drawing that looks
right, prints, and sends a wrong number to a machine shop. The decision is to
allow only what ADR-004 already permits, which means accepting that *some
dimensions an engineer will want cannot be expressed in `P14`* — a dimension
to a fillet's tangent edge has no spelling until semantic topology (`P21`).
That gap is recorded rather than closed by a reference type that breaks on the
most ordinary parametric edit.

**ADR-014 removes a problem rather than accommodating it.** Because drawing
geometry is built on demand from an already-regenerated document, a drawing
never races the assembly solve, nothing is cached so nothing can be stale, and
no change to qualified `features` infrastructure is needed. The cost —
re-projection on every query, and HLR is expensive — is stated plainly and
owned by `P14-REGEN-001`, which is given three constraints in advance if it
adds a cache: it lives in `drawing`, it compares **resolved inputs** rather
than revisions (because a configuration override of a free parameter changes
no revision — the case `P13-REGEN-001` measured), and it publishes all or
nothing.

**ADR-011 makes the file format a non-event again**, the way ADR-002 did.
Drawing objects go into the existing `objects` array through the existing
envelope, so there is no new top-level key and no version bump, and a document
with no drawing stays byte-identical to one written before `P14`.

## Convention verification

The two conventions most likely to be fixed wrongly by the first code that
needs them were checked arithmetically against the qualified semantics of
`Frame3D`, before any of it was written into an ADR.

```text
26/26 checks passed
```

| What was checked | Result |
| --- | --- |
| The three principal frames are right-handed, `Y = normal × X` | 6/6 |
| Under "normal faces the viewer", the frames are Front, Right, Top, and their reverses are Rear, Left, Bottom | 6/6 |
| A 100 × 60 × 40 box projects to 100 × 40 in Front, with the depth collapsing 8 corners to 4 | 3/3 |
| Top gives 100 × 60 and Right gives 60 × 40 | 4/4 |
| `1:2` halves, `2:1` doubles, `1:1` is identity; a 100 mm feature at 1:2 measures 50 mm | 5/5 |
| The full chain places a Front view at 1:2 inside an A3 sheet at the given placement | 3/3 |
| A dimension's text is the model value (100), not the drawn length (50) | 2/2 |

The second row is the substantive finding: **the standard views are not a new
convention, they are the frames the codebase already has.** Choosing the
opposite convention would invert all six relative to `Frame3D`.

It also disarms a trap this project has already paid for. That `Frame3D::xz()`
has normal **−Y** is what made a positive `Distance` mate move a component to
negative *y* in `P13-REFMOD-001`, putting every located component on the wrong
side of its deck while every test still passed. Here the same fact is what
makes a Front view come out right.

Script and log: `verification/view-convention.py`, `view-convention.log`.

## Identity model

`Sheet`, `View`, `Dimension` and `Annotation` are `DocumentObject`s with IDs
that widen to `ObjectId` (ADR-017). Tables and balloons are **not** allocated
types here; whether they need their own identity is `P14-BOM-001`'s to decide
on evidence.

The decisive argument is ADR-002's, restated: graph participants must be
document objects, or `dependencies()` cannot express them. A dimension depends
on the geometry it measures. As a sub-object it could not say so, and the
owner would become the unit of dirtiness — so moving a dimension's text would
re-run hidden-line removal on its view. That is the wrong cost for the most
common interactive edit in a drawing.

Never used as persistent identity: array position, render order, memory
address, kernel handle, display name, or a position in a feature's own
reference list.

## Reference contract

```text
allowed            PlaneReference (datum plane, principal plane, named face's plane)
                   AxisReference  (datum axis, principal axis)
                   FaceName       (feature + role + copy chain)
                   ObjectReference / ObjectId
                   ComponentId    (an occurrence, not just a part)

refused            geometry::EdgeSignature
                   geometry::FaceSignature
                   any topology index or enumeration order
                   a position in a feature's reference list
```

States are inherited from `P13-REF-001` unchanged: resolved, unresolved, and
the document-mismatch case ADR-010 makes unreachable for now. Recovery is
inherited too — when the intended target returns under its own identity the
reference resolves again, because the reference itself never changed.

The invariant, in the form the tests must take:

```text
intended geometry disappears        →  unresolved, naming what it wanted
a similar face appears in its place →  still unresolved, NOT adopted
the intended target returns         →  resolves to the same geometry
```

## Coordinate systems, units and scale

```text
model space        the document's 3D coordinates, SI internally
view space         oriented by the view's Frame3D
projection plane   2D, model-sized, from Frame3D::toLocal
sheet space        2D, millimetres from the sheet corner
paper              physical millimetres, exported or printed
```

```text
model point → toLocal → × (paper/model) → + view placement → sheet mm
```

Units: lengths and angles are the existing `Length` and `Angle`; sheet
coordinates are millimetres from a sheet corner regardless of the model's
units; imperial sheets are expressible because `units::inch` exists. Scale is
an exact rational `paper : model` pair, stored as written, so `1:2` is a
reduction and `1:3` survives a round trip.

## Regeneration contract

```text
model change  →  drawing object handlers validate references
              →  (nothing derived is published by the regenerator)
              →  the next build of a view projects from the current model
```

Drawing objects get ordinary `RegenerationHandler`s — resolve the references,
fail loudly if one does not resolve, return `std::nullopt` because they own no
geometry. This is not optional: an object with no handler is silently marked
`UpToDate` and never validated (`features/Regenerator.cpp:341-344`), the exact
defect `P13-REGEN-001` fixed for mates.

What makes a drawing rebuild: any change to the model objects a view shows,
any change to the geometry a dimension measures, a parameter that drives
either, a configuration switch, a suppression change, and any edit to the
drawing objects themselves. Because nothing is cached, "makes it rebuild" and
"makes it dirty" are the same statement.

Failure is atomic and inherited: a view whose source is broken publishes no
geometry rather than its previous geometry.

## Module layering

```text
core 0, sketch 1, features 2, assembly 3, drawing 4, io 5, renderer/scripting 6
```

Decided here, **applied by the milestone that creates `src/drawing/`**.

```text
core/geometry (0)  project a Body onto a Frame3D; classify visible, hidden,
                   silhouette and smooth edges; section cuts. Behind occt/.
drawing       (4)  sheets, views, dimensions, annotations, tolerances, BOM.
                   Decides what to project. Owns the drawing scene.
io            (5)  serialization of drawing objects; PDF, SVG and DXF
                   writers; atomic file writing.
```

Answers to the questions the checklist asks: drawing **may** depend on
assembly; assembly **must not** depend on drawing; drawing calls `geometry`
directly, as `assembly` does; projection and HLR live in `core/geometry`
behind the OCCT adapter; export adapters live in `io`.

## Export architecture

```text
drawing intent → regenerated + solved model → drawing scene → writer → bytes
   canonical            derived                  derived      io      caller writes
```

The scene is neutrally named, lives in `drawing`, validates itself once, and
is consumed by three writers that compute nothing. Writers return
`Result<std::string>` — the file contents — so that file handling stays in one
place, as `Exchange.hpp:14-16` requires.

Nothing in the scene is serialized. It is built on demand and thrown away.

## Contract tests

**None were added, and the reason is the first section of this document.**
Every test the checklist proposes requires a type that does not exist yet:

| Proposed | Why it could not be pinned here |
| --- | --- |
| strong IDs remain distinct | needs `DrawingId`/`SheetId`/`ViewId` in `Id.hpp` — an unused placeholder |
| drawing object ownership rules | needs the object kinds |
| scale transform math | needs a scale type |
| coordinate conversion math | needs a view type; the *convention* was verified instead, against `Frame3D` |
| module dependency checks | needs `drawing` in the layer table, which ADR-015 defers to the applying milestone |
| derived-state non-persistence | needs something derived to not persist |
| reference document mismatch | **already tested** by `P13-REF-001`; nothing new to pin |
| serialization version behaviour | ADR-010 concludes there is no version change, so there is nothing to pin |

What was verified instead is recorded under "Convention verification": 26
checks against the qualified `Frame3D` semantics, which needed no new type.

Each ADR's Verification section states what the implementing milestone must
prove, so the contracts are pinned by tests that exist when the thing they
describe exists.

## Adversarial architecture review

Nineteen questions were put to the design before any implementation milestone
starts. Fourteen are answered by the decisions; five produced findings.

| Question | Answer |
| --- | --- |
| A second document system? | No — ADR-010 declines it explicitly, and says what would make it right |
| Can a drawing survive an unresolved model reference? | Yes — intent persists, the view fails explicitly, recovery is inherited from `P13-STREF-001` |
| Can a dimension silently jump to another edge? | **No, structurally** — ADR-012 admits no reference type that can move |
| Is generated 2D geometry accidentally canonical? | No — and under ADR-014 it is never stored at all |
| Can save/load serialize stale projected geometry? | No — nothing projected is serializable |
| Can sheet scale be read both ways? | No — ADR-013, verified 26/26 |
| Can an assembly drawing reference an occurrence? | Yes — `ComponentId` is in the allowed set, so two instances of one part are distinguishable |
| Does configuration switching invalidate correctly? | Yes — the build reads what is in force; the free-parameter-override case is named in ADR-014's cache constraints |
| Can suppressed components leak into views or BOM? | No — ADR-016 puts the check in the scene, once, for all three formats |
| Can exporters compute their own geometry and disagree? | No — ADR-016 forbids it |
| Does undo/redo have a clear ownership boundary? | Yes — per object, existing commands, one history |
| Can Python/AI use the same public drawing API? | Yes — `drawing` is an ordinary library module |
| Does this require filesystem-path identity? | No |
| Does it violate a `P13` ADR? | No. It extends ADR-004 (ADR-012), mirrors ADR-005 (ADR-011), follows ADR-002 (ADR-010, ADR-017), and **supersedes ADR-006's table** — recorded in both ADRs |

### Findings

**F-1 — `Point2D` cannot distinguish model units from sheet millimetres.**
ADR-013 names five coordinate spaces, but projection-plane coordinates and
sheet coordinates are both `Point2D`, so a value from one can be passed where
the other is expected and nothing will complain. This is the same class of
error the units system exists to prevent everywhere else in the codebase, and
"units are part of correctness" is an architectural invariant. *Not blocking
`P14-SHEET-001`*; it lands in `P14-VIEW-001`. Recommendation recorded there:
give sheet space its own point type, or carry `Length` components, rather than
relying on discipline.

**F-2 — one active configuration means one configuration per sheet.**
Configurations are document-global (ADR-007), and ADR-010 keeps the drawing in
the same document. So every view on every sheet shows the *same* active
configuration: an "as assembled" view and a "bare frame" view cannot sit side
by side on one sheet. This is a direct consequence of a qualified decision, it
is a capability a drawing user will expect, and it is recorded rather than
designed around. Lifting it needs per-view configuration selection, which is
the same shape as the per-component configuration selection `P13` already
records as absent.

**F-3 — "inactive" must not be reported as "unresolved".** A view's intent may
name a component that is suppressed in the active configuration. That is not a
broken reference; `P13-CONF-001` established that "inactive is not unresolved".
`P14-ASM-001` must keep the two distinct, or a configuration switch will fill
a drawing with false errors.

**F-4 — nothing structurally prevents a model→drawing dependency.** The
dependency graph is untyped `ObjectId`s, so a cycle back from a feature into a
drawing is expressible in principle. In practice no reference type can name a
drawing object, which is what actually prevents it, and the existing cycle
detector would catch one anyway. Recorded because the protection is a property
of the reference types, not of the graph, and would be lost if a future
reference type became more general.

**F-5 — the GUI invariant remains vacuous.** "The GUI owns no CAD state" holds
today only because the GUI is a placeholder, and `CheckLayering.cmake`'s
`module_of()` returns nothing for `apps/`, so no rule applies there. Nothing in
this design puts drawing state in the GUI, but nothing prevents a future one
from doing so. Carried forward from `P13-QUAL-001`, unchanged.

No finding blocks the next milestone. F-1 and F-3 are constraints on
`P14-VIEW-001` and `P14-ASM-001`; F-2 and F-5 are limitations; F-4 is an
observation.

## Known limitations this architecture accepts

Consequences of the decisions, not oversights.

- **A drawing lives in the same `.bcad` file as its model.** It cannot be
  opened without the model — which is also why it can never be stale relative
  to one. A separate `DrawingDocument` needs cross-document dependency
  *execution*, which `P13` scoped out after measuring it.
- **A dimension to an edge has no spelling** unless that edge is the
  intersection of two nameable faces. Fillet tangent edges, chamfer cut edges
  and split edges are not referenceable until semantic topology (`P21`).
- **One configuration per document means one per sheet** (F-2).
- **Opening a drawing costs a regeneration, a solve and a projection**, every
  time, because nothing derived is cached. `P14-REGEN-001` owns the
  measurement and ADR-014 constrains the remedy.
- **Drawing objects cannot be suppressed by configuration.** The override
  machinery enumerates exactly the kinds it knows in seven places; adding a
  fourth is not free and `P14` does not do it.
- **`bettercad::drawing::Dimension` shares a name with `bettercad::Dimension`**,
  the units dimensional-analysis type. The namespace resolves it; no drawing
  header may write `using namespace bettercad;`.
- **A drawing sub-graph has no cascade.** Deleting a sheet leaves its views
  unresolved rather than deleting them — the same behaviour `P13-CMD-001`
  recorded for a component and its mates, and the same decision not to add
  cascading deletion.
- **`AxisReference` has no `validate()` or `referencedObjects()` overload**,
  unlike `PlaneReference`; they are open-coded in `MateReference.cpp:108-112`.
  A drawing that references axes should promote them rather than open-code a
  second copy.

## Verification of "no code changed"

```text
git status --porcelain -- src include apps tests examples cmake \
    CMakeLists.txt CMakePresets.json
    (empty)
```

The eight source and test tree IDs are unchanged from the baseline commit
`27f83e3`:

```text
apps              8da209723f46d3e0194750fcaad4ee2981af723c
include           da4a989538fdfc7dbd8641540993d31dc30be5e3
src               6fc8350a9c8ab6acfe760f509e6b35ab58eb7068
tests             d12ca6408e4876b2c75b3d8bc518fb72099f5b40
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

These are the values `P13-QUAL-001` qualified. This milestone writes ADRs,
documentation and this directory, and nothing that can reach the executable or
the tests — so `P13`'s qualification is untouched and no regression run is
required or claimed.

## Result

```text
TASK:            P14-ARCH-001 — Drawing architecture and contracts
IMPLEMENTATION:  none — no executable source or test file changed
OUTPUT:          ADR-010 … ADR-017; a supersession note on ADR-006;
                 ARCHITECTURE.md and docs/architecture.md updated
VALIDATION:      26/26 convention checks against the qualified Frame3D
                 semantics (verification/view-convention.log)
ADVERSARIAL:     19 questions, 5 findings, 0 blocking
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-ARCH-001 → [x]
NEXT:            P14-SHEET-001 — drawing documents / sheets / formats
```

## Revision

The layer renumber ADR-015 decides is **not applied here**. `ARCHITECTURE.md`
records it as decided and names the milestone that applies it, and
`P14-SHEET-001`'s checklist carries applying it — and updating both
architecture documents — as explicit items.

That last point is deliberate. `P13-QUAL-001` found that ADR-006's identical
renumber was decided by `P13-ARCH-001`, applied by `P13-COMP-001`, and that
both architecture documents went on claiming the old table for fifteen
milestones because nothing made the update anyone's job. Putting it in the
applying milestone's checklist is the fix.

## Files

```text
verification/view-convention.py   the convention check
verification/view-convention.log  26/26
```
