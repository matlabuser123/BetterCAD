# P18-MAT-001 — validation, and who owns it

```text
P15 owns the invariants of a canonical material.
P18-MAT answers one question: is the current canonical material sufficient
and valid for THIS thermal analysis mode?
```

## The finding that shaped the milestone

The checklist asks P18 to reject `k = 0`, `k < 0`, NaN and infinity, and the
same for `rho` and `cp` where required. **P15 already does all of it, at the
point of entry**, which means an unusable value cannot be in a document at
all:

```text
createMaterial            refuses it: "the thermal properties are not valid:
                          thermal conductivity must be greater than zero, not 0"
setMaterialThermal /      refuse it, and leave the previous value intact
setMaterialMechanical
isAvailable               treats an out-of-range value as ABSENT, so a
                          completeness query cannot call such a material Ready
requireThermalConductivity / requireTransientConductionProperties
                          validate again at consumption, naming the material and
                          every gap at once
```

P17-MAT-001 found exactly this for `E`, `nu` and `rho` by eleven failing
tests; this milestone knew it going in and wrote the tests against the
guarantee that exists. So the honest claim is not "P18 validates these" -- it
is "**P15 makes them unrepresentable**, and here is the proof, run through
the thermal path".

## The invalid-value table

| Property | Value | Mode | Expected | Actual | PASS |
| --- | --- | --- | --- | --- | --- |
| k | missing | steady | FAIL | `RequiredPropertyUnavailable`; "has no thermal conductivity, which conduction needs" | ✓ |
| k | missing | transient | FAIL | `RequiredPropertyUnavailable`; "... no thermal conductivity, which transient conduction needs" | ✓ |
| k | 0 | both | unrepresentable | `createMaterial` refuses: "not valid"; an edit to 0 refused, old value kept | ✓ |
| k | < 0 | both | unrepresentable | refused at entry | ✓ |
| k | NaN | both | unrepresentable | refused at entry (the case a `<= 0` test alone misses) | ✓ |
| k | +inf, −inf | both | unrepresentable | refused at entry | ✓ |
| rho | missing | steady | **PASS** | resolves; no density diagnostic anywhere | ✓ |
| rho | missing | transient | FAIL | "has no density ..., which transient conduction needs"; report `missingMechanical = {Density}` | ✓ |
| rho | 0, < 0, NaN, ±inf | both | unrepresentable | refused at entry; NaN edit refused, revision unchanged | ✓ |
| cp | missing | steady | **PASS** | resolves | ✓ |
| cp | missing | transient | FAIL | "has no specific heat capacity ..."; report `missingThermal = {SpecificHeatCapacity}` | ✓ |
| cp | 0, < 0, NaN, ±inf | both | unrepresentable | refused at entry; −1 edit refused, revision unchanged | ✓ |
| rho and cp | both missing | steady | **PASS** | resolves; the over-requirement check | ✓ |
| rho and cp | both missing | transient | FAIL | one diagnostic naming both, density first; report lists both, mechanical then thermal; identical over 16 repeats | ✓ |
| k, rho, cp | 1e-9, 1e-6, 1e-6 | transient | PASS | exact values returned | ✓ |
| k, rho, cp | 1e9 / `double::max` / `nextafter(0, 1)` | transient | PASS | exact values returned; no cap, no floor | ✓ |

Tests: `MissingConductivityFailsBothModes`, `DensityIsRequiredOnlyByTransient`,
`SpecificHeatIsRequiredOnlyByTransient`, `SteadyNeedsNeitherDensityNorSpecificHeat`,
`TransientNamesEveryMissingPropertyDeterministically`,
`P15RefusesAnUnusableValueBeforeItCanReachTheDocument`,
`AcceptsAnyFinitePositiveValue`, `MissingAndZeroAreDifferentThings`.

## Missing versus invalid

`Unknown` is empty -- `MaterialProperty::value()` is `std::nullopt`, never
`0` -- and a zero value cannot be stored. The two therefore produce different
things at different layers: an absent property is a `RequiredPropertyUnavailable`
with P15's "has no ..." diagnostic; a zero is a `createMaterial` /
`setMaterialThermal` refusal with "must be greater than zero". No P18 code
conflates them, and `MissingAndZeroAreDifferentThings` pins both.

## Why the resolver has no range check of its own

Because a second copy of the validation would be ADR-028's defect one layer
along: two notions of a usable conductivity that agree today and drift later.
The delegation is live and tested through the reachable half of P15's
contract -- an ABSENT required property -- by asserting that the resolver's
diagnostic is byte-for-byte P15's own (`TheResolverStillAsksP15RatherThanAssumingValidity`).
If an import path or a file ever let an out-of-range value into a document,
the same `require*` call would refuse it.

## Layer ownership, stated

```text
intrinsically malformed value (NaN, <= 0)   P15, at entry. A material cannot
                                            hold one, so P18 never has to decide
                                            whether an UNUSED malformed density
                                            should fail a steady analysis -- the
                                            situation cannot arise
absent property                             P15 stores it as Unknown; P18 decides
                                            per MODE whether its absence matters,
                                            by reading P15's table
```

## Diagnostics

Structured, reusing P15's and P18-DATA-001's:

```text
thermal::MaterialProblem          BodyNotFound | NoMaterialAssigned |
                                  RequiredPropertyUnavailable
materials::CompletenessReport     state, present/missing kinds in enumeration
                                  order, MaterialIssue list with semantic
                                  property identity
Result<T> / Error                 the message, for logs; the code and the enum
                                  for machines
```

No free text is the only machine-readable error: a caller that needs the
category asks `thermalMaterialProblem`, one that needs the property list asks
`thermalMaterialCompleteness`, and one that needs to fail asks
`resolveThermalMaterial`.
