# P13-CONF-001 — Assembly Configurations / Suppression

```text
STATUS:          PASS
BASELINE:        94e6bef (P13-CONF-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           which components and mates are in force, per configuration,
                 and what switching means. No stable references, no
                 regeneration pipeline, no commands milestone.
IMPLEMENTATION:  suppression overrides on the existing Configuration
                 (ADR-007), effective readers in assembly, and two reads
                 changed in the solver. No second configuration system.
TESTS:           25 new Catch2 cases in 1 file (776 lines)
EVIDENCE:        this directory
```

## Scope

A configuration says which build of one assembly this is. This milestone adds
component and mate suppression to that, and defines what switching between
builds means.

What it does not touch: stable assembly references and the regeneration
pipeline, which are `P13-STREF-001` and `P13-REGEN-001`.

## Configuration model

The whole of it, in one line:

```text
base state -> the active configuration's override -> what is in force
```

An object's own `suppressed` flag is its **base** state, and no configuration
edits it. What the solver sees is that base with the active configuration's
override applied on top.

This is not a new idea in this codebase. It is exactly what `P12-PARAM-002`
does for parameter values, and it is reused rather than reinvented, because
the property this milestone most needs is a consequence of it — see
*Configuration switching* below.

```cpp
using ComponentSuppression = std::map<ComponentId, bool>;
using MateSuppression      = std::map<MateId, bool>;
```

**An override is a value, not a toggle.** A map to `bool` rather than a set of
suppressed objects, so that a component whose base state is *suppressed* can
be turned back **on** by a configuration. A set could only ever turn things
off. Parameters already took the general form; this keeps one rule for both
kinds of overridable state, and it is measured — a component suppressed at the
base and unsuppressed by a configuration is one of the cases below.

## Configuration identity

**Assembly configurations are the existing `Configuration`, and there is no
`AssemblyConfigurationId`.** This is a deliberate deviation from the
milestone's checklist, decided under `CLAUDE.md`'s rule for architecturally
significant choices and recorded in full as
[ADR-007](../../architecture/decisions/ADR-007-one-configuration-system.md).

The reasoning, in short:

| | |
| --- | --- |
| **Constitutional** | `CLAUDE.md` rejects "a second parameter system or document model". Two configuration tables means two active states, two name spaces, two persistence paths, two undo stories, and two answers to "which build is this?" |
| **For the user** | A family of parts varies by dimension *and* by content together: the `Large` build is wider **and** has the bracket. Under two systems the engineer creates `Large` twice and keeps them in step by hand, and a document whose parameter configuration is `Large` while its assembly configuration is `Small` is a silent wrong answer, not an error |
| **Duplication** | Exactly-one-active, base-is-none, name uniqueness, ascending-ID order, forget-on-delete, persistence by name, and the commands are already built and qualified |
| **It gains nothing** | `ConfigurationId` is already a strong typed ID over its own tag. A second tag would make assembly configurations *distinguishable* from parameter configurations, which is precisely what is not wanted — they are the same configuration |

**The capability the checklist asks for is delivered in full**: assembly
configurations have strong typed identity, carried by `ConfigurationId`. What
is not delivered is a second identity type for it, because the thing it would
identify is not a second thing.

ADR-007 also records the two candidates rejected: a separate table, and
suppression as a flag flip rather than a value.

## Base / default behaviour

The base configuration is the **absence** of overrides. Nothing has to be
created, named or activated for an assembly to behave as it did before this
milestone.

| Expected | Measured |
| --- | --- |
| an assembly with no configurations | 12 unknowns, 6 equations, 6 DOF — unchanged |
| a configuration that overrides nothing | identical status, counts and **bit-identical** transforms to the base |
| a file whose configurations change only parameters | written with no `components` or `mates` keys at all |

The last row is the compatibility claim in its strongest form: a
parameter-only document's file is byte-for-byte what it was before this
milestone, because the suppression keys are written only when there is
suppression to write.

## Component suppression

```text
active component     -> six unknowns, a transform in the result
suppressed component -> no unknowns, no transform, and its mates go with it
```

Hand-derived on a three-component assembly (a grounded base, an arm and a
bracket, each held by a `Coincident`):

| | Unknowns | Equations | DOF |
| --- | --- | --- | --- |
| base configuration | 12 | 6 | 6 |
| bracket suppressed | **6** | **3** | **3** |

Six unknowns go with the component, and three equations go with the mate that
held it — which is the rule below, not an accident.

**Suppression deletes nothing**, measured: the component keeps its ID, its
part reference, its placement intent and its own base flag; the mate that
holds it keeps its targets and its own flag. Both come back unchanged when the
configuration does.

## Mate suppression

```text
active mate     -> contributes its equations
suppressed mate -> contributes none; its components stay
```

| | Unknowns | Equations | DOF |
| --- | --- | --- | --- |
| base configuration | 12 | 6 | 6 |
| one `Coincident` suppressed | 12 | **3** | **9** |

The component whose mate was suppressed keeps its transform and is simply
free — measured at its placement intent, 80 mm up, because nothing now
constrains it.

Suppressing the **grounding** `Fixed` mate is the same mechanism and gives the
assembly its six rigid-body modes back: 18 unknowns, 6 equations, 12 DOF.

### A mate on a suppressed component is *inactive*, not *unresolved*

The rule suppression makes newly possible, and the one most likely to be got
quietly wrong, because from inside the solver the two look alike and they mean
entirely different things to the engineer.

A mate whose target sits on a suppressed component **resolves perfectly
well** — the component is in the document with its ID, its geometry and its
placement intact. But constraining a part that is not in this build would
describe a relationship that does not exist in it. So such a mate is
**inactive**, a state of this configuration, and not **unresolved**, which is
a fault in the model.

Suppressing a component therefore takes its mates out with it **without
touching them**. Measured: with the bracket suppressed,

```text
isMateActive(holdsBracket)     == false
isMateSuppressed(holdsBracket) == false     <- its own flag never moved
```

and it returns, unchanged, when the component does.

A genuinely missing component is still an error. `P13-SOLVE-001`'s contract is
untouched: removing a component that a mate names fails the solve with
`NotFound` rather than becoming a status.

## Solver participation

Suppressed components and mates are removed from problem construction
**before** the solve, not solved and discarded afterwards. The solver asks for
the active sets and never learns that configurations exist:

```cpp
for (const ComponentId id : activeComponents(document)) { ... }
for (const MateId id : activeMates(document)) { ... }
```

That is the principle `P12-PARAM-002` states for parameters — "a configuration
reaches everything without any of them knowing that configurations exist" —
applied to a second kind of state. Two reads changed in `SolverSystem.cpp`;
nothing else in the solver moved.

Measured, and each is a separate assertion rather than an inference from the
totals:

| Claim | Measured |
| --- | --- |
| a suppressed component's DOFs are absent | unknowns fall 12 → 6 |
| a suppressed component has no transform | `transforms` does not contain it |
| a suppressed mate's rows are absent | equations fall 6 → 3 |
| the active assembly still solves correctly | status, DOF and positions all as derived |

**A gap this closed.** Before this milestone the solver skipped suppressed
*mates* but not suppressed *components* — `ComponentDefinition::suppressed`
existed and was ignored, so a suppressed component still contributed six
unknowns and inflated the reported degrees of freedom. That was unimplemented
scope rather than a defect (`P13-SOLVE-001` was never asked about
suppression), but it is a behaviour change to an already-qualified solver and
is recorded here as one.

## Configuration switching

```text
Config A -> solve -> state A
Config B -> solve -> state B
Config A -> solve -> state A, exactly
```

Ten round trips, and every transform **bit-identical** to the first solve on
every pass — not close, identical.

That is inherited rather than engineered. Because switching never edits a base
state, there is nothing to accumulate: the second solve of `A` starts from the
same intent the first did and runs the same arithmetic. It is the same reason
`P12-PARAM-002` can say that `Small -> Large -> Small` restores `Small`
exactly, and it is why ADR-007 chose to extend that model instead of building
a new one.

Also measured across five switching cycles: placement intent unchanged, and
both base suppression flags unchanged.

## Dependencies

Suppression is not deletion, so the dependency graph must not move.

| Expected | Measured |
| --- | --- |
| a suppressed component's dependencies | identical to before suppression |
| a suppressed mate's dependencies | identical |
| dependents of a suppressed component | identical |
| missing edges | none, in any configuration |
| after switching back | identical again |

Nothing stale, nothing deleted, nothing duplicated. The full regeneration
behaviour is `P13-REGEN-001` and is not implemented here.

## Unresolved references

Three distinct states, kept distinct:

| Situation | State | Behaviour |
| --- | --- | --- |
| the component is suppressed | **inactive** | the mate contributes nothing; nothing is rebound |
| the component is gone | **unresolved** | the solve fails with `NotFound`, as `P13-SOLVE-001` defines |
| a file names an object that is not there | **refused** | the load fails, naming the ID |

No nearest-geometry binding anywhere, and no stale geometry: the solver's
system is rebuilt on every call, so there is no resolved state from another
configuration for it to reuse.

## Persistence

Written only when there is something to write, and read back in full:

| Expected | Measured |
| --- | --- |
| configuration identities and names | both round-trip |
| which configuration is active | round-trips, by name |
| **the base being active** | round-trips as the base, not as the first configuration found |
| component suppression states | round-trip |
| mate suppression states | round-trip |
| **an override that turns something ON** (`false`) | round-trips as readily as one that turns it off |
| canonical component and mate identities | unchanged |
| solving after reload | **bit-identical** transforms |
| writing what was read | byte-identical file |

Two negative cases:

- **A configuration with no suppression writes no suppression keys**, so a
  parameter-only document's file is exactly what it was before this milestone.
- **A file naming an object that is not there is refused**, not silently
  dropped — tested by editing a saved file to point at ID 987654, which fails
  the load with that ID in the message.

**One ordering change was needed, and it is a change to a qualified path.**
Configurations were read *before* objects, because they only had to follow
parameters. A suppression override names an object, so the reader now runs
after the objects are loaded. Found by the persistence test failing on its
first run — the check that an override names a real object rejected every
override, because at that point no object existed yet. All 42 of
`P12-PARAM-002`'s configuration tests pass against the new order.

## Failure atomicity

| Operation | Expected | Measured |
| --- | --- | --- |
| suppress in an unknown configuration | structured failure | `NotFound` |
| suppress an unknown component or mate | structured failure | `NotFound` |
| suppress an ID of the wrong kind (a mate as a component) | structured failure | `NotFound` |
| switch to an unknown configuration | previous one stays in force | active unchanged, and the document still solves to the same transforms |
| after any of the above | nothing partially changed | the configuration is still empty |

Recovery is measured, not assumed: after the failed switch the document is
solved again and gives the same answer as before it.

**The degenerate build** is not a failure. Suppressing every component gives
0 unknowns, 0 equations, 0 DOF and `FullyConstrained` — an assembly of no
parts is empty, not contradictory.

**A deleted object cannot linger in a configuration.**
`Document::removeObject()` calls `ConfigurationTable::forgetObject()`, the
sibling of the `forgetParameter()` that already ran when a parameter was
deleted. Measured: deleting a suppressed component and its mate leaves the
configuration empty, and a deleted mate stops contributing to the solve
immediately.

## Determinism

| Expected | Measured |
| --- | --- |
| the same configured document solved twice | identical status, counts, iterations, residual |
| the transforms | **bit-identical** |
| the active component and mate sets | identical |
| suppression set in a different order | the same transforms |
| ten switching round trips | **bit-identical** every pass |
| Debug / Release / Debug-shared | the same tests, asserting exact values to 1e-8 m |

Bit-identical is claimed only where earned: nothing is seeded, nothing is
cached, iteration order is ascending ID, and switching moves no base state.

## Independent validation

Every expected value below is derived by hand from the geometry and the
equation counts, never read back from the solver.

| Case | Active components | Active mates | Expected DOF | Status | Result |
| --- | --- | --- | --- | --- | --- |
| base: 3 components, 2 coincidences | 3 | 3 | 6 | UnderConstrained | PASS |
| 1 component suppressed | 2 | 2 | 3 | UnderConstrained | PASS |
| 1 mate suppressed | 3 | 2 | 9 | UnderConstrained | PASS |
| grounding mate suppressed | 3 | 2 | 12 | UnderConstrained | PASS |
| all components suppressed | 0 | 0 | 0 | FullyConstrained | PASS |
| component suppressed at base, unsuppressed by a configuration | 3 | 3 | 6 | UnderConstrained | PASS |
| 6 independent equations, nothing suppressed | 2 | 5 | 0 | **FullyConstrained** | PASS |
| the same, `Perpendicular` suppressed | 2 | 4 | 1 | **UnderConstrained** | PASS |

The last pair is one assembly that is fully constrained in one build and
under-constrained in the other, which is the milestone's claim in its
sharpest form.

**Alternate configurations selecting different mates** is measured too: two
contradictory `Distance` mates, 25 mm and 60 mm, which are `Inconsistent`
together and each correct alone — the arm lands at exactly 25 mm in one
configuration and exactly 60 mm in the other.

## Adversarial review

One finding, a real defect in this milestone's own code, found by its tests
rather than by reading. Nothing weakened.

### Cleared

| Question | Answer |
| --- | --- |
| Can a suppressed component still contribute DOFs? | No — unknowns fall 12 → 6, and it has no transform in the result |
| Can a suppressed mate still contribute residuals? | No — equations fall 6 → 3, and the freed component sits at its intent |
| Can switching mutate canonical placement? | No — placement intent and both base flags measured unchanged after five switching cycles |
| Can switching leave stale solver state? | No — the system is rebuilt on every call, and ten round trips give bit-identical transforms |
| Can a deleted component remain in suppression overrides? | No — `removeObject()` calls `forgetObject()`, measured: the configuration is left empty |
| Can a deleted mate remain in active constraint construction? | No — measured: it leaves the solve immediately and its override goes with it |
| Can active-configuration state become invalid after load? | No — the active one is written by name, and the **base** being active round-trips as the base rather than as the first configuration found |
| Can two configurations share mutable suppression state? | No — measured both ways: setting one leaves the other empty, and clearing one leaves the other's override standing |
| Can save/load change suppression semantics? | No — states round-trip including a `false` override, and the reloaded document solves bit-identically |
| Can component ordering change results? | No — `activeComponents()` preserves the ascending-ID order `components()` already guarantees |
| Can mate ordering change results? | No — measured: suppression set component-first and mate-first gives the same transforms |
| Can switching accumulate numerical drift? | No — ten round trips, bit-identical to the first solve every pass. This is a consequence of never editing a base state, not of a tolerance |
| Can unresolved references use stale geometry from another configuration? | No — a suppressed component's mate is inactive rather than resolved-and-ignored, and nothing is rebound. A genuinely missing component is still `NotFound` |
| Can Debug and Release disagree materially? | The three presets run the same tests asserting exact values to 1e-8 m |
| Were solver tolerances loosened? | No tolerance line was added, changed or removed anywhere in this milestone |

### Finding 1 — configurations were read before the objects they name

The persistence test failed on its first run: a saved document with
suppression would not load.

**Root cause.** `documentFromJson()` read configurations immediately after
parameters, because until now a configuration named only parameters. A
suppression override names a *component* or a *mate*, and the reader's check
that the named object exists therefore ran when no object had been loaded
yet — so it rejected every override, including correct ones.

**Fix:** the configuration block moved after the objects loop. It still
follows the parameters it overrides, which was the original ordering
constraint, and now also follows the objects it suppresses.

**Blast radius, checked rather than assumed.** This is a change to
`P12-PARAM-002`'s qualified load path. All 42 of its configuration tests
across `core`, `features` and `io` pass against the new order, along with
this milestone's 25 — 67 cases and 3584 assertions in total. Nothing between
the two positions depends on configurations being set: objects are restored
from their stored definitions, and nothing evaluates a parameter during load.

**Worth stating plainly:** the check that caught this is the one the fix
exists to serve. A reader that silently dropped unknown overrides would have
loaded the file "successfully" and quietly lost the suppression — the test
would have passed, and the defect would have shipped.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 436/436 | 0 | **1446/1446** | 279.4 s |
| `release` | 436/436 | 0 | **1446/1446** | 283.9 s |
| `debug-shared` | 436/436 | 0 | **1446/1446** | 322.0 s |

Then the milestone's related tests, five times over until failure — 946
tests selected by the configuration, suppression, joint, solver, mate,
reference, component, placement, datum, object, persistence, parameter,
sketch, CLI and architecture names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **946/946 ×5** | 948.3 s |
| `debug` | **946/946 ×5** | 1001.5 s |

1446 = the 1421 of `P13-MATE-002` plus this milestone's 25. All fifteen stage
exit codes are 0.

**Qualified tree.** The harness records a git tree ID per source directory
before building. Recomputed from the working tree after the run, all eight are
identical, so the tree that was qualified is the tree that is committed:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           fbd3a31b14c5c5fa58aaea23457c7e94f2c6cb78
src               9007a77647d42195943ecaf7fff0dd9ad2617868
tests             25b18f7d521f5968945d5a45b4bfdf96fdaa36f2
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`ninja: warning: premature end of file; recovering` heads each build log. It
is ninja's own `.ninja_log`, damaged when runs were killed during
`P13-SOLVE-001`, and it makes ninja rebuild more rather than less — all 436
targets were built in every preset regardless. It is not a compiler warning,
and none appears in any of the three build logs.

**Test discovery, checked by name.** A new test file that compiles but is
never registered would leave the suite green and the milestone unverified.
Each of the 25 new cases was looked for individually in the qualification's
own ctest log by its own name; all 25 are there, and none is reported other
than `Passed`.

## Known limitations

- **Suppression is per object, not per branch.** Suppressing a component
  takes its mates out with it, but nothing propagates further: a component
  positioned only by a mate to a suppressed component becomes free rather
  than being suppressed in turn. That is the honest behaviour — it is still
  in the build — but a caller wanting a whole sub-assembly gone must suppress
  its parts.
- **An over-constrained result still carries no transforms**, inherited from
  `P13-SOLVE-001`. A configuration that leaves an assembly over-constrained
  gives a diagnostic and no positions.
- **A component cannot select a different part configuration** than the
  document's. `TODO.md` records this as an accepted P13 constraint and it is
  unchanged here.
- **Nothing consumes the solved transforms yet**, unchanged from
  `P13-SOLVE-001`: they are returned, not applied, and no regeneration calls
  the solver. Configuration switching therefore changes what a *solve*
  returns, and no regeneration pipeline reacts to it — that is
  `P13-REGEN-001`.
- **There are no configuration commands for suppression.** Parameters have
  `CreateConfigurationCommand` and `ModifyConfigurationCommand` with undo;
  suppression is set through `assembly::suppressComponent()` and
  `suppressMate()`, which bump the revision but are not undoable commands.
  Commands and undo are `P13-CMD-001`.

## Result

```text
TASK:            P13-CONF-001 — Assembly configurations / suppression
IMPLEMENTATION:  suppression overrides on the existing Configuration, the
                 effective readers in assembly, and two reads changed in the
                 solver. One new header and source in assembly (193 lines);
                 +60/-1 core Configurations.hpp, +113/-1 its .cpp,
                 +14 Document.hpp, +53 its .cpp, +74/-10 DocumentJson.cpp,
                 +10/-5 SolverSystem.cpp. No second configuration system,
                 no new solver path, no new solver status.
TESTS:           25 new cases in 1 file (776 lines)
VALIDATION:      every active set, DOF count and status derived by hand
                 before measurement; 8 analytical cases including one
                 assembly that is fully constrained in one build and
                 under-constrained in the other
REGRESSION:      1446/1446 on debug, release and debug-shared, each from
                 clean; 946/946 five times over in release and debug;
                 0 compiler warnings in all three builds; all 42 of
                 P12-PARAM-002's configuration tests pass against the
                 changed load order
ADVERSARIAL:     1 finding, a real defect in this milestone's own code,
                 found by its tests and fixed
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: suppression is configuration-dependent and excluded before
the solve rather than after; switching restores a build exactly rather than
approximately; suppression deletes nothing; and a mate on a suppressed
component is inactive rather than unresolved — each measured, and the
switching claim measured bit-identically over ten round trips.

What is **not** claimed: that suppression propagates beyond an object's own
mates; that there are undoable commands for it; or that any regeneration
reacts to a configuration change. Those are `P13-CMD-001` and
`P13-REGEN-001`.

The milestone's one deliberate deviation is recorded in full above and in
ADR-007: there is no `AssemblyConfigurationId`, because assembly
configurations are the configurations this document already had.

## Revision

| When | What |
| --- | --- |
| 23:0x | ADR-007: one configuration system, not two. Candidates compared before implementing |
| 23:5x | implemented; 20 of 21 tests passed on the first run |
| 00:0x | the one failure was a real ordering defect — configurations were read before objects, so the new existence check rejected every override. Read order moved; P12's 42 configuration tests re-run against it |
| 00:1x | adversarial cases added |
| 00:46 | full debug suite 1446/1446; tree frozen and qualified from clean |
| 02:23 | three presets and both repeat stages PASS on the final tree |
