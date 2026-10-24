# P18-PERSIST-001 — Thermal Analysis Persistence

```text
MILESTONE:  P18-PERSIST-001                                    the sixteenth milestone of P18 - Thermal Analysis
BASELINE:   HEAD 975efa6 (P18-CMD-001 record), origin == HEAD, working tree clean, 5432 tests
ADDS:       the persisted form of a thermal analysis's canonical intent, as one more object type in the existing document format
CHANGES:    nothing qualified except one test that pinned the refusal this milestone removes; 511 of 515 src/include/apps files byte-identical to HEAD, the other four
            are the milestone's own (compared by blob in FREEZE.md)
RESULT:     5504/5504 x 3 presets, 2254 x 2 in two, 0 warnings over 727 objects, 17 stages exit 0, 6h44m47s, first attempt
NOT STARTED P18-CLI-001, P18-REFMOD-001, P18-QUAL-001
```

## What this milestone is

> **THE SAVE FILE IS CANONICAL THERMAL INTENT. IT IS NOT A THERMAL SOLVER OR RESULT CACHE.**

By the end of P18-CMD-001 a thermal analysis can be defined, edited with undo, meshed, solved, recovered, balanced, validated and drawn — and could not be saved: the document writer refused it by name. The audit
found the format already built for this (P16 and P17 each added one object type to it) and that every architectural question in the brief already had an answer in the tree, so **P18 persistence is one more object
type in the existing `.bcad` document**: no second file, no version bump, no migration, no new validation framework, no new reference encoding, no new determinism mechanism.

```text
thermal-analysis   mesh · mode · loads[] · boundary_conditions[] · initial_temperature · solver · transient       (seven members for seven fields)
                   fixed SI named by the key: flux W/m^2 · generation W/m^3 · temperature, ambient K · coefficient W/(m^2 K) · time_step, end_time s
                   a FaceName by the canonical encoding, an unresolved one kept exactly; unknown keys rejected; order preserved; ids never regenerated
```

## The result in one table

| Gate | Result |
| --- | --- |
| three presets, the unfiltered ctest | **5504/5504** in `debug-ext`, `release-ext` and `debug-shared-ext` (the previous milestone had 5432; this adds 48 + 24) |
| the repeat set | 2254 tests x 2 presets, all pass |
| warnings | 0 over 727 objects per preset, `-Werror` |
| no-op rebuilds | 1 edge each (nothing rebuilt) |
| fingerprints | the eight-path fingerprint at the freeze == before the run == after it |
| stages | 17, all exit 0, 6h44m47s — the first attempt |
| unit cases | **48** `ThermalPersistence_*`, 4363 assertions |
| architecture gate | **24** tests: the real tree, a valid fixture and 22 fixtures that each break exactly one rule |
| zero-match guard | 11 exact-count filters |
| pre/post-solve byte identity | identical, **steady** (8 states, 4230 B) and **transient** (a 3.9 MB result, 5251 B) |
| golden bytes | the hand-written file reproduces itself character for character; two FNV constants pinned in the test, identical in all three presets |
| save → load → re-solve | temperatures, gradient and heat-flux fields **bit-identical**; heat-balance terms within 1e−12; verdicts equal — steady, transient, and a shortened last step |
| mutation probes | **41** on the reader/writer + **16** on the gate, all killed; 2 first-run survivors each produced a sharper assertion or a fixture |
| independent review | 4 medium, 6 low, 8 gate holes, a list of vacuous tests — each with a disposition; 0 high |
| qualified modules changed | **0** (one test renamed and rewritten: the refusal it pinned is gone by design) |

## Documents

| Document | What it holds |
| --- | --- |
| [AUDIT.md](AUDIT.md) | the architecture audit; the capability table; the questions the project had already answered; the brief's items that do not exist in this data model |
| [SCHEMA.md](SCHEMA.md) | every persisted field, with unit and validator; seven schema decisions; the version decision |
| [AUTHORITY.md](AUTHORITY.md) | persisted / not persisted; why the exclusion is structural; reachability measured; P15 stays the material authority |
| [BYTE_IDENTITY.md](BYTE_IDENTITY.md) | the hard gate: steady and a large transient; golden bytes |
| [ROUND_TRIP.md](ROUND_TRIP.md) | field-by-field round trip; destroy → load → remesh → re-solve with the comparison table |
| [UNRESOLVED_REFERENCES.md](UNRESOLVED_REFERENCES.md) | preserved, not rebound — and why a rebind cannot be written here; the invalid feature handle |
| [MALFORMED_INPUTS.md](MALFORMED_INPUTS.md) | every refusal and its path; duplicate identities; atomic load; the identifier cap |
| [BACKWARD_COMPATIBILITY.md](BACKWARD_COMPATIBILITY.md) | why the version does not move; the real corpus; what was not tested because it does not exist |
| [DETERMINISM.md](DETERMINISM.md) | the mechanism, the tests, the locale test, the golden constants |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | the independent review's findings with dispositions; observations; the author's questions answered |
| [MUTATIONS.md](MUTATIONS.md) | 41 + 16 probes; the brief's mutations that could not be written; the two survivors |
| [SCOPE.md](SCOPE.md) | what is in, what is not, the one qualified test changed |
| [FREEZE.md](FREEZE.md) | the candidate, the fingerprints, the commit |
| `qualification/` | the run scripts, the post-run analysis, the mutation harnesses and their logs, the source audit, the reachability measurement |
| ADR-056 | `docs/architecture/decisions/ADR-056-thermal-analysis-persistence-is-one-more-object-type-and-the-file-holds-intent-only.md` |

## Decisions

| # | Decision | Why |
| --- | --- | --- |
| D1 | one object type in the existing format; no version bump | adding a type is additive and fails safely in both directions |
| D2 | the writer's only argument is the analysis object; the reader gets no document, mesher or regenerator | no expression can depend on a solve; nothing to rebind a reference to |
| D3 | fixed SI named by the key; `.si()` out, `fromSi` in | no conversion factor exists in the file to be wrong |
| D4 | variants by an explicit string, keys a kind may not carry refused by name; enumerations by stable strings | `requireObject` can only allow the union; an integer would change meaning when an enumerator is inserted |
| D5 | the core's validators, called not copied; the file adds only the path, duplicates, per-kind keys, the invalid feature handle and the identifier cap | one rule, one place |
| D6 | solver settings always written, all required | a missing field is a malformed file, not "the default" |
| D7 | unknown keys rejected | a fake result or flag can never become anything: the file is refused naming the key |
| D8 | the writer refuses an identifier above the file cap | it must not emit what the reader refuses (independent review, M1) |
| D9 | golden bytes pinned in the source and by a hand-written file | self-comparison cannot see a changed key order or number format (independent review, M4) |

## Findings for the owner

1. **Repeated JSON keys are last-wins, format-wide** (M2). `{"flux": 1, "flux": -1}` loads as −1 for a thermal load exactly as for every other object type; the parser collapses a repeated key before any reader
   sees it. Pinned by a test, not changed: a strict parser would change how every document loads. Duplicate *identities* are refused.
2. **The data model accepts `FaceName{}`** (O2, from P18-CMD-001). `validate(ThermalLoad / ThermalBoundaryCondition)` checks the selector, not the feature handle. The commands refuse it; **the file now refuses it too**
   (`face.feature: 0` is "a target must name a feature"); a valid handle naming a missing feature stays ordinary, unresolved intent. The data model itself is untouched.
3. **A dangling mesh control loads.** A thermal analysis naming a control that does not exist, or one of another kind, loads (the document checks no dependencies on load, for any type) and is refused later by
   `requireThermalModel` with a diagnostic. Pinned.
4. **`validate(ThermalAnalysisDefinition)` is O(n²)** in the number of loads and conditions (the reader's duplicate check is too). 3000 + 3000 records round-trip in the unit suite; ~10⁵ would be slow. Qualified P18-DATA
   code, not touched.
5. **Not in this data model, so not persisted:** `NamedBoundarySet` (a P16 concept), an explicit output-time schedule (the grid is derived), a material reference inside the analysis (P15 owns the assignment), an
   earlier P18 schema (thermal analyses could not be saved before). The brief's items about them have no subject ([AUDIT.md](AUDIT.md)).
6. **Analysis names are validated identifiers**, so a Unicode or escaped name is refused by the document, identically each time; there is no free text in a thermal section.
7. **A result never attaches to another document by identifier** — a result is bound to the mesh stamp of its mesher and compares the canonical intent by value; with the copy's analysis revision made *equal* to the
   original's the original's result is still stale, for the mesh and the loads. (The first of the two currentness regimes P18-CMD-001 recorded.)
8. **`TODO.md` ordering.** The brief asks for the Future Phases section to be the end of `TODO.md`; in the repository (as at HEAD and at every earlier milestone) three sections follow it. The order was kept.
9. **Environment.** Smart App Control is now off on this machine (the owner's decision during P18-CMD-001). `cli.new.unicode-path` failed once in an unfiltered, detached pre-freeze run and passes alone — the
   console-code-page cause recorded in the project's flake notes; it did not fail in the qualification.
