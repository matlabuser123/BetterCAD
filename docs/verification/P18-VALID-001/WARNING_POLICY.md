# P18-VALID-001 — severity and warning policy

```text
SEVERITIES:  Error | Warning | Info          — a FUNCTION OF THE CODE (severityOf), carried on every finding, never a field a caller or a viewer chooses
OVERALL:     PASS | PASS_WITH_WARNINGS | FAIL — derived from the ordered checks and findings every time it is asked; not stored, not settable
```

## The semantics, frozen

| Severity | Meaning | Effect on acceptance |
| --- | --- | --- |
| **Error** | the analysis result cannot be accepted | `FAIL` |
| **Warning** | the result may be accepted and the user should be told | `PASS_WITH_WARNINGS` |
| **Info** | context only | none |

| Condition | Severity | Acceptance effect | Reason |
| --- | --- | --- | --- |
| a mesh element past the radius-ratio floor or the aspect-ratio ceiling | **Error** `element_below_accuracy_floor` | FAIL | the recovered gradient is not guaranteed to 1e-9 relative |
| a mesh element outside the qualified P16 envelope (any of the four metrics) | **Warning** `element_outside_qualified_envelope` | PASS_WITH_WARNINGS | accurate, but worse than every mesh BetterCAD has qualified; results near it carry larger discretisation error |
| a mesh-control quality policy that is self-contradictory | **Warning** `mesh_quality_policy_unusable` | PASS_WITH_WARNINGS | P16 still measured every metric; refusing a sound solve over an error in the user's reporting preferences would be a false rejection |
| a mesh with an inverted, degenerate, repeated-handle or non-finite element | **Error** | FAIL | it is not a tetrahedron mesh |
| stale geometry / mesh / material / load mapping / condition mapping / assembly / constraint map / solution / recovered fields / balance | **Error** (`geometry_ineligible`, `mesh_stale`, `source_stale`, `source_mismatch`, …) | FAIL | the result describes another model |
| missing material, unresolved load, unresolved or conflicting condition | **Error** | FAIL | there is no model to solve |
| no temperature datum / unanchored region (steady), no capacity (transient) | **Error** | FAIL | the problem has no unique solution |
| a failed residual or pivot gate; a failed heat balance; a non-finite number anywhere | **Error** | FAIL | the numbers are not a solution |
| no load configured; no boundary condition configured | Info | none | a valid model may have neither |
| a transient with no datum (insulated), well posed by its capacity | Info | none | says why a body with no condition is accepted |
| a prescribed temperature that overrode the initial temperature at t = 0 | Info | none | the documented policy: prescription dominates |

* **A warning never masks a fatal condition.** Each fatal condition above is an Error AT THE SOURCE: `severityOf` returns it, the report contains it, and no layer converts it.
  `SeverityIsAFunctionOfTheCodeAndNothingElse` enumerates every code and asserts EXACTLY two warnings and four information codes; the compile-fail group
  (`A_FINDING_HAS_NO_DISPLAY_SEVERITY`, `A_FINDING_HAS_NO_OVERRIDE`, `THERE_IS_NO_SEVERITY_SETTER`) shows there is no field in which a viewer could downgrade one, and
  `requireWellFormed` — run on every report the mapped tests produce — asserts `finding.severity == severityOf(finding.code)` and `finding.rule == ruleOf(finding.code)`.
* **A warning-only analysis is accepted.** `AWarningOnlyAnalysisIsAcceptedWithWarnings` runs a 0.3 mm plate through P16, P18's whole chain and the validator: three warnings
  (radius ratio 1.08e-04, aspect ratio 133, maximum dihedral 3.129 rad), zero errors, `PASS_WITH_WARNINGS`, `accepted()` true, every other rule PASS.
* **A skipped rule is never a pass.** It counts against acceptance (`ASkippedRuleIsNeverAPass`: an otherwise all-pass report with one skipped rule is `FAIL` with zero findings).
  A rule that does not apply to the mode (`TV_TRANSIENT_SETUP` for a steady analysis) is `NOT_APPLICABLE` and counts neither way.

## Determinism and boundedness

* **Deterministic order.** Findings are ordered by rule (the enumeration order), then severity (errors first), then code, then element, then step. Never by message text, never by a hash
  (`ReportOrderingAndContentsAreDeterministic`, `PrimaryFailureReasonDoesNotDependOnTheOrderOfTheDocument`: with a missing density, an unresolved load and a conflicting condition at once,
  the first reported cause is `TV_MATERIAL`, then `TV_LOADS`, then `TV_BCS`, always).
* **No warning spam.** Fifty thousand elements past one bound are ONE finding per metric: it carries the worst value, its element, the bound, and `count` — how many elements crossed
  it (`ReportsTheNumberOfElementsPastABoundAndNamesTheWorst`: two flattened tetrahedra, one finding, `count = 2`). EVERY element was judged: the worst value is the extreme of
  P16's summary over all of them, and the count is taken over P16's per-element records. Bounding the report never skips an element.
* **Stable codes.** The 50 codes have stable snake-case names, a rule and a severity, listed in `thermalValidationPolicyText()` and hashed into the policy fingerprint.
