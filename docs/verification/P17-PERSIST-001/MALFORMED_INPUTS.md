# P17-PERSIST-001 — malformed and invalid input

```text
34 rejection cases, 2 deliberate ACCEPTANCE cases.
Every rejection names a JSON path. No rejection returns "file invalid".
```

Counted from the test file rather than quoted: 19 sections in
`RejectsMalformedStructuralIntent`, 8 in
`RejectsInvalidSolverSettingsRatherThanDefaulting`, 4 in
`RejectsDuplicateIdentitiesRatherThanMerging` (3 rejections, 1 acceptance), 4 in
`AMalformedReferenceIsNotTheSameAsAnUnresolvedOne` (3 rejections, 1
acceptance), plus `TheReaderRefusesANodalForceLoadByName`.

## Where each rule lives

The reader does **not** re-implement any engineering rule. It calls the core's,
and it owns only the rules about the *file*:

```text
the core's, called:
    validate(StructuralLoad)              a valid LoadId, a finite value
    validate(StructuralRestraint)         a valid id, a valid selector, a
                                          non-empty mask
    validate(SolverSettings)              a known algorithm, finite positive
                                          thresholds  (reached through
                                          StructuralAnalysis::create)
    validate(FaceSelector)                via faceNameFromJson, shared with
                                          every other object kind
    StructuralAnalysis::create            a valid mesh handle

this file's own, because they are about the FILE and not the model:
    a duplicate LoadId in one analysis
    a duplicate RestraintId in one analysis
    a component listed twice in one mask
    a key belonging to a different load kind
    an unknown enum spelling, by name
```

That split is not a style choice. `validate(StructuralAnalysisDefinition)`
checks the mesh handle and the solver settings and **nothing else** — measured,
fourteen lines, and P17-CMD-001's probe C11 measured the same asymmetry from the
other side — so the per-record validators have to be called explicitly or the
loads and restraints would go unchecked. See [AUDIT.md](AUDIT.md) §3.

The duplicate-key rules are the reader's for the reason P16 gives for the one
it already owns ("the metric `{}` is given a threshold twice"): a repeated id
is a mistake in the file, and choosing one of the two would hide it.

## The matrix

### Structure and the analysis itself

| Case | Expected | Actual diagnostic | Document published? |
| --- | --- | --- | --- |
| truncated JSON | reject | `ParseError` | no |
| an unknown field (`max_iterations`) | reject | names `max_iterations` | no |
| a missing `mesh` | reject | names `mesh` | no |
| `"mesh": 0` (the invalid handle) | reject | `the invalid handle cannot name the mesh to analyse` | no |
| a missing `mode` | reject | names `mode` | no |
| `"mode": "nonlinear"` | reject | `unknown analysis mode 'nonlinear'` | no |
| a missing `solver` section | reject | names `solver` | no |

### Loads

| Case | Expected | Actual diagnostic | Document published? |
| --- | --- | --- | --- |
| `"loads": 3` (not an array) | reject | names `loads` | no |
| `"type": "moment"` | reject | `unknown load type 'moment'` | no |
| `"type": "nodal_force"` | reject | `a nodal force load names a mesh node and cannot appear in a document file (ADR-031)` at `loads[0].type` | no |
| a pressure with no `magnitude` | reject | names `magnitude` | no |
| a pressure **also** carrying `traction` | reject | `a pressure load does not carry this` at `loads[0].traction` | no |
| a gravity load **also** carrying `face` | reject | `a gravity load does not carry this` | no |
| a traction with two components | reject | names `traction` | no |
| a load with no `id` | reject | names `id` | no |
| `"id": 0` | reject | `a structural load must carry a valid LoadId` | no |
| a non-finite value (`-1e999`) | reject | `ParseError`, `number overflow` | no |
| two loads sharing a `LoadId` | reject | `the load id 5 is used twice in this analysis` at `loads[1].id` | no |

### Restraints

| Case | Expected | Actual diagnostic | Document published? |
| --- | --- | --- | --- |
| `"components": []` | reject | `InvalidArgument`, from `validate(StructuralRestraint)` | no |
| `"components": ["rx"]` | reject | `unknown displacement component 'rx'` | no |
| `"components": ["ux","ux"]` | reject | `the component 'ux' is listed twice` | no |
| two restraints sharing a `RestraintId` | reject | `the restraint id 2 is used twice in this analysis` at `restraints[1].id` | no |

### Solver settings

| Case | Expected | Actual diagnostic | Document published? |
| --- | --- | --- | --- |
| `"algorithm": "pardiso"` | reject, **never default** | `unknown solver algorithm 'pardiso'` | no |
| a missing `relative_residual_tolerance` | reject, **never default** | names the key | no |
| a missing `pivot_floor` | reject, **never default** | names the key | no |
| a negative tolerance | reject | `the relative residual tolerance must be finite and positive` | no |
| a zero tolerance | reject | same | no |
| a negative `pivot_floor` | reject | `the pivot floor must be finite and positive` | no |
| a non-finite `pivot_floor` | reject | `ParseError`, `number overflow` | no |
| an unknown solver field (`ordering`) | reject | names `ordering` | no |

### Geometry references

| Case | Expected | Actual diagnostic | Document published? |
| --- | --- | --- | --- |
| a role this build does not know | reject — **malformed** | names `restraints[0].face` | no |
| a `side` role naming no entity | reject — **malformed** | names `restraints[0].face`, from the selector's own rule | no |
| a reference that is not an object | reject — **malformed** | names `restraints[0].face` | no |
| a well-formed reference to a face that does not exist | **ACCEPT** — unresolved, not malformed | — | **yes** |

### Identity

| Case | Expected | Actual |
| --- | --- | --- |
| two analyses sharing an object id | reject | refused by the document's own rule |
| a `LoadId` and a `RestraintId` sharing a numeric value | **ACCEPT** | different strong types scoped to one analysis; ordinary and must not be refused |

## The two acceptance cases are the point

A rejection suite that refused everything unusual would be worse than one that
refused nothing, because it would conflate two different things. Both
distinctions are mandatory in the brief and both are tested:

**Malformed is not unresolved (§17).** A reference whose *representation*
cannot be decoded is corruption and the file is refused. A reference that
decodes perfectly and names a face the current geometry does not produce is
legitimate engineering state — the user changed the model — and the document
loads with the reference preserved exactly. See
[UNRESOLVED_REFERENCES.md](UNRESOLVED_REFERENCES.md).

**Invalid is not incomplete (§113–114).** An analysis with no loads and no
restraints is valid canonical intent that `P17-VALID` refuses to *solve*.
Refusing the *file* would conflate "this record is malformed" with "this model
is not ready", and `StructuralPersistence_AnEmptyLoadAndRestraintSetRoundTrips`
holds that line: the document loads, the collections are empty, and the
validation report is `Rejected` when a solve is requested.

## Load atomicity

`StructuralPersistence_ACorruptFileLeavesTheOpenDocumentAlone`: a saved file is
corrupted in its analysis section and loaded while another document is open.
The load fails, and the open document is then checked **twice** — its canonical
fingerprint is unchanged, and re-saving it produces the original bytes.

Atomicity here is structural rather than defended: `documentFromJson` builds a
fresh `Document` and returns it or an error, so there is no partially populated
document to publish. No P17 code was needed for it.

## Every positive path was checked first

Each rejection suite begins by saving the valid fixture and requiring that it
loads:

```cpp
const std::string good = saveToString(part.document);
REQUIRE(io::documentFromJson(good).has_value());
```

Without that, a rejection could be for the wrong reason — a broken fixture
rather than the field under test — and the whole matrix would be worthless.
