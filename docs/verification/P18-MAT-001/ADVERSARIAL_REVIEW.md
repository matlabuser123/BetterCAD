# P18-MAT-001 — adversarial review

```text
SUBJECT:   an attempt to disprove the thermal material resolution before any
           matrix is built from it
QUESTIONS: 32 from the brief, plus 6 of the reviewer's
MUTATIONS: 14 run-time probes on the production tree -- 13 killed, 1 refused
           by the compiler; 2 of the 13 rerun after a test hardening so the
           kill is an assertion failure rather than a process abort
FINDINGS:  2 -- 1 test-method weakness fixed before the freeze, 1 recorded
           audit note
PRODUCTION DEFECTS: 0
GATE-BLOCKING: 0
```

Each answer below was obtained by reading the code or running something.

## Findings

### F1 — two mutants killed by aborting the process, not by a reported failure (TEST METHOD, fixed)

MAT02 (transient resolution ignores rho and cp) and MAT03 (`consumerFor`
maps `Transient` to `ThermalSteady`) both make the resolver return a view
with no capacity for a transient analysis. The tests dereferenced
`capacity()->` without first requiring it engaged, so under the debug
build's `_GLIBCXX_ASSERTIONS` the libstdc++ `optional::operator->` assertion
fired and the test process aborted (exit −1073740791). A kill, but an
uninformative one: no test name, no assertion, no count.

Fixed before the freeze by a `capacityOf()` helper that `REQUIRE`s the
optional engaged and returns the pair by value; 18 call sites. The hardened
suite passes with 1529 assertions (23 more than before), and the two probes
rerun kill cleanly:

```text
MAT02b   31 cases | 18 passed | 13 failed   1442 assertions | 52 failed
MAT03b   31 cases | 17 passed | 14 failed   1440 assertions | 55 failed
```

The production code was never at fault; the lesson recorded is that a test
of a "present exactly when" invariant must assert the presence before
reading through it, or a violation reports as a crash.

### F2 — the fallback-literal audit's two hits (RECORDED)

A search of `src/thermal/` and `include/bettercad/thermal/` for `value_or`,
`abs`, `epsilon`, `clamp`, default-like literals and unordered containers
finds two `value_or(Power{})` in `HeatBalance` (P18-DATA-001): a steady
balance has no stored-energy term and the sum treats "no term" as a zero
contribution. That is arithmetic over an absent TERM of a derived balance,
not a fallback for an absent material PROPERTY, and nothing in
`ThermalMaterial.cpp` has a `value_or` at all. Recorded so the next audit
does not re-investigate it.

## The brief's attacks

| Attack | Expected protection | Observed | Regression test | PASS |
| --- | --- | --- | --- | --- |
| P18 creates a second material database | none exists | no map, library, preset or stored property in `thermal/`; `ThermalMaterial` is five members by `sizeof` | `PreservesIdentityAndRevisionWithoutCopyingProvenance`; source audit in MATERIAL_AUTHORITY.md | ✓ |
| k/rho/cp copied into the canonical analysis definition | the definition's size mirror | no such field; adding one breaks `ThermalData_TheDefinitionHoldsOnlyIntent` | P18-DATA-001's mirror | ✓ |
| a steady analysis wrongly requires rho | the table is read | k-only and k+cp materials resolve for steady; no density word in any steady diagnostic | `SteadyNeedsNeitherDensityNorSpecificHeat`, `DensityIsRequiredOnlyByTransient`, MAT01 | ✓ |
| a steady analysis wrongly requires cp | same | k+rho resolves for steady | `SpecificHeatIsRequiredOnlyByTransient`, MAT01 | ✓ |
| a transient analysis proceeds without rho | `requireTransientConductionProperties` | refused, density named | `DensityIsRequiredOnlyByTransient`, MAT02, MAT05 | ✓ |
| a transient analysis proceeds without cp | same | refused, specific heat named | `SpecificHeatIsRequiredOnlyByTransient`, MAT02, MAT05 | ✓ |
| missing k becomes zero | P15's `Unknown` is empty, never 0; the resolver carries P15's refusal | refused in both modes | `MissingConductivityFailsBothModes`, `MissingAndZeroAreDifferentThings`, MAT04 | ✓ |
| invalid k repaired with `abs()` | unrepresentable: P15 refuses a negative k at entry | `createMaterial` and `setMaterialThermal` refuse; old value kept | `P15RefusesAnUnusableValueBeforeItCanReachTheDocument` | ✓ |
| invalid rho/cp replaced by epsilon | same, at entry | refused | same | ✓ |
| NaN passes because `k <= 0` is false for NaN | P15 checks `isFinite` first | NaN refused for k, rho, cp | same | ✓ |
| infinity passes positivity | `isFinite` | ±inf refused | same | ✓ |
| k/rho/cp fields swapped | distinct types; distinct test values | a swap does not compile (MAT09); 42.75 / 7853.25 / 486.5 land in their own fields | `ResolveTransientProperties`, MAT09 | ✓ |
| W/(mm K) mistaken for W/(m K) | no conversion in P18 | 45 stays 45 | `UnitsAreCanonicalAndEquivalentInputsAgree`, MAT08 | ✓ |
| g/cm³ mistaken for kg/m³ | same | 7800 stays 7800; `7.8_g_per_cm3` equals it at the literal | same, MAT07 | ✓ |
| kJ/(kg K) mistaken for J/(kg K) | same | 500 stays 500; `0.5_kJ_per_kg_K` equals it at the literal | same, MAT06 | ✓ |
| MaterialId alone establishes currentness after an edit | the source carries the revision | same id, revision 1 → 2, result stale | `TheSameIdWithAChangedConductivityIsADifferentInput`, `AConductivityEditLeavesTheMeshCurrent...`, MAT10 | ✓ |
| identical values keep an old result current after a new source generation | identity, not values | a clone with identical numbers reassigned → stale | `ReassignmentStalesTheResult...` "numerically identical", `ProvenanceDistinguishesIdAndRevision` | ✓ |
| a material edit stales the P16 mesh | the mesh stamp is independent of the material | `Mesher::currency` Current, stamp and NodeIds unchanged after k, rho, cp, E edits | `AConductivityEditLeavesTheMeshCurrent...`, `DensityAndSpecificHeatEditsStaleBothModes` | ✓ |
| a material edit regenerates the mesh | nothing calls `generate` | same stamp, same NodeIds | same | ✓ |
| a failed P15 edit stales a current result | P15 edits are atomic | cp = −1 and rho = NaN refused; material and document revisions unchanged; result `SolvedCurrent` | `AFailedMaterialEditChangesNothing` | ✓ |
| reassignment keeps the old result current | `source.material` changes | stale, reason material; B's values resolve | `ReassignmentStalesTheResult...` | ✓ |
| material deletion lets cached values keep solving | there is no cache | `currentResultSource` fails; `InputsUnavailable`; `NoMaterialAssigned` | `RemovalStalesTheResultAndNothingCachedSurvives`, `RefusesWhenTheAssignedMaterialIsGone` | ✓ |
| a cached view used after its provenance is stale | no cache; a held view carries its revision and the source will mismatch | — | by construction; MATERIAL_AUTHORITY.md | ✓ |
| k(T) silently sampled at an arbitrary temperature | P15 stores constants only; no law exists | nothing to sample | P15_INTEGRATION.md | ✓ |
| anisotropic conductivity reduced to a scalar | not representable in P15 | nothing to reduce | P15_INTEGRATION.md | ✓ |
| a thermal density beside P15's | `ThermalCapacity::density` is P15's density from `requireTransientConductionProperties` | no `thermalDensity` anywhere | source audit; `ResolveTransientProperties` | ✓ |
| a P15 schema extension breaks old files | no extension was made | — | n/a | ✓ |
| adding thermal properties breaks P17 structural resolution | nothing in P15 or P17 changed | the unfiltered three-preset runs carry every P15 and P17 test | FREEZE.md | ✓ |
| diagnostics depend on unordered iteration | P15's order is semantic; the resolver adds none | identical message and report over 16 repeats; density before specific heat | `TransientNamesEveryMissingPropertyDeterministically` | ✓ |
| GUI/CLI later implement a different required-property policy | one place: `requiredProperties(consumerFor(mode))`; `thermalMaterialCompleteness`, `thermalCapability` expose it | — | `TheModeChoosesP15sConsumerAndNothingElseDecidesIt`, MAT03, MAT13 | ✓ |
| zero targeted tests still report PASS | `ZeroMatchGuard` | `unit\.ThermalMaterial` 31 ≥ 15 | the guard | ✓ |

## Mutation probes

Each probe is one textual mutation of a production source, a rebuild, one run
of `bettercad_tests "[thermal][material]"` (plus `[thermal][data]` where the
mutation is in the shared result source), a verdict and a byte-identical
restore. Logs are in the session scratchpad; the summary is in
`qualification/mutation-probes.txt`; the tree was verified free of every
mutation before the pre-freeze run.

| # | Mutation | File | Verdict | Killed by |
| --- | --- | --- | --- | --- |
| MAT01 | steady requires rho and cp (always the transient path) | ThermalMaterial.cpp | **KILLED** 8 cases / 15 assertions | `SteadyNeedsNeither...`, `DensityIsRequiredOnly...`, `SpecificHeatIsRequiredOnly...`, `CapabilityMatrix`, `ResolveSteadyConductivity`, the k-only state test |
| MAT02 | transient ignores rho and cp (always the k-only path) | ThermalMaterial.cpp | **KILLED** by process abort (empty optional) | — see F1 |
| MAT02b | same, hardened tests | ThermalMaterial.cpp | **KILLED** 13 / 52 | `ResolveTransientProperties`, `DensityIsRequiredOnly...`, `CapabilityMatrix`, every transient-mode case |
| MAT03 | `consumerFor(Transient)` returns `ThermalSteady` | ThermalMaterial.cpp | **KILLED** by process abort | — see F1 |
| MAT03b | same, hardened tests | ThermalMaterial.cpp | **KILLED** 14 / 55 | `TheModeChoosesP15sConsumer...` and every transient-mode case |
| MAT04 | missing k becomes a default of 1 W/(m K) | ThermalMaterial.cpp | **KILLED** 4 / 7 | `MissingConductivityFailsBothModes`, `TheResolverStillAsksP15...`, `ReachesEveryMaterialProblem...`, `CapabilityMatrix` |
| MAT05 | missing rho/cp become 7800 / 500 with k = 45 | ThermalMaterial.cpp | **KILLED** 8 / 17 | `DensityIsRequiredOnly...`, `SpecificHeatIsRequiredOnly...`, `TransientNamesEveryMissing...`, `CapabilityMatrix`, the transient state test |
| MAT06 | cp divided by 1000 (a kJ reading) | ThermalMaterial.cpp | **KILLED** 6 / 10 | `ResolveTransientProperties`, `UnitsAreCanonical...`, `AcceptsAnyFinitePositiveValue`, `VolumetricHeatCapacity...` |
| MAT07 | rho divided by 1000 (a g/cm³ reading) | ThermalMaterial.cpp | **KILLED** 6 / 11 | same set |
| MAT08 | k divided by 1000 (a mm reading) in the transient path | ThermalMaterial.cpp | **KILLED** 7 / 27 | same set, plus the k-edit case |
| MAT09 | rho and cp swapped | ThermalMaterial.cpp | **REFUSED BY THE COMPILER**: `Density` is not a `SpecificHeatCapacity` | the type system; the distinct values would catch a swap that compiled |
| MAT10 | result source carries `materialRevision = 0` | ThermalResult.cpp | **KILLED** 5 / 20 (67 cases run: material + data suites) | `AConductivityEditLeavesTheMeshCurrent...`, `DensityAndSpecificHeatEdits...`, `AStructuralEdit...`, P18-DATA-001's material-edit and source-population cases |
| MAT11 | result source resolves for `Steady` whatever the analysis's mode | ThermalResult.cpp | **KILLED** 1 / 1 | `ATransientAnalysisOnAConductivityOnlyMaterialIsNotReady` |
| MAT12 | the body-exists check removed | ThermalMaterial.cpp | **KILLED** 2 / 3 | `RefusesWhenTheBodyIsGone`, `ReachesEveryMaterialProblem...` |
| MAT13 | `thermalCapability.transient` reports the steady answer | ThermalMaterial.cpp | **KILLED** 1 / 3 | `CapabilityMatrix` |
| MAT14 | no material reported as `RequiredPropertyUnavailable` | ThermalMaterial.cpp | **KILLED** 4 / 6 | `RefusesWhenNothingIsAssigned`, `RefusesWhenTheAssignedMaterialIsGone`, `RemovalStales...`, `ReachesEveryMaterialProblem...` |

```text
probes with a verdict   14 (+ 2 reruns)
killed                  13, every one of them, 2 by abort then cleanly on rerun
refused by the compiler  1   MAT09
survived                 0
```

Not probed, and why: `k = abs(k)`, an `isfinite` omission and an epsilon
replacement all live in P15, where the values are refused at entry; P18 has
no such check to mutate, and the values cannot reach it. "Material edit
also increments the mesh revision" would be a mutation of P16. "Copy k/rho/cp
into the definition" is killed by P18-DATA-001's size mirror at compile time.
"Cache by MaterialId without revision" has no cache to mutate.

## The reviewer's own six

**1. Does the transient path resolve k through a DIFFERENT P15 function than
the steady path, and could the two disagree?** Yes, deliberately:
`requireTransientConductionProperties` for transient, `requireThermalConductivity`
for steady, so that a transient failure names every gap in one diagnostic.
Both read `ThermalProperties::thermalConductivity` with the same test
(present, finite, > 0); `ResolveTransientProperties` asserts the two paths
return the same k for the same material.

**2. Could the result source and the resolver disagree about the material?**
No: `currentResultSource` takes `id()` and `revision()` from the
`ThermalMaterial` it resolved, not from a second lookup. There is one read.

**3. Is `InputsUnavailable` the right state for a transient analysis on a
k-only material, rather than a new state?** Yes. P18-DATA-001 defined that
state as "a definition exists and nothing can yet be computed from it", and
`thermalMaterialProblem` names the reason as `RequiredPropertyUnavailable`
with P15's list. A new enumerator would be a second place to say the same
thing.

**4. Does the resolver accept a material the input boundary would refuse, or
refuse one it would accept?** The boundary (`requireThermalModel`) demands a
usable k and nothing more; the resolver demands the same for steady and more
for transient. So every material the resolver accepts for any mode passes
the boundary, and a k-only material passes the boundary and fails the
transient resolver -- which is the ordering ADR-051 and P18-ARCH-001 chose
(the boundary proves the input; the staged validation judges it).

**5. Why is the volumetric heat capacity's type an alias in `thermal/` and
not a core quantity?** Because nothing stores one: it is derived from rho and
cp on request, so a unit, a literal and a catalog entry would be machinery
for a value no document holds. The alias names the product's own dimension,
which already exists; `VolumetricHeatCapacityIsDerivedNotStored` asserts it
equals `Energy / (Volume * Temperature)`.

**6. Did any test accidentally depend on the material being assigned per
document rather than per body?** Every test assigns the document's material
and resolves against the one body, which is P15's model today (ADR-026,
one solid per ADR-049). The resolver takes the body for the same reason
P17's does: a per-body assignment changes the answer and not the call.

## Verdict

```text
adversarial review     PASS -- 0 production defects; 1 test-method weakness
                       fixed before the freeze; 1 audit note recorded; every
                       attack in the brief's list answered by a test, by the
                       compiler, or by P15's entry validation named in the
                       table
```
