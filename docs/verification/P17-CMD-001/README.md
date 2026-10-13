# P17-CMD-001 — Commands / Undo / Redo

```text
AUTHORIZED 2026-10-13 by an explicit scope decision from the owner. TODO.md
had recorded "NOT AUTHORIZED: P17-VIZ-001 passing will not be permission to
start it", which blocked CLAUDE from starting it unprompted -- not the owner
from authorizing it.
```

## Evidence

| Document | What it holds |
| --- | --- |
| [COMMAND_MODEL.md](COMMAND_MODEL.md) | the audit that said build almost nothing, the eight commands, and the two changes to qualified code |
| [HISTORY_AUTHORITY.md](HISTORY_AUTHORITY.md) | what the history may and may not hold, the one declared exception, and the **measured** header-reachability table |
| [UNDO_REDO.md](UNDO_REDO.md) | why "exact" is a statement about CONTENT, and the revision check that runs the opposite way |
| [ATOMICITY.md](ATOMICITY.md) | atomic by construction, the nine refusal shapes, and the redo-branch rules |
| [RESULT_INVALIDATION.md](RESULT_INVALIDATION.md) | the asymmetry, and the three cases that run the opposite way |
| [COMPILE_FAIL.md](COMPILE_FAIL.md) | 16 cases, and the one that proved more than it was written to prove |
| [MUTATION.md](MUTATION.md) | 16 runs, 13 mutations, **three real gaps** and why each was invisible |
| [FREEZE.md](FREEZE.md) | the three-preset qualification -- 3883 x 3, 1012 x 5, 3h21m07s, four predecessors requalified, and the probe gate that halted the chain until C3 died |
| [ADR-047](../../architecture/decisions/ADR-047-structural-commands-edit-one-analysis-definition-and-solver-settings-become-canonical.md) | the command layer, why the solver settings became canonical, and the two designs rejected |

## The shape of the milestone

**The audit was the valuable part, and it said build almost nothing.** Four of
the five capabilities the brief asks for already existed:

```text
Command / CommandHistory      the three guarantees, already in its contract
AddObjectCommand              create and delete, same ObjectId on redo
Document::modifyObject        validates through setDefinition BEFORE assigning
the object's REVISION         already what makes a result non-current
```

So this milestone adds **no command framework, no invalidation mechanism and
no identity type**. It adds eight commands on the existing base, and two
changes to qualified code that it could not avoid.

Three questions the brief asked to be *decided* the project had already
decided — load order is the user's and preserved, an analysis edit stales the
result but not the mesh, and a no-op edit succeeds without invalidating but is
still recorded. Following the project rather than inventing answers is what the
audit was for, and each is now asserted by a test rather than left as a
comment.

## The two changes to qualified code

**Solver settings became canonical.** The definition deliberately omitted them
because `SolverSettings` did not exist; P17-SOLVE-001 created it, so the reason
expired. The field had to exist for a command to edit it — settings that live
only as an argument to the solve cannot be undone, redone or persisted.
`solveStructuralAnalysis` now reads them from the document and lost its
parameter. The type moved to `StructuralData.hpp` so command headers reach the
definition without reaching the assembled system.

**`validate(const StructuralLoad&)` was exposed**, implemented by the same
`isFiniteValue` helper `prepareStructuralLoads` already used, in the same
translation unit — one rule, two callers. A command must refuse a NaN force
with no mesh in hand; the prepare path keeps its `LoadProblem` classification.

Requalified by this run: **P17-DATA-001, P17-SOLVE-001, P17-VALID-001** and
**P17-LOAD-001**. The debt is named rather than assumed away.

## Totals

```text
runtime tests          25 -> 32 StructuralCommand_ cases
compile-fail           16 cases, 16 expected failures, 0 unexpected passes,
                       plus a control target that must compile
mutation               16 runs, 13 distinct mutations, 12 killed,
                       1 equivalent mutant documented, 3 real gaps closed
architecture           15/15 (the new structural-to-core command edge)
```

## Three things that went wrong and were recorded rather than tidied away

**A `static_assert` caught the new field, which is the guard working.**
`StructuralDataTests` keeps a mirror struct of the permitted definition members
and asserts a size equality. Its own comment states the convention: extending
the mirror *is* where the new field's authority gets stated. So the mirror
gained `SolverSettings solver` and a note saying why that is canonical intent.

**A compile-fail case proved more than it was written to prove.** It expected a
refused conversion; the measured diagnostic was `'StructuralResult' is not a
member of 'bettercad::structural'`, because the result header is not reachable
from a command translation unit at all. The case **failed until the claim was
corrected** — the measurement improved the statement rather than the statement
being bent to fit.

**One claim was deliberately weakened, because the strong version is false.**
Preprocessing shows the assembled system and every result array are unreachable
from the command header — but `DofIndex`, `MeshDofMap`, `ConstraintSet` and
`FreeEquationMap` *are* reachable, through `StructuralRestraint.hpp` →
`StructuralDof.hpp`, which is how the canonical restraint type has always been
declared. Include isolation is therefore partial, and the compile-fail cases
carry the DOF family instead. HISTORY_AUTHORITY.md says so rather than claiming
the header "cannot see" solver-local types.

## One place the brief and the qualified codebase disagreed

The brief lists a `NodeId` in a load command as an automatic failure. P17-LOAD-001
declares `NodalForceLoad` mesh-local **on purpose**: it carries the `MeshStamp`
it belongs to, is refused against any other mesh "including one with the same
node count and the same numeric handles", and exists for fixtures.

Under this repository's conflict order — verification evidence → TODO →
architecture → roadmap — the qualified evidence wins. A command that adds one
carries that handle as part of a canonical record that declares its own mesh
binding; forbidding it would leave a qualified canonical load form with no
undoable editing path. What is forbidden, and probed, is a `NodeId` or
`ElementId` as a command's **target** and every species of derived solver
state. The owner was asked and confirmed this reading before any code was
written.
