# P18-VALID-001 — determinism

```text
CLAIM: the same document, mesh and derived state produce the same report, byte for byte, on every run, every preset and every platform the project builds on
HOW:   no clock, no pointer value, no unordered container, no thread, no random number reaches the report; every collection is ordered by a rule written down
```

## What can make a report non-deterministic, and what the design does about it

| Source of variation | Where it could enter | What prevents it |
| --- | --- | --- |
| a timestamp | "when was this validated" | the report has no time field; the only time values are the ANALYSIS's own step times, taken from the settings |
| a pointer or address | `ThermalDerivedState` holds views of the derived objects | the views are inputs, never stored in the report; the report holds the `ThermalResultSource` (ids, counts, revisions, hashes) instead |
| an unordered container | findings per rule, observations per metric, element lookup | ownership gate rule 5 forbids `unordered_`, `std::hash`, `chrono`, `clock`, `rand(` in the validator; architecture fixture `validation-unordered` proves the gate fires |
| iteration order of the document | analyses, materials, loads, conditions | the validator reads them through the predecessors' own resolution, which sorts by id; `PrimaryFailureReasonDoesNotDependOnTheOrderOfTheDocument` inserts the same conditions in opposite order and compares reports |
| the order of findings | many elements, many steps | rules run in the fixed order of `kValidationRules`, and `finishFindings` applies a stable sort on (rule, severity rank, code, element id, step); the sort key contains no address, so equal inputs give an equal order |
| floating-point reduction order | the worst value of a metric, a residual norm | the worst value is the extreme of P16's summary (an order-independent min or max); residual and balance values come from the predecessors' deterministic reductions in fixed index order |
| thread scheduling | none | the validator is single-threaded and starts no thread |
| formatting | `print()` lines used as evidence | messages and evidence lines are built with `std::format`, which is locale-independent, and doubles print in the shortest round-trip form or with an explicit precision; ids and enumerators print as their stable names |
| the policy itself changing | a constant edited between two runs | `thermalValidationPolicyFingerprint()` (`p18-valid-v1:<16 hex>`) hashes the rule order, every code's rule and severity and the mesh thresholds; a report records the one it was produced under and `isCurrent` compares it |

## Tests

| Test | What it shows |
| --- | --- |
| `ReportOrderingAndContentsAreDeterministic` | two validations of the same analysis are equal in every field and in the order of every list |
| `RepeatedReportsOfEveryClassAreStable` | the same for each class of verdict: pass, pass with warnings, and each kind of failure |
| `PrimaryFailureReasonDoesNotDependOnTheOrderOfTheDocument` | the first failure named does not depend on the insertion order of the document's objects |
| `PolicyFingerprintIsDeterministicAndDescribesTheRules` | the policy text and its fingerprint repeat, and change when a rule, a severity or a bound changes |
| the `FP`, `OVERALL`, `REPORT`, `FINDING` and `MATRIX` evidence lines | printed by the real-document tests; the post-run script compares them across `debug-ext`, `release-ext` and `debug-shared-ext` and requires them to be identical |

## Cross-preset comparison

Three presets compile the validator differently (assertions on and off, optimisation on and off, a shared library with exported symbols). The report is a function of the evidence only, so
the printed evidence must agree between them. `postrun-valid.ps1` parses the XML reporter's output of each preset and requires every `FP`, `OVERALL`, `REPORT`, `FINDING` and `MATRIX`
line to be identical across the three; a difference is a failure of the qualification, not a warning. The result is recorded in README.md.

## What is not claimed

* Floating-point values that come from the solve itself (a residual norm) are as deterministic as the solve is; the validator neither adds nor removes variation, and it judges them with
  tolerances that sit orders of magnitude away from rounding noise.
* A report is not byte-stable across a change of the policy; the fingerprint exists to say so.
