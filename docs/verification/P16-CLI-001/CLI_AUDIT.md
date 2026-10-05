# P16-CLI-001 — CLI architecture audit

```text
SUBJECT:  what the CLI already is, which core APIs each proposed command
          routes to, and the one design decision this milestone has to make
DATE:     2026-10-05
AT:       7024649
```

Written before any production code. As with P16-PERSIST-001, most of the
brief's architecture questions turn out to be answered by what is already
there — and the one that is not is the output format.

## 1. The CLI as it stands

```text
apps/bettercad_cli/Cli.hpp          kProgramName, ExitCode, run()
apps/bettercad_cli/Cli.cpp          kCommands, the dispatch, help, version
apps/bettercad_cli/Commands.hpp     one usage string + one handler per report
apps/bettercad_cli/Edits.hpp        the EDIT SPINE: EditCommand, runEdit
apps/bettercad_cli/EditRegistry.cpp the one listing of edit verbs
apps/bettercad_cli/Selectors.hpp    how the command line names model objects
apps/bettercad_cli/Arguments.hpp    option parsing, parseLength, parseAngle
apps/bettercad_cli/BatchCommand.cpp the N-edit transaction driver
```

| Question | Answer |
| --- | --- |
| Entry point | `cli::run(args, out, err)`, `noexcept`; `main.cpp` is a `wmain` shim |
| Command parser | a `constexpr std::array kCommands` of `{name, usage, summary, handler}` |
| Subcommand architecture | **two** kinds: *reports* (handlers in `kCommands`) and *edits* (`EditCommand` in a registry) |
| Open/save | reports load and never write; edits go through `runEdit`, which loads, applies, and **saves only if the edit succeeded** |
| Structured output | **none** — there is no `--json` and no field-oriented mode (`--format` is paper size, A0–A4) |
| Error conventions | `failure(command, code, problem, err)`, where `code` is a **stable machine name** |
| Exit codes | `Success = 0`, `Failure = 1` (ran, reported a problem), `UsageError = 2` (command line invalid) |
| Test harness | in-process `runCliCommand` for behaviour, plus `bettercad_add_process_test` for the real executable |

## 2. The edit spine is the answer to half this milestone

`Edits.hpp` states its own design, and it is exactly what the brief's
mutation requirements ask for:

> Every document-changing command is a function over an ALREADY-LOADED
> document. Two drivers use the same functions: single-shot `load -> apply one
> edit -> save`, batch `load -> apply N edits -> save`, so the one-line form
> and the scripted form cannot disagree about what a command means — the
> single-shot command is literally the batch of one.
>
> Atomicity is a property of that shape rather than of care taken at each call
> site: nothing is written until every edit has succeeded.

Three consequences for P16:

**Mutations are `EditApply` functions, not handlers.** `set global size`, `add
local sizing` and `remove local sizing` become entries in a
`meshingEditCommands()` table in a new `MeshingEdits.cpp`, joined in
`EditRegistry.cpp`.

**That makes them scriptable for free.** `batch` drives the same table, so a
meshing verb registered there is a batch verb in the same
one-transaction-or-nothing script as a mate verb, with nothing added to
`BatchCommand.cpp`. P15-CLI-001 recorded this as the property that made
materials scriptable without new driver code.

**Failure atomicity needs no new work and no new tests of its own
mechanism.** `runEdit` saves only on success, so the brief's §93 — "invalid
mutation must not alter saved state" — is a property of the spine. It is still
tested, because a property one believes is not a property one has measured.

## 3. Reports never write, and that is a recorded decision

P15-CLI-001's header comment:

> These five report and never write: everything they print is either canonical
> state the document already holds or state derived from it, and a report that
> saved its own derivation would be caching an answer the next load has to
> recompute anyway.

So `mesh settings`, `mesh info`, `mesh quality`, `mesh validate` and
`mesh boundaries` load, compute and print. **None of them saves a mesh**,
which is also P16-PERSIST-001's authority boundary expressed headlessly.

## 4. The face-reference syntax already exists — reuse it

`Selectors.cpp` has `parseNamedFace`, reading `<feature>:<role>[:<entity>]`
after a kind prefix, with roles `start_cap`, `end_cap`, `side`, `hole_bottom`,
`counterbore_floor`, `chamfer`, `spotface_floor`, and an entity for a side
face. It is the grammar drawings and mates already use, and its header
documents the one thing it cannot name — a copied face — as "a recorded
limitation of the interface, not of the model".

It is currently **file-local**. The brief's §14 says to reuse existing syntax
and §5 says to improve the shared API rather than duplicate logic in a
command, so it is exported from `Selectors.hpp` and the meshing commands call
it. A second face grammar for meshing would be a second contract, and
`face:Block:side:5` would eventually mean two things.

`parseLength(text, defaultUnit)` likewise already exists, so `10mm` and
`0.01m` parse to the same `Length` through the parser every other command
uses. There is no new quantity syntax.

## 5. Core API routing

Every proposed command and the existing entry point it calls. No row needs a
new core capability.

| CLI operation | Core API | Mutates? | Needs a mesh? |
| --- | --- | --- | --- |
| mesh settings | `meshing::meshControls`, `findMeshControl`, `MeshControl::definition`, `orderedLocalSizing`, `orderedBoundarySets` | no | no |
| set global size | `meshing::SetGlobalMeshSizeCommand` via `CommandHistory` | yes | no |
| add local sizing | `meshing::AddLocalMeshSizingCommand` | yes | no |
| remove local sizing | `meshing::RemoveLocalMeshSizingCommand` | yes | no |
| mesh generate | `features::Regenerator::regenerate`, then `meshing::Mesher::generate` | no (derived) | creates one |
| mesh info | `Mesher::currency`, `VolumeMesh::nodeCount / mesh().elementCount() / tetrahedralVolume / conformity / source / revision` | no | yes |
| mesh quality | `Mesher::quality` (a `MeshQualityReport` from `evaluateMeshQuality`), `qualityDescribesCurrentPolicy` | no | yes |
| mesh validate | `meshing::validate(const Mesh&)` → `MeshValidationReport` | no | yes |
| mesh boundaries | `meshing::resolveBoundarySet(set, map)`, `Mesher::map` | no | yes |

**The CLI computes nothing.** No tetrahedron volume, no aspect ratio, no
dihedral angle, no nearest-face search, no serialization. Those live in
P16-VOL, P16-QUALITY, P16-MAP and `io`, and the commands are adapters over
them.

**Mutations go through the P16-CMD command objects**, and the CLI already has
the helper for it. `EditSupport.hpp` provides

```cpp
inline std::optional<EditFailure> execute(Document& document, Command& command)
```

whose comment states the reason: *"Every edit that has a command goes through
one, so the CLI inherits that milestone's validation and its all-or-nothing
execution instead of restating either (ADR-009)."*

It calls `command.execute(document)` directly — **no `CommandHistory`**, which
is right for a one-shot process that has nothing to undo into, and consistent
with the undo stack not being persisted (P16-PERSIST-001). The brief's §12 and
§66 allow either a command or a lower-level mutation provided the canonical
result is identical; going through the command objects means the CLI cannot
acquire different validation, atomicity or no-op behaviour from the GUI,
because it is running the same objects.

*(Noted against an earlier draft of this audit, which assumed a
`CommandHistory` was constructed per edit. It is not; the helper is simpler
than that and the correction is recorded rather than quietly replaced.)*

## 6. `BetterCAD::meshing` is a new CLI dependency

The CLI links core, assembly, drawing, features, io and sketch — not meshing.
Adding it is the only build change. Verified today that **nothing in
`apps/bettercad_cli/` mentions `nglib`, `Netgen` or `Ng_`**, and that must
stay true: the backend lives behind `src/meshing/netgen/` and
`architecture.layering` fails the build if its headers escape.

## 7. The one real decision: there is no structured output

This is the only question the existing CLI does not answer, so it is recorded
rather than assumed.

**What the project does today.** P15-CLI-001's evidence states the design:
exit codes carry the gate — *"a report that gates is only usable from a script
if the gate reaches the exit status"* — failures carry stable machine codes,
and the streams are disciplined so that "could not answer" prints nothing to
stdout. Success output is human prose, asserted in tests by regex. There is no
JSON mode anywhere.

**What the brief requires.** §61: a script must be able to extract node count,
element count, volume, quality status, validation status and boundary count
*"without brittle prose scraping ... If no structured-output framework exists,
add the smallest general mechanism rather than P16-only regex-targeted text."*
§62 names camelCase fields; §104 requires human and structured output to agree.

**The decision.** Add a **small, general** structured writer to the CLI and
have the mesh reports accept `--json`. General so that any future command can
adopt it; small so that it is not a refactor of three qualified command
surfaces. **No existing command's output changes**, because P13-CLI, P14-CLI
and P15-CLI all have tested output and rewriting it would be combining a
refactor with feature work — which the engineering rules forbid, and which
would put a qualified surface at risk for no benefit to this milestone.

What that costs honestly: after this milestone the CLI has structured output
for meshing and prose for everything else. That is an inconsistency, it is
recorded as a known limitation, and adopting `--json` elsewhere is future
work. The alternative — P16-only regex-targeted text — is the thing §61
explicitly rules out.

The writer uses the `nlohmann_json` target the build already fetches and that
`src/io` already links. Hand-rolling a JSON emitter would risk escaping bugs
in a new interface for no gain.

## 8. Scope

```text
IN     five mesh reports and three mesh edits, as adapters over existing core
IN     a small general JSON writer in the CLI, with --json on the reports
IN     exporting parseNamedFace from Selectors.hpp so the face grammar is shared
IN     BetterCAD::meshing on the CLI target
IN     targeted tests, process tests on the real executable, an end-to-end
       scripted workflow (valid and negative), core/CLI equivalence, and a
       zero-match filter guard

OUT    converting existing commands to --json (recorded as a limitation)
OUT    a mesh-edit verb beyond add/remove local sizing: `mesh local edit` is
       NOT exposed. P16-CMD-001 has EditLocalMeshSizingCommand, but the CLI's
       own philosophy for a one-shot process is remove + add, and the brief
       (§17) asks for the decision to be documented rather than for the verb
OUT    any new meshing semantics, defaults, tolerances or thresholds
OUT    the carried FileIo atomic-replace defect
```
