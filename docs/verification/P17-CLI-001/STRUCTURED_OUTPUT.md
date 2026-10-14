# P17-CLI-001 — structured output and the stream contract

```text
--json     stdout is EXACTLY one JSON object, and nothing else
no --json  stdout is prose, carrying the same numbers
failure    see "the two kinds of failure" below
```

## The contract, which is BetterCAD's and not this milestone's

The brief suggests a `{"status": "error", "code": ...}` envelope on stdout for
every outcome. BetterCAD's contract is different and the brief's §4 and §48
both say to follow the existing one. It distinguishes two things that envelope
merges:

### "Could not answer" — stdout is EMPTY

A missing file, no analysis, two analyses, a model that will not regenerate, a
mesh that will not generate, a refused solve. The code and message go to
**stderr** in the project's established shape, and the exit status is 1 (or 2
for a command-line fault):

```text
$ bettercad-cli structural-settings plate.bcad
bettercad-cli structural-settings: no_structural_analysis: this document has no
structural analysis; structural-analysis-add creates one
                                                              [stdout: empty]
                                                              [exit: 1]
```

`cli.mesh.settings.no-control` is the precedent and asserts exactly this
shape. The reason it is better than an error payload: a refusal that cannot
answer writes **nothing** to stdout, so it can never be mistaken for an
answer by a consumer that forgot to check the exit status.

### "The answer is a failure" — the payload IS written, and the exit is still 1

A validation report whose verdict is `rejected` is an answer. So
`structural-validate` writes the whole report to stdout **and** exits 1:

```json
{
  "status": "rejected",
  "acceptable": false,
  "reachedStage": "restraints",
  "findings": [
    {
      "code": "restraints_unresolvable",
      "severity": "failure",
      "message": "a restraint does not resolve against the current mesh: restraint:5: its target face names no face of the body as it is now, so there is nothing to constrain. Nothing nearby is rebound"
    }
  ]
}
```

`mesh-validate` is the precedent, and its comment gives the reason: "a report
that gates is only usable from a script if the gate reaches the exit status."

This is the brief's §120 — "a JSON error payload may parse successfully while
the CLI exits nonzero" — and both halves are asserted separately, in the
in-process tests and again in the workflow script.

## The payloads

Field names are lowerCamelCase and name BetterCAD concepts, never a backend's.
Key order is insertion order, which is what makes the bytes deterministic
without any sorting.

### `structural-settings` — canonical intent only

```json
{
  "analysisId": 9,
  "name": "PlateStudy",
  "mode": "linear static",
  "meshControl": 7,
  "loads": [
    {
      "loadId": 1,
      "type": "pressure",
      "face": "face:Extrude001:end_cap",
      "faceResolves": true,
      "magnitude": { "value": 2500000, "unit": "Pa" }
    },
    {
      "loadId": 2,
      "type": "surface_traction",
      "face": "face:Extrude001:side:5",
      "faceResolves": true,
      "traction": { "x": 1234.5, "y": -2500, "z": 0, "unit": "Pa" }
    }
  ],
  "restraints": [
    {
      "restraintId": 1,
      "face": "face:Extrude001:start_cap",
      "faceResolves": true,
      "components": "fixed"
    }
  ],
  "solver": {
    "algorithm": "simplicial_ldlt",
    "relativeResidualTolerance": 1e-08,
    "pivotFloor": 1e-12
  }
}
```

**`face` is written the way the command line writes one**, so output can be
fed back in. **`faceResolves`** is the brief's §5: an unresolved target is
reported, never omitted and never nulled.
`StructuralCli_InspectsTheCanonicalAnalysisAndNothingDerived` additionally
checks that twelve derived key names are absent — matched *with their opening
quote*, because `restraints` contains `strain`.

### `structural-solve` — a summary at any size

```json
{
  "nodeCount": 81,
  "elementCount": 287,
  "tetrahedronCount": 127,
  "freeEquations": 119,
  "normalizedResidual": 9.914440826665752e-18,
  "pivotRatio": 0.022663604162487316,
  "strainEnergy": { "value": 0.00084942003810145876, "unit": "J" },
  "largestDisplacement": { "value": 2.5894606930351219e-07, "unit": "m" },
  "largestVonMises": { "value": 2912178.3259140174, "unit": "Pa" },
  "forceImbalance": { "value": 4.077665913544245e-12, "unit": "N" },
  "momentImbalance": { "value": 3.94847006346305e-13, "unit": "N m" },
  "validation": { "status": "accepted", "acceptable": true,
                  "reachedStage": "complete", "findings": [] }
}
```

**No array is ever dumped by default** (§24, §240). The three summary commands
exist for the detail, and a test asserts the solve payload stays under 2 KB.

**Both counts, named apart.** `elementCount` is `Mesh::elementCount()` —
surface triangles *plus* tetrahedra, which is what `mesh-info` means by it.
`tetrahedronCount` is what the physics is over. Conflating them was a real
defect: see [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

### The three summaries

`structural-displacement` carries the range of the magnitude and of `ux`,
`uy`, `uz`, plus `largest` with the node id. `structural-stress` carries
`"domain": "element"`, the range of von Mises, of all six components and of
σ₁/σ₃, plus `largest` with the element id. `structural-reactions` carries
`totalForce`, `totalMoment`, `externalForce`, the two imbalances, the moment
origin, `constrainedNodes`, the per-restraint `ownedForce`/`ownedMoment`, and
`sharedForce`/`sharedMoment`.

**The shared term is written out on purpose** (§45). P17-REACTION-001 counts a
shared constrained degree of freedom once, so the owned parts alone do not sum
to the total and the payload says so rather than presenting a sum that is not
one.

## Units are never implicit

Every engineering number is either a `quantity` — `{"value": ..., "unit": ...}`
— or a vector with a `unit` member. `JsonValue::quantity`'s own comment is the
rule: "NEVER A BARE NUMBER. A size written as `0.01` with no unit anywhere is
the shape of a factor-of-1000 mistake that nothing can catch later."

The workflow script asserts `largestVonMises.unit == "Pa"`,
`magnitude.unit == "m"` and `totalForce.unit == "N"` rather than assuming them,
and probe **L5** — a value divided by 1e6 while still labelled `Pa` — is
killed.

## The parse gate

`tests/cli/StructuralWorkflow.cmake` uses CMake's **`string(JSON)`**, a real
parser, with no `ERROR_VARIABLE`: a malformed payload is a fatal error. So
every structured payload in the multi-process workflow is *parsed* before any
field is read.

```text
no tail      no sed      no grep      no head
```

Checked by grep: the only match for those words in the script is the comment
saying there are none. That is the brief's §121 and §174, and it is stronger
than the P16 sibling, which asserts fields with `string(MATCHES)`.

The in-process tests add a second, independent gate:
`StructuralCli_StructuredOutputIsTheWholeOfStdout` requires, for all six
reports, that stdout begins with `{`, ends with `}` and a single newline, and
has balanced braces outside strings — so a prefix or suffix anywhere fails.
That test runs the mesher and the solver, so Netgen's chatter is live during
the capture. See [BACKEND_STDOUT.md](BACKEND_STDOUT.md).

Probe **L4**, which writes `debug: emitting` before the payload, is killed.

## Determinism

`StructuralCli_ARepeatedCommandGivesTheSameBytes` runs each of the six reports
four times and requires byte-identical stdout. The workflow goes further: two
**separate processes** solving the same file produced byte-identical payloads,
which is asserted rather than compared field by field.

Nothing in the payload is a timestamp, a path, an address, a duration or a
revision, so there is nothing to normalise before comparing — which is why the
byte comparison is available at all (§140).

## Prose carries the same numbers

Human output is concise and is never used for numerical qualification.
`StructuralCli_TheProseAndTheJsonCarryTheSameNumbers` extracts the node,
tetrahedron and free-equation counts from the JSON and requires the prose to
contain each — so the two forms cannot drift.

```text
Solved: 81 nodes, 127 tetrahedra, 119 free equations
  strain energy:      0.00084942 J
  max |u|:            2.58946e-07 m
  max von Mises:      2.91218e+06 Pa
  residual:           9.914e-18
  force imbalance:    4.078e-12 N
  moment imbalance:   3.948e-13 N m
  validation:         accepted
```
