# P17-CMD-001 — what the history may hold

```text
THE CLAIM: the undo history carries canonical engineering intent and never a
generated solver array.

PROVED TWO WAYS, and the second is the stronger one:
  1. runtime tests show that no solver array WAS stored
  2. compile-fail cases show that none CAN be
```

## The authority table

| Entity / data | In canonical history? | How that is enforced |
| --- | --- | --- |
| `AnalysisId` | **YES** | every command is keyed on one |
| `LoadId` | **YES** | the load's identity (ADR-032) |
| `RestraintId` | **YES** | the restraint's identity |
| `FaceName` / geometry reference | **YES** | the canonical target |
| canonical load definition | **YES** | the whole `StructuralLoad` record |
| canonical restraint definition | **YES** | `RestraintId` + `FaceName` + mask |
| `SolverSettings` | **YES** | canonical since ADR-047 |
| `MeshControlId` | **YES** | names the body transitively |
| | | |
| `NodeId` as a command **target** | **NO** | compile-fail |
| `ElementId` | **NO** | compile-fail |
| `DofIndex` | **NO** | compile-fail |
| `FreeEquationIndex` | **NO** | not constructible into any payload |
| boundary facet handle / index | **NO** | not a member of the definition |
| `ConstraintSet` | **NO** | compile-fail: no such member |
| prepared nodal force field | **NO** | compile-fail: no such member |
| `GlobalStructuralSystem`, `K`, `F` | **NO** | compile-fail, and **not reachable** |
| `StructuralSolution`, `u` | **NO** | compile-fail, and **not reachable** |
| strain, stress, von Mises, principal | **NO** | compile-fail, and **not reachable** |
| reaction arrays | **NO** | compile-fail, and **not reachable** |
| viewer state: deformation scale, contour, camera | **NO** | no such member, and the renderer is a higher layer |

## The one declared exception, and why it is intent rather than a leak

A `NodalForceLoad` names a `NodeId`. The brief lists that as an automatic
failure; **the qualified codebase disagrees, and it wins** under this
repository's conflict order (verification evidence → TODO → architecture).

P17-LOAD-001's header is explicit:

> MESH-LOCAL BY DECLARATION, and that is a decision rather than an oversight. A
> `NodeId` is a handle into one generation of one mesh (ADR-031), so this load
> cannot track a CAD edit and must not pretend to: it carries the `MeshStamp`
> it was created against and is refused against any other mesh, including one
> with the same node count and the same numeric handles. What it is for is
> fixtures.

So a command that adds one carries that `NodeId` **as part of a canonical
record that declares its own mesh binding and refuses a foreign mesh.**
Forbidding it would leave a qualified canonical load form with no undoable
editing path, which is a worse answer than carrying it.

What is forbidden instead, and probed: a `NodeId` or `ElementId` as a command's
**target**, a `NodeId` as a **restraint** target (a restraint has no mesh-local
form at all), and every species of derived state.

The three face-based load forms — traction, pressure, gravity — carry no mesh
identity whatever.

## The header authority, measured rather than asserted

A translation unit that includes **only** `StructuralCommands.hpp` was
preprocessed and searched for each forbidden declaration.

```text
NOT REACHABLE from the command header
  GlobalStructuralSystem      SolvedSystem
  StructuralSolution          StructuralPostResult
  StructuralReactionResult    PreparedLoads
  MeshView                    Eigen (any)

REACHABLE, AND PRE-EXISTING -- not introduced by this milestone
  DofIndex  NodalDof  MeshDofMap  ConstraintSet  FreeEquationMap
```

The DOF family arrives through `StructuralRestraint.hpp` → `StructuralDof.hpp`,
which is how the canonical restraint type has always been declared. Excluding
it would mean restructuring a qualified header, which is outside this
milestone.

**So include isolation is partial, and the compile-fail cases carry the DOF
family rather than the include graph.** That distinction is recorded here
rather than collapsed into a claim that the header "cannot see" solver-local
types, because the stronger claim would be false.

The part that *is* strong is worth stating plainly: **the assembled system and
every result array are unreachable.** A command cannot be handed a `K`, a `u`
or a stress field, because those types are not visible where commands are
declared.

## Where the size claim comes from

A command's payload is a pair of `StructuralAnalysisDefinition`s: a
`MeshControlId`, a mode, two short vectors of canonical records and a
`SolverSettings`. So history memory scales with **the number of loads and
restraints**, never with the mesh or the result.

That is a structural claim, not a benchmark: the definition has no array whose
length depends on a node or element count, and the compile-fail cases are what
keep it that way. A `std::vector<Stress6>` added to the definition would break
`StructuralDataTests`' mirror assertion at compile time before any measurement
could notice.
