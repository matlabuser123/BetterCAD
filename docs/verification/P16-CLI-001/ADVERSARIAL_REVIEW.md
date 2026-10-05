# P16-CLI-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before marking it complete
METHOD:   the brief's 29 attack questions against the final diff, then 8
          mutations of the CLI
```

## 1. The attack questions

| # | Attack | Answer |
| --- | --- | --- |
| 1 | Can the CLI call nglib directly? | **No.** Zero `nglib`, `Ng_[A-Z]` or `Netgen` occurrences in `apps/bettercad_cli/`, checked case-sensitively, and `meshing` links Netgen **PRIVATE** so the CLI cannot reach it transitively. `architecture.layering` fails the build if a backend header escapes `src/meshing/netgen/`. |
| 2 | Can the CLI have its own default target size? | **No.** An absent `--size` leaves `globalTargetSize` empty, which *is* BetterCAD's scale-relative default, and prints as `null` / "BetterCAD default" rather than a number. Mutation **M1** inserts one. |
| 3 | Can the CLI interpret `10mm` differently from the GUI? | **No.** `parseLength(text, units::mm)` is the CLI's existing parser, shared with every other command. `10mm`, `0.01m` and `1cm` are asserted equal in one test. |
| 4 | Can an invalid CLI size mutate the document before failing? | **No.** `runEdit` saves only on success, so a refusal writes nothing. Measured byte-for-byte for six values in the targeted test and again for six refused commands in the end-to-end script. |
| 5 | Can a failed command still save partial changes? | **No**, same mechanism — atomicity is a property of the edit spine, not of care at each call site. |
| 6 | Can CLI quality recompute its own radius ratio? | **No.** `mesh-quality` prints `MeshQualityReport`'s own fields; there is no `radiusRatio`, `dihedral`, `aspectRatio`, `std::sqrt` or `determinant` anywhere in the CLI. |
| 7 | Can CLI validation disagree with the core, e.g. via `abs(volume)`? | **No.** `mesh-validate` calls `meshing::validate(const Mesh&)` and prints its issues. The equivalence test compares `dataValid` against the core's. |
| 8 | Can the boundary query use nearest-face logic? | **No.** `meshing::resolveBoundarySet` does the resolution; `nearest` appears nowhere in the CLI. A face with no facets is reported as unresolved, never substituted. |
| 9 | Can a `NodeId` be a persistent local-sizing target? | **No.** There is no syntax for one. `node:4` exits 2 with a message naming the `face:<feature>:<role>` form. |
| 10 | Can a current mesh be reported after the controls changed? | **Not reachable.** Every query generates from the intent it just loaded, so there is no held mesh to go stale — see finding **C5**, which records this as a consequence rather than a claim of correctness. |
| 11 | Can stale geometry be meshed headlessly though the GUI refuses? | **No.** `prepare()` regenerates and propagates the core's refusal as `regeneration_failed`; the core itself refuses to mesh stale geometry (P16-GEOM-001). Mutation **M6** skips the check. |
| 12 | Can `NG_OK` with zero tetrahedra look like success? | **No.** The CLI never sees a backend code; `Mesher::generate` returns a `Result`, and P16-VOL-001's zero-element trap is refused inside the core. The CLI's only job is to propagate, and mutation **M4** makes it return success on failure. |
| 13 | Can an unresolved control be ignored only in the CLI? | **No.** `volumeMeshFor` refuses rather than silently ignoring a refinement (P16-PERSIST-001 finding A8), and the CLI propagates it: exit 1, `mesh_generation_failed`, with the core's "does not resolve". Asserted. |
| 14 | Can human and JSON output disagree? | **No.** `MeshingCli_HumanAndJsonAgree` compares the node count, element count, the PASS/FAIL word and the exit code between the two renderings. |
| 15 | Can a CLI error return exit 0? | **No**, and mutation **M4** is exactly that mutation. Every failure path in the matrix is exercised. |
| 16 | Can a successful query return non-zero? | **No**, and one case is deliberate: a boundary query reporting `"resolved": false` exits **0**, because observation is not operation. |
| 17 | Can a malformed document partially overwrite an existing file? | **No.** A load failure returns before any edit runs, and `documentFromJson` builds a new `Document` so there is nothing half-loaded to write back. |
| 18 | Can the multi-process workflow depend on persisted tetrahedra? | **No.** The script asserts the saved document contains no `tetrahedra`, `"nodes"` or `connectivity`, and that it is byte-identical after a generate. |
| 19 | Can a fresh load query old quality without regenerating? | **No.** There is no stored report to read; `mesh-quality` meshes and evaluates, and says `"meshSource": "generatedNow"`. |
| 20 | Can a PATH-resolved stale executable be tested? | **No.** Every process test and the script take the binary from `$<TARGET_FILE:bettercad_cli>`, an absolute path, and the script prints it with its size. |
| 21 | Can Debug qualification invoke the Release binary? | **No**, for the same reason: `$<TARGET_FILE:...>` resolves per preset, so a preset's tests run that preset's binary. The three-preset run therefore proves three binaries. |
| 22 | Can the CLI load a foreign MSYS2 or Netgen runtime? | Not newly: the CLI links the same `BetterCAD::meshing` the tests do, and INFRA-NETGEN-001 qualified that closure. The process tests run it under CTest's environment, which is the same one the existing `cli.*` tests have always used. Recorded as inherited rather than re-proved. |
| 23 | Can `ctest -R` match zero tests and still count as PASS? | **It can, and that is why the guard exists.** `cli.mesh.zero-match-guard` asserts a discovered **count** per filter, proves its own counter by running an impossible filter, and records that `ctest` exits **0** on that filter. |
| 24 | Can the E2E script continue after a failed command? | **No.** Each step is a `FATAL_ERROR` on an unexpected exit code, carrying the command, stdout and stderr. Demonstrated during development: a wrong feature name in the fixture stopped the run at step 4 — finding **C9**. |
| 25 | Can core and CLI produce different element counts for identical intent? | **No**, compared exactly in the equivalence test over a fixture with >100 elements. |
| 26 | Can CLI output ordering depend on unordered maps? | **No.** Local controls and boundary sets come from the ordered accessors, quality metrics from a `std::map` keyed by the metric, findings in the core's order, and JSON fields in insertion order. Two runs are byte-identical, with the controls deliberately added out of canonical order. Mutation **M7**. |
| 27 | Can `mesh-info` dump an unbounded number of elements by default? | **No.** It is a summary at any size; the test requires the payload under 2000 bytes for a mesh of several hundred elements and asserts no `"nodes": [` or `"elements": [` array. |
| 28 | Can headless commands need GUI or viewer initialisation? | **No.** The CLI links core, assembly, drawing, features, io, meshing and sketch — not `BetterCAD::renderer` and no Qt. The mesh reports use `Mesher`, not `MeshView`. |
| 29 | Can `--help` initialise the mesher or open a document? | **No.** `help` is a handler over the static `kCommands` table and touches no document; the mesh handlers are only reached by name. |

## 2. Findings

### C1 — Netgen writes to stdout, which made structured output unparseable (production defect, fixed)

The first `mesh-info --json` could not be parsed. The first line a script read
was not `{` but

```text
 WARNING: RestrictLocalH called, creating mesh-size tree
```

printed by Netgen to **stdout** — `2>/dev/null` did not remove it. That defeats
the whole point of a machine-readable mode, and it was invisible until the
output was actually fed to a parser rather than eyeballed.

**Fixed in the backend adapter, not in the CLI.** `src/meshing/netgen/`
already owns the backend's quirks, and stdout is a shared resource: fixing it
in the CLI would have left the GUI, the tests and every future consumer with
the same corrupted stream *and* a CLI compensating for it. A `SilentBackendScope`
swaps `std::cout`'s streambuf for the duration of the nglib calls, under the
backend mutex that is already held, so nothing races and nothing else is
affected.

Worth noting what was *not* done: the message was not surfaced as a
diagnostic. It is informational noise about an internal data structure, with
no action attached — P16-SIZE-001 records it as the expected behaviour of
local sizing — so turning it into a warning would have been noise with a
diagnostic code.

### C2 — my JSON renderer emitted stray commas (defect in new code, fixed)

`set()` called the public `render()`, which appends a trailing newline, so
every field arrived already terminated and the comma landed on its own line
with a pad before each closing bracket. Valid JSON, unreadable output. Found
by looking at it; fixed by rendering children with the depth-aware form.

### C3 — face references were not re-feedable (defect in new code, fixed)

Output used `label()`, which produces `Extrude001 (object:6)` — good prose,
not a selector. The claim that output could be fed back in was therefore
false. Now the feature's own name, which `resolveObject` accepts, with the ID
as a fallback.

### C4 — there was no structured-output framework, and the decision is recorded

The CLI's machine-readability was deliberate and documented by P15-CLI-001:
exit codes carry the gate, failures carry stable codes, the streams are
disciplined. Success output was prose.

The brief (§61) requires scriptable fields and says to "add the smallest
general mechanism rather than P16-only regex-targeted text" if none exists. A
small general writer was added and adopted **only** by the mesh reports.
Converting P13's, P14's and P15's commands would be rewriting three qualified
output surfaces — a refactor inside a feature milestone, which the engineering
rules forbid.

The honest cost: the CLI now has structured output for meshing and prose
everywhere else. That inconsistency is a known limitation, not a finished
CLI-wide format, and saying so is the point of recording it.

### C5 — `mesh-info` can only ever report `current` (documented consequence)

The brief's §91 asks for a stale-state workflow. It is unreachable headlessly:
a mesh is never persisted, so a one-shot process always generates from the
intent it just loaded and cannot hold a mesh while its intent moves.

This is a consequence of P16-PERSIST-001's authority boundary, not a gap. The
stale states exist in core and are tested there (P16-CMD-001's invalidation
matrix). What would be wrong is persisting a mesh to make the CLI able to
report staleness — the one thing the derived-state rule forbids. Recorded in
the contract and in the limitations rather than quietly omitted.

### C6 — a negative size needs `--`, and both paths are in the matrix

`-1mm` begins with a hyphen, so the parser reads it as an unknown option and
exits **2** before the value becomes a quantity. With `--`, which the parser
already supports, it reaches P16-SIZE-001's validator and exits **1** with the
core's own "size is -0.001 m, which is not positive".

Both are non-zero and both leave the document unchanged, so the brief's
requirement holds either way — but only one of them exercises the core
validator, and the matrix records both rather than presenting the convenient
one. No parser change was made: `--` is the POSIX convention and inventing a
"leading hyphen before a digit is positional" rule would be a general change
to argument handling for one command's benefit.

### C7 — the face grammar was file-local, and was exported rather than copied

`parseNamedFace` lived in an anonymous namespace in `Selectors.cpp`. Copying
it would have created a second reader of `face:Block:side:5`, and the two
would eventually disagree. It is now declared in `Selectors.hpp` and reached
through a thin `parseFaceReference`, so drawings, mates and meshing share one
implementation of the grammar.

### C8 — `mesh local edit` is deliberately not exposed (decision, §17)

P16-CMD-001 has `EditLocalMeshSizingCommand`, so the verb would be cheap. It
is not exposed: for a one-shot process `mesh-local-remove` then
`mesh-local-add` is the same canonical result in two commands that already
exist, and `batch` makes it one transaction when atomicity matters. The brief
asks for the decision to be documented rather than for the verb, and adding a
fifth mutating verb with no new capability is surface for its own sake.

### C9 — the workflow fixture named a feature that does not exist (test defect, caught by the script's own fail-fast)

The script was written against `face:Solid:end_cap`, the name used by the C++
test helper, while `plate.bcad`'s extrude is `Extrude001`. Step 4 stopped the
run with the command, the exit code and the core's message. Worth recording
because it is the fail-fast requirement working on its first real use rather
than on a contrived test.

### C10 — two process-test regexes spanned newlines (test defect, fixed)

Patterns asserting several fields at once used `\[\n *` and an escaped `[`
that opened a character class, so `cli.mesh.info` failed on a correct payload.
Reduced to one unambiguous field per process test; the thorough field-by-field
assertions live in the workflow script and the in-process tests, which is
where they read clearly.

## 3. Mutation testing

Eight mutations of the CLI, each a single substitution, judged by whether
`[meshcli]` fails.

| # | Mutation | Must be caught by |
| --- | --- | --- |
| M1 | the CLI substitutes its own default target size | the absent-size test |
| M2 | the CLI validates the size itself and bypasses the core | the invalid-size matrix |
| M3 | a failed core mutation is reported as success | the invalid-size matrix |
| M4 | a failed generation exits zero | the failure-propagation tests |
| M5 | a structurally invalid mesh does not gate `mesh-validate` | the validate/quality split |
| M6 | the CLI skips the regeneration precondition | the failure-propagation tests |
| M7 | local controls listed in stored order | the determinism test |
| M8 | the JSON writes a bare number with no unit | the settings payload tests |

Results in [qualification/mutation/](qualification/mutation/).
