# P16-CLI-001 — mutation testing

```text
SUBJECT:  whether the CLI tests detect the defects they claim to
TARGET:   apps/bettercad_cli/{MeshingEdits,MeshingReports}.cpp, StructuredOutput.hpp
JUDGED BY: bettercad_tests.exe "[meshcli]" under debug-ext
RESULT:   6 killed, 2 equivalent mutants (proven), 1 PRODUCTION DEFECT found
```

| # | Mutation | Verdict | Failing assertions |
| --- | --- | --- | --- |
| M1 | the CLI substitutes its own default target size | killed | 2 |
| M2 | the CLI validates the size itself and bypasses the core | killed | 4 |
| M3 | a failed core mutation is reported as success | killed | 4 |
| M4 | a failed mesh generation exits zero | killed | 1 |
| M5 | a structurally invalid mesh does not gate `mesh-validate` | survived — **equivalent mutant**, proven | — |
| M6 | the CLI skips the regeneration precondition | survived → **a real defect**, fixed, re-expressed, **killed** | 7 |
| M7 | local controls listed in stored order | survived — **equivalent mutant**, proven | — |
| M8 | the JSON writes a bare number with no unit | killed | 9 |

**This run was worth doing.** Three mutations survived and only two of them
were harmless; the third pointed at a production defect that every other gate
had missed.

## M6 found a production defect

The mutation removed the regeneration check from the CLI and survived, because
no fixture had a model that fails to build. Writing one exposed the real
problem, which was not the missing check but a misreading of the core:

```text
Regenerator::regenerate() returns an ERROR only for a document-level fault.
A FEATURE that fails to build is a SUCCESS carrying `failed`, `blocked` or
`cycles` in its report.
```

The CLI checked only the `Result`. So a document whose extrude did not build
passed the precondition, and the command then reported **`no_mesh_control`** —
true, and not the reason. The brief's §44 names this exactly: *"Do not
translate every problem into 'mesh command failed' only."*

Fixed by inspecting the report and keeping the core's distinctions:
`dependency_cycle`, `regeneration_failed` (naming the object and carrying its
own error), `regeneration_blocked`. The mutation was then re-expressed against
the new code — the old text no longer existed, so the harness correctly
reported `PATTERN-ABSENT` rather than a verdict — and killed with 7 failing
assertions.

Two smaller corrections came with it. The fixture's premise had to be asserted
rather than assumed: a single-line sketch does **not** make `regenerate()`
return an error, which is how the misreading surfaced. And
`mesh-control-add` on a broken model still **succeeds**, which is correct —
creating intent does not require working geometry, the same principle
P16-PERSIST-001 settled for an unresolved face reference — so the test asserts
that and then asserts that `mesh-generate` still refuses, for the model's
reason.

## M5 is an equivalent mutant: the branch cannot be reached

`mesh-validate` returns `dataValid() ? Success : Failure`. The mutation always
returns `Success` and cannot be distinguished, because **no mesh the CLI can
obtain is ever invalid**:

```cpp
// src/meshing/VolumeMesh.cpp, inside volumeMeshFor
const MeshValidationReport report = validate(volume);
if (!report.dataValid()) {
    return failure(VolumeMeshFailure::InvalidMesh, std::move(detail));
}
```

The core validates before returning, so `dataValid()` is always true for any
`VolumeMesh` that reaches a caller. No fixture, and no geometry, can make the
failure branch fire.

The gate stays. It is correct, it costs nothing, and it is the right behaviour
if that upstream invariant ever weakens — but it is **defence in depth, not a
tested gate**, and the evidence says so rather than implying `mesh-validate`
has a verified failure path. A contrived test would have to hand-build an
invalid `VolumeMesh`, which ADR-030 makes impossible by design.

## M7 is an equivalent mutant: persistence already canonicalises the order

The mutation lists the stored vector instead of `orderedLocalSizing()`. It
survived even after a test was added that builds two documents with the
controls added in **opposite** orders and requires identical output.

The reason is upstream, and measured:

```text
$ mesh-local-add <doc> face:Extrude001:end_cap   2mm     # added FIRST
$ mesh-local-add <doc> face:Extrude001:start_cap 3mm     # added SECOND
file order:  start_cap 0.003
             end_cap   0.002
```

P16-PERSIST-001's serializer writes the **canonical** order, so every CLI
invocation loads a document whose stored order already *is* canonical. The two
expressions cannot differ through a file.

The ordered accessor is still the right call — an in-memory document edited by
the GUI can hold a non-canonical stored order, and the GUI reads the same
accessor — but the CLI cannot observe the difference. The new test asserts a
true and useful user-visible property (same intent, same listing) and is kept;
what it does **not** do is kill M7, and claiming otherwise would misdescribe
where the guarantee comes from.

## M4 died on a single assertion

Worth stating rather than glossing. Only one assertion inside `[meshcli]`
checks that a failed generation exits non-zero. The second line of defence is
outside that filter: the end-to-end script asserts the exit code of six
distinct failure scenarios, and `cli.mesh.settings.no-control` is a process
test. One assertion is enough to kill the mutation; it is also exactly one.

## How to run it

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
python docs/verification/P16-CLI-001/qualification/mutation/run-remaining.py
```

Launched detached, restoring in a `finally` block rather than a shell trap,
for the reason P16-PERSIST-001 recorded: an orphaned harness once left a
mutation applied to an untracked source file, which `git status` and
`git diff` both report as clean. **Afterwards, whatever happened, diff the
working files against `pristine/`.**
