# P18-VIZ-001 — Thermal Visualisation and Inspection

```text
MILESTONE:  P18-VIZ-001                                    the fourteenth milestone of P18 - Thermal Analysis
BASELINE:   HEAD c56aef05 (P18-VALID-001 record), origin == HEAD, working tree clean, 5106 tests
ADDS:       a read-only view of the qualified derived thermal data: contours, vectors, symbols, inspection, extrema, playback, stale state
CHANGES:    nothing qualified (P16, the thermal module, linalg, structural, core and the P17 renderer files are byte-identical to HEAD, compared by blob in FREEZE.md);
            the Viewer gains ONE additive overload
NOT STARTED P18-CMD-001, P18-PERSIST-001, P18-CLI-001, P18-REFMOD-001, P18-QUAL-001
```

## What this milestone is

> **THERMAL VISUALISATION = A READ-ONLY VIEW OF QUALIFIED DERIVED P18 DATA, NOT A NEW THERMAL ANALYSIS AUTHORITY.**

By the end of P18-VALID-001 a thermal analysis produces a chain of derived objects, each carrying the source it was derived from, and nothing shows any of it. A colour is read as a fact, so a
view is dangerous exactly where it is persuasive: when it is stale, when it is on the wrong mesh, when it silently converted a unit, or when it coloured the part of a result that happened to classify. This
milestone adds a headless view layer that reads those objects, draws them, and owns no thermal number.

```text
renderer (layer 70) ──reads──▶ thermal (45): ThermalPostResult · ThermalDofMap · PreparedThermalLoads/Boundary · ThermalValidationReport · isCurrent/staleReasons
ThermalResultView   channels, complete scalar fields, the classifier, extrema (ties → first)
ThermalContour      legend, bands, corner colours, a cache keyed on result · mesh · frame · channel · bands · range · view
ThermalInspection   display state (Current / Stale / SolveFailed …), validation badge, banner, node & element inspection, navigation targets, playback
ThermalOverlay      recovered-flux vectors · applied-q″ arrows · prescribed-T and convection symbols · boundary-set records, each with expected / classified / rendered
Viewer              displayOverlay(ThermalOverlay) — additive; the OCCT presentation converts metres → millimetres once, through the project's one expression
```

## Documents

| Document | What it holds |
| --- | --- |
| [AUTHORITY.md](AUTHORITY.md) | what is read from where, what is never done, how it is enforced |
| [FIELD_MAPPING.md](FIELD_MAPPING.md) | channel → stored member → location → unit; parallel arrays; absent is not zero; the hypot finding |
| [UNITS.md](UNITS.md) | K, K/m, W/m² never rescaled; the SI-metre → OCCT-millimetre boundary, measured against the CAD solid |
| [CONTOURS.md](CONTOURS.md) | nodal and element-wise contours, the legend, ranges, the cache |
| [HEAT_FLUX_VECTORS.md](HEAT_FLUX_VECTORS.md) | the recovered flux, stored as POST made it; display scale; q is not q″ |
| [BOUNDARY_SYMBOLS.md](BOUNDARY_SYMBOLS.md) | q″ positive into the body, symbols, duplicates, named sets, the outward normal |
| [INSPECTION.md](INSPECTION.md) | node and element inspection, sparse identifiers |
| [CLASSIFIER_CARDINALITY.md](CLASSIFIER_CARDINALITY.md) | expected / classified / rendered; zero-classified and partial are never complete |
| [EXTREMA_NAVIGATION.md](EXTREMA_NAVIGATION.md) | extremes with ties to the first entity; navigation targets |
| [TRANSIENT_PLAYBACK.md](TRANSIENT_PLAYBACK.md) | stored times, one state per frame, no interpolation |
| [STALE_STATE.md](STALE_STATE.md) | the display state, same counts and same ids after a remesh, a failed recompute, the validation badge |
| [READ_ONLY.md](READ_ONLY.md) | rendering mutates nothing |
| [DETERMINISM.md](DETERMINISM.md) | repeatability, the `VIZFP` fingerprints, the large-mesh smoke |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | two reviews, thirteen findings with their disposition, fifty answered questions |
| [MUTATIONS.md](MUTATIONS.md) | 75 probes: 71 killed, 4 unreachable by construction |
| [SCOPE.md](SCOPE.md) | what is in, what is out (no Qt panels), the TODO.md ordering note |
| [FREEZE.md](FREEZE.md) | the candidate, the fingerprints, the commit |
| `qualification/` | the run scripts, the post-run analysis, the mutation harness and its logs, the source audit |
| ADR-054 | `docs/architecture/decisions/ADR-054-thermal-visualization-is-a-read-only-view-of-derived-data.md` |

## Decisions

| # | Decision | Why |
| --- | --- | --- |
| D1 | A separate thermal view layer, not a thermal case in the structural one | the structural types are the structural result's; sharing an enumeration couples two modules the architecture keeps apart |
| D2 | The view is a pure function of the core's data; the display state is **recomputed** on every call | a stale flag is a second, hand-kept answer to a question the core answers by comparing sources |
| D3 | Units and locations are read from the core's descriptors; the gradient magnitude is a display norm of the stored vector | the view restates nothing the core owns; POST stores no gradient magnitude |
| D4 | Element quantities are element-wise and unsmoothed; a boundary triangle takes its owner's value | smoothing would invent a variation the element does not have |
| D5 | Every field and glyph reports expected / classified / rendered; zero or partial is never complete | the P16 trap: a classifier that classified nothing is not a complete classifier |
| D6 | q is drawn from the stored vector, never flipped or normalised in storage; q″ is positive into the body (−n_out); they are different overlays | heat flows where q points; an applied flux is an input at a boundary |
| D7 | Playback uses the stored times and one state per frame; there is no interpolation | the last step is a shortened remainder; a blend of two accepted states is not an accepted state |
| D8 | The cache key holds the whole source **and the view** | a key that omits one is a stale colour waiting for the right edit |
| D9 | The one metre → millimetre conversion stays in the OCCT adapter | measured against the CAD solid, not asserted |
| D10 | One ownership gate for the layer and the applications | the discipline is enforced, not reviewed |

## Tests

| Family | Cases | What |
| --- | --- | --- |
| `unit.ThermalViz_*` | **100** | fields, units, contours, vectors, symbols, cardinality, inspection, extrema, stale states, playback, read-only, determinism, large-mesh smoke (offscreen renders tagged `[frame]`) |
| `compile_fail.thermalviz.*` | **51** | quantities do not convert, enumerations are not booleans, no stale flag or smoothing or interpolation or rebinding exists, nothing is writable through a const view |
| `architecture.thermalviz-ownership*` | **12** | the real tree and eleven fixtures, one defect each |
| **new in total** | **163** | 5106 + 163 = 5269 |

Groups (the zero-match guard holds a floor for each): units 11 · contours 18 · vectors 8 · symbols 13 · classifier 9 · extrema 6 · inspection 5 · stale / currentness 13 · transient 6 · read-only 4 ·
large smoke 4 · determinism 3, and the family at 100.

## Evidence in numbers

* Large-mesh smoke: a block at a 2 mm local size (161 nodes, 810 tetrahedra, 12 facets on the loaded face) and a cylinder at 3 mm (900 nodes, 4118 tetrahedra, 572 boundary facets: 288 + 142 + 142 glyphs, all
  `Complete`).
* Unit boundary: a 1 × 2 × 3 mm CAD box silhouettes x 119..200, y 1..143 px; the thermal overlay x 118..201, y 0..144 (within 3 px).
* Transient stored times 0, 0.3, 0.6, 0.8999999999999999, 1 s (not 1.2 s).
* Mutation testing: 75 probes, 71 killed, 4 unreachable by construction ([MUTATIONS.md](MUTATIONS.md)).

## Result

```text
PASS 2026-10-23. Qualified against c56aef05 plus the working tree: 5269/5269 x 3 presets
(debug-ext, release-ext, debug-shared-ext), 2071 selected tests x 5 in two, 0 warnings and
0 errors over 721 objects, every stage exited 0, 6h05m33s, fingerprints at the freeze ==
before == after (include a7494226, src 54c8a486, tests 82afaa3a; apps, examples, cmake and the
build rules equal to HEAD). Second attempt on the frozen tree; the first is recorded below.
```

* **Attempt 1** (13:30, 2026-10-23) was stopped by me after its debug-ext ctest reported 10 failures: `cli.assembly.batch` failed after 34 s and the nine tests that depend on its output
  (`cli.assembly.solve`, `.status`, `.solve.configuration`, `.batch.fails.wrote-nothing` and the five `.exit.*` cases) did not run. The same test passes in 0.22 s alone, passed in the pre-freeze full run, and
  passed in attempt 2; it copies an example from the OneDrive-hosted tree at the start of a loaded run (a plausible cause; it was not isolated). Nothing in the changed files can reach it. The pre-freeze run had a different transient on a CLI
  process test (`cli.new.unicode-path`, 0.20 s, passes alone; seen once before under `-j 8` as well). I did not wait for the other presets of attempt 1, because the harness has no retry and any single
  failed stage fails the run; its logs are in `qualification/attempt1/`. Both are environmental flakes of the CLI process tests on this machine; they are **reported, not hidden**.
* **Attempt 2** (15:19 → 21:25): all 17 stages exit 0. The ctest of each preset: 5269/5269 in 4493 s, 4269 s and 4454 s. The repeat set (`architecture`, `unit.Linalg`, `unit.Structural`,
  `unit.Thermal`, the P17 qualification, the mesh, view, GUI and CLI families, …) selected 2071 tests and ran each five times in release-ext (1345 s) and debug-ext (1545 s) without a failure.
* **Cross-preset equivalence:** the seven `VIZFP` fingerprints of the visualization suite are identical in the three presets (0 differing lines; `qualification/postrun-summary.txt`):

```text
VIZFP steady-block 7cd19fb9128ca590            VIZFP transient-block frame 2 0108fcb4a733f303
VIZFP steady-cylinder bd76108f12f5fbcf         VIZFP transient-block frame 3 dc58cd0f6678d744
VIZFP transient-block frame 0 6e2b1d3bb510ea00 VIZFP transient-block frame 4 fcc4d4e5a09b3bd8
VIZFP transient-block frame 1 f852c55e57e66ad5
```

* **Per-preset binaries:** every preset's own `bettercad_tests` was re-run for `ThermalViz_*` (100 cases, 28 871 assertions, 0 failures), `ThermalValidation_*`, `ThermalBalance_*`, `ThermalPost_*`,
  `ThermalTransient_*`, `StructuralViz_*`, `[structural]` and `[meshing]`: all pass in all three. The shared build carries `libbettercad_renderer.dll` and `libbettercad_thermal.dll` among 12 DLLs.
* **Pre-freeze run:** 5269 tests in debug-ext, 5268 passed and `cli.new.unicode-path` failed once under `-j 8` (passes alone), as above.
* **Source audit** (`qualification/source-audit.txt`): in the eight renderer files, 0 unordered containers, 0 clocks, 0 `const_cast` / `mutable`, 0 unit literals, 0 Celsius, 0 `toModel`, 0 gradient-recovery /
  Fourier / balance / solve / assembly / validation-rule names, 0 raw-id indexes; the only norms are the two documented `hypot` sites.
* **Gate:** `CheckThermalVizOwnership.cmake` scans 43 files, 0 violations; the layering checker 541 files, 0 violations.
## Independent review

A separate reviewer was given the code, nine contract clauses and no conclusions. It found no wrong sign, off-by-one, NaN or tie-break defect, and **four real holes** that the author's own tests had not
reached: a cache key without the view, glyph builders that did not compare the loads they drew with the loads the contributions were made from, a validation badge that did not belong to its analysis, and an arrow
length that could overflow. All four are fixed with tests that fail without the fix (`VIZ31`, `VIZ71`/`VIZ72`, `VIZ42`/`VIZ43`, `VIZ70`). Six lower findings are accepted (one partly) and documented
([ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md)).

## Corrections made after the mutation campaign's first pass

* Three survivors were test gaps and were closed with three tests (a minimum tie, a view bound to one result, the vector glyph ordinal).
* A test whose final paragraph sat inside a `//` comment (literal `\n` characters) was repaired; un-commenting it exposed a wrong assertion, which was corrected (element channel under a shared range).
* The mutation runner could not pass text containing double quotes on a command line; it now reads the text from files, and the four probes affected were repaired and re-run.

## Scope audit

Visualisation and inspection of qualified thermal data implemented here: **YES**. Computing any thermal quantity, changing P16 / POST / BALANCE / VALIDATION / LOAD / BC, persisting view state, thermal
commands, undo/redo, CLI reports, reference models, the P18 qualification: **NO**. Qt result panels: **NO** (none existed for P17-VIZ-001 either; reported, not hidden). [SCOPE.md](SCOPE.md).

## Limitations recorded rather than hidden

* The Qt application has no thermal result UI; the GUI smoke is the headless layer and offscreen renders of the production viewer path.
* `inspectThermal*` default their `resultState` to `Current`; the caller states it (R7).
* A glyph record carries no mesh stamp; it is a per-call read-out and the builders bind it to the prepared object by value (R6).
* An extreme carries no mesh stamp; fields, hence extrema, exist only for a current result (R9).
* Four guards cannot be reached through any public construction (`VIZ12`, `VIZ15`, `VIZ22`, `VIZ52`) and are kept as defensive restatements of the contract.
* Display scale, band count and ranges are view choices; the visual quality of a colour map is not a numerical claim.
* The core's `HeatFlux3::magnitude()` uses a three-argument `std::hypot`, which returns the norm of the OTHER components when one is NaN in this library; untouched (qualified; POST validates finiteness first).
