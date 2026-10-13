# P17-CMD-001 — the compile-fail guards

```text
16 cases, 16 expected failures, 0 unexpected passes.
Plus 1 CONTROL target that must COMPILE, which is what makes every failure
attributable to the selected line.

Group: structcmd    Source: tests/compile_fail/StructuralCommandMisuse.cpp
```

A runtime test can show that no stiffness matrix **was** stored. These show
that none **can** be, which is the stronger statement and the one that survives
the next person editing the commands.

## The harness, and why the control matters

P16 established the shape and this group reuses it unchanged. One source file
holds every case behind a `BETTERCAD_CF_*` macro. Compiled with **no** macro it
is the group's control target and is part of the normal build, so it must
compile cleanly. Each case then compiles the same file with one macro defined
and requires the build to fail **with a case-specific diagnostic**.

Because the control compiles, a failure cannot be the file being broken in
general — it is attributable to the one line the macro enabled.

## The cases

| Case | What it attempts | Expected |
| --- | --- | --- |
| `A_LOAD_COMMAND_IS_NOT_KEYED_ON_A_NODE` | `AddStructuralLoadCommand(NodeId, ...)` | no conversion |
| `A_LOAD_COMMAND_IS_NOT_KEYED_ON_AN_ELEMENT` | `AddStructuralLoadCommand(ElementId, ...)` | no conversion |
| `A_RESTRAINT_COMMAND_IS_NOT_KEYED_ON_A_NODE` | `AddStructuralRestraintCommand(NodeId, ...)` | no conversion |
| `A_COMMAND_IS_NOT_KEYED_ON_A_DOF_INDEX` | `SetSolverSettingsCommand(DofIndex, ...)` | no conversion |
| `A_RESTRAINT_DOES_NOT_TARGET_A_NODE` | `StructuralRestraint{id, NodeId, mask}` | no conversion |
| `A_RESTRAINT_MASK_IS_NOT_A_DOF_INDEX_LIST` | `StructuralRestraint{id, face, vector<DofIndex>}` | no conversion |
| `THE_DEFINITION_HAS_NO_DISPLACEMENTS` | `definition.displacements` | no such member |
| `THE_DEFINITION_HAS_NO_STRESSES` | `definition.stresses` | no such member |
| `THE_DEFINITION_HAS_NO_REACTIONS` | `definition.reactions` | no such member |
| `THE_DEFINITION_HAS_NO_CONSTRAINT_SET` | `definition.constraints` | no such member |
| `THE_DEFINITION_HAS_NO_PREPARED_LOADS` | `definition.nodalForces` | no such member |
| `THE_DEFINITION_HAS_NO_STIFFNESS_MATRIX` | `definition.stiffness` | no such member |
| `A_COMMAND_IS_NOT_CONSTRUCTIBLE_FROM_A_RESULT` | a command taking a `StructuralResult` | **see below** |
| `A_COMMAND_HAS_NO_RESULT_SETTER` | `edit.setResult(nullptr)` | no such member |
| `A_COMMAND_IS_NOT_COPYABLE` | copy-construct a command | deleted function |
| `THE_DEFINITION_DOES_NOT_TAKE_A_MESH` | `definition.mesh = MeshStamp{}` | no conversion |

## One case proved more than it was written to prove

`A_COMMAND_IS_NOT_CONSTRUCTIBLE_FROM_A_RESULT` expected a **conversion**
failure. The measured diagnostic was:

```text
error: 'StructuralResult' is not a member of 'bettercad::structural';
       did you mean 'StructuralRestraint'?
```

`StructuralResult.hpp` is **not reachable from the command header at all**. So a
command cannot be handed a generated result not because the conversion is
refused, but because the type a command would have to hold **is not visible
where commands are declared**. That is strictly stronger.

**The case failed until the claim was corrected**, which is the right way
round: the expected regex was the thing that was wrong, and the measurement
improved the statement rather than the statement being adjusted to fit. The
regex now accepts `is not a member of` as well as a conversion error, and both
the case and the registration record why.

## Why the probes go through constructor parameters

P16-CMD-001 recorded this trap and it applies here unchanged:

> The obvious probe -- `LocalMeshSizing{.face = someObjectId}` -- COMPILES:
> `FaceName` is an aggregate, so brace elision initializes its FIRST member and
> the ObjectId silently becomes `feature`, leaving a default EndCap selector.

So the id-misuse cases go through **command constructor parameters**, where a
parameter of a named type is copy-initialised and no elision applies. The
restraint case goes through `StructuralRestraint`'s constructor for the same
reason — it has one, so `face` is a parameter rather than an aggregate member.

## The exception that is deliberately not probed

A `NodalForceLoad` names a `NodeId`. It is **not** probed as a forbidden thing,
and the source says so at length: P17-LOAD-001 made that choice explicitly, the
payload carries the `MeshStamp` it belongs to, it is refused against any other
mesh, and it exists for fixtures. A command that adds one legitimately carries
that handle.

Writing a case that required it to be unconstructible would have been writing a
case against the qualified data model. What is probed is a `NodeId` or
`ElementId` as a command's **target**, and a `NodeId` as a **restraint**
target — a restraint has no mesh-local form at all.

## What this group does and does not establish

```text
ESTABLISHED  no command can be keyed on a mesh-local identity
             no result, system, constraint set or prepared load field can be
               a member of the definition a command copies
             a command cannot be copied, so the history cannot hold two owners
             the RESULT TYPES ARE NOT EVEN VISIBLE from a command TU

NOT ESTABLISHED, and recorded rather than implied
             include-level invisibility of DofIndex, MeshDofMap, ConstraintSet
             and FreeEquationMap. They arrive through
             StructuralRestraint.hpp -> StructuralDof.hpp, which is how the
             canonical restraint type has always been declared. Excluding them
             would mean restructuring a qualified header.
             These cases carry that weight instead: the types are reachable,
             and a command still cannot be built from one.
```
