# P2 — Document model (P2-001, P2-002): verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 139/139 passed |
| release | exit 0 | exit 0 | 0 | 139/139 passed |
| debug-shared | exit 0 | exit 0 | 0 | 139/139 passed |

The `debug-shared` run also shows that object kinds defined outside the core
DLL (the test objects) work with `Document`, including `dynamic_cast`-based
typed lookup across the DLL boundary.

## P2-001 — Document

`Document` (`include/bettercad/core/document/Document.hpp`) holds parameters
(a `ParameterTable`), polymorphic `DocumentObject`s and `DocumentMetadata`.
Object kinds (sketch, feature, body) come from higher modules and plug in by
subclassing `DocumentObject`, so core does not depend on them.

| Requirement | Tests (`tests/core/document/DocumentTests.cpp`) |
|-------------|----------------------------------------------|
| Create document | `A new document has an identity, a name and no content` |
| Assign stable ID | `Documents get distinct IDs unless one is given` (random UUID v4, or a given ID) |
| Name document | `Document names are validated and renames are tracked` |
| Add/remove objects | `Objects can be added, found by ID and name, and removed`; `IDs are never reused and insertObject restores an ID`; `addObject needs a new object and insertObject an identified one` |
| Lookup by ID | same as above; `Typed lookup checks the object kind`; `Lookup by name and ID works across kinds` |
| Lookup by name | `Lookup by name and ID works across kinds`; `Names are unique across parameters and objects`; `Renaming works for parameters and objects and keeps names unique` |
| Dirty-state tracking | `Dirty state follows effective changes since markClean` (no-ops and failures do not dirty) |
| Revision counter | `The revision counts every effective change` (8 operations, revision 8); `modifyObject tracks changes and checks the kind` |
| Also covered | shared ID space (`Parameters and objects share one ID space`), ID listing order, `uniqueName`, `restoreParameter`, metadata, `clone()` independence, `equivalent()` sensitivity (name, metadata, parameter value, object content, identity) |

## P2-002 — Commands and undo/redo

`Command` has `execute()`, `undo()` and `redo()`; `CommandHistory` holds the
undo and redo stacks for one document. Implemented commands:
`CreateParameterCommand`, `ModifyParameterCommand` (name, value, display
unit and expression; validated on a copy, then applied atomically),
`AddObjectCommand`, `DeleteObjectCommand` (parameters and objects) and
`RenameObjectCommand`.

`CreateSketchCommand` needs the `Sketch` object kind from the sketch module
(P4). It will be implemented there on top of `AddObjectCommand` and is listed
under P4 in `TODO.md`.

### Acceptance: edit → undo → redo returns deterministic equivalent states

`equivalent(Document, Document)` compares identity, name, metadata,
parameters (ID, name, SI value, unit, expression) and objects (ID, name,
kind, content). It ignores revision counters.

| Test | What it shows |
|------|---------------|
| `Edit, undo and redo return the document to equivalent states` | 6 different commands; after each undo the document equals the snapshot before that command; after each redo it equals the snapshot after it (32 assertions) |
| `Random edit, undo and redo sequences stay consistent and deterministic` | 500 random steps (seed 20260914): 140 commands succeeded, 115 were rejected. After **every** step the document equals the snapshot for the current history position, and each rejected command left no trace. Undoing everything restores the initial state; redoing everything restores the final state. Replaying the seed produces an equivalent document with the same IDs (2426 assertions) |
| Per-command round trips | Create, Modify (5 variants), Add, Delete (object and parameter), Rename (parameter and object): execute/undo/redo compared against snapshots |
| `CreateParameterCommand executes, undoes and redoes with the same ID` | redo restores ID 1; a new creation after undo gets ID 2 (IDs never reused) |
| `ModifyParameterCommand is atomic` | a valid value with a taken name, or a valid name with a wrong-dimension value, changes nothing |
| History behaviour | stacks and descriptions, redo cleared by a new command, failed commands not recorded, nothing-to-undo errors, refusal to act on another document, history limit |

Counts in the table come from running the tests with `-s`, e.g.
`CHECK( first.executed > 50 ) with expansion: 140 > 50`.

## Findings during implementation

- **GCC 16 flags omitted designated initializers.**
  `-Wmissing-field-initializers` warns when designated initializers omit
  members that have no default member initializer. `ParameterChanges` is
  meant for `ParameterChanges{.value = ...}`, so its fields (and those of
  `DimensionedValue` and `DocumentMetadata`) now have explicit `{}` defaults.
- **Unchecked `.error()` calls in new tests.** Several tests called
  `.error()` on a `Result` without checking that it failed, which is
  undefined behaviour if the call unexpectedly succeeds. They now use
  `test::errorCode()`, which returns `std::nullopt` on success.
- **Commas in test names.** A comma in a Catch2 test-name filter means OR.
  CTest (via `catch_discover_tests`) escapes the comma correctly, as the
  `Test command` lines in the verbose output show.
- **Dirty tracking is conservative.** Undo after saving leaves the document
  dirty, even when the content matches what was saved. This is documented
  and tested.
