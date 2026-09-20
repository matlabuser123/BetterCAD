# P13-REGEN-001 — Dependency / Regeneration

```text
STATUS:          PASS
BASELINE:        c73ed92 (P13-REGEN-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           regeneration drives the assembly: what makes it re-solve,
                 what it may write, and what it does when something is
                 broken. No commands or undo, no CLI, no persistence
                 milestone.
IMPLEMENTATION:  a final-pass mechanism in the regenerator (ADR-008), a mate
                 handler, and the assembly solve registered as that pass
TESTS:           22 new Catch2 cases in 1 file (644 lines)
EVIDENCE:        this directory
```

## Scope

Four milestones built a solver, four mechanical joints, configuration control
and stable references, and each carried the same line forward: *nothing
consumes the solved transforms yet — they are returned, not applied, and no
regeneration calls the solver.* This is where that closes.

**The failure this milestone exists to prevent is stale derived state that
looks current.** A transform left over from before a mate changed is worse
than no transform at all: the assembly renders, the positions are plausible,
and they are answers to a question nobody asked any more. So it is never
enough here to assert that a solve happened — every case asserts **which**
transforms are in force afterwards.

Its companion is the opposite failure: re-solving when nothing relevant moved,
which is not wrong but is how a CAD system becomes unusable on a large
assembly. That is why the trigger is observable and asserted in both
directions.

## Regeneration contract

```text
canonical input    component definitions and placements, mate definitions,
                   configurations and their overrides, parameters, and the
                   geometry beneath a component
dirty/affected     per object, by revision, through the dependency graph;
                   plus, for the assembly, the inputs listed under
                   "Solve triggers" below
derived output     component transforms, published beside the bodies and
                   never persisted (ADR-005)
failure state      a mate whose target does not resolve FAILS; anything in
                   force that failed or was blocked means no transforms are
                   published at all
```

What causes an assembly to re-solve, and what does not, is the subject of
*Solve triggers*. What regeneration may write is the subject of *Canonical vs
derived state*, and it is the hard gate.

## The architectural decision

`features::Regenerator`'s unit of work is **one object**: a handler takes an
`ObjectId` and returns that object's body. A sketch fits that — it owns its
own solve. An assembly solve does not: it spans every active component and
mate at once and produces transforms keyed by `ComponentId`, so there is no
object whose handler it is. And `features` is layer 2 while `assembly` is
layer 3, so the regenerator cannot call the solve itself.

**The solve is a document-level final pass**, registered by the assembly
module and run after the object pass.
[ADR-008](../../architecture/decisions/ADR-008-the-assembly-solve-is-a-final-pass.md)
records the decision and the two candidates rejected — a handler on a
designated object, and a store the caller updates with a second call.

```text
parameters   ->  a document-level phase, before the objects
objects      ->  one handler each, in dependency order
assembly     ->  a document-level phase, after the objects
```

The regenerator already had the first. It now has the third, for the same
reason, and the symmetry is the argument: a result that belongs to the
document rather than to any one object, computed at the moment its inputs are
ready. The solve must run after the bodies exist, because a face target
resolves against them.

`features` learns nothing about assemblies. `ComponentId` and
`RigidTransform3D` are `core` types, so it stores what the pass returned
exactly as it stores a body without knowing what an extrude is.

## Component dependencies

Already built by `P13-REF-001`: a component handler resolves the part so that
an unresolvable one fails loudly instead of silently. A component owns no
geometry — the part's body is built once by the part's own features, however
many components place it.

What this milestone adds is that the failure now reaches the assembly:

| Case | Measured |
| --- | --- |
| the part a component places is removed | the component fails, and the assembly publishes **no transforms at all** |
| an unrelated part is edited | the component is **not** rebuilt, and the assembly does not re-solve |

## Mate dependencies

**Mates had no handler at all before this milestone**, which made an
unresolvable target silent during regeneration. A face is named through its
feature's roles, so the dependency graph reports nothing missing when the role
stops existing — and a handler-less mate regenerated as if all were well.

The new handler resolves every target a mate names — all four of them, since a
slider carries a roll reference as well as its axis — and fails if any does
not resolve.

| Case | Measured |
| --- | --- |
| a mate names a face role its feature does not produce | the mate is `Failed`, with an error, and the pass does not succeed |
| the mate is repaired | the next pass succeeds and the transforms come back |
| the mate is **suppressed**, and its target is broken | the pass **succeeds** — inactive is not broken |

The last row is the distinction `P13-CONF-001` drew, now enforced in
regeneration: a mate that is not in this build has nothing to resolve and
nothing to say about whether the model is sound.

## Affected-state propagation

The regenerator's existing machinery does this and was not rebuilt: items
whose revision changed are found, everything downstream of them is marked, and
only the dirty are rebuilt in dependency order.

Measured on a document with two independent parts, editing one:

| Expected | Measured |
| --- | --- |
| the edited part rebuilds | in `regenerated` |
| the other part | **not** in `regenerated`, and `UpToDate` |
| its components and mates | **not** in `regenerated` |
| the assembly | does not re-solve |

## Solve triggers

The assembly solve is **global** — one mate's value moves the whole system,
because that is what a constraint system is. So the honest granularity is
whether the solve runs at all, not which components it recomputes, and the
trigger is what this section is about.

**The trigger is derived from what the solve actually reads**, not from a
revision proxy. That is what makes "it did not re-solve for an unrelated
change" provable rather than hoped for:

```text
the active component set          the active configuration
the active mate set               the resolved placement of every active component
whether any of them was rebuilt, failed or blocked in this pass
```

| Change | Expected | Measured trigger |
| --- | --- | --- |
| nothing | no re-solve | `NotNeeded`, over five consecutive passes |
| a mate's value | re-solve | `ObjectChanged` |
| a component's placement intent | re-solve | `ObjectChanged` |
| the geometry beneath a component | re-solve | `ObjectChanged` |
| a configuration switch | re-solve | `ConfigurationChanged` |
| suppression edited without switching | re-solve | `MatesInForceChanged` |
| an unrelated part added and built | **no re-solve** | `NotNeeded` |

Every one of those also checks where the components ended up, not just that
the trigger fired: the mate-value case lands the arm at 30 mm, the placement
case at 80 mm, the geometry case at 25 mm — each derived by hand from the
change made.

### The case a revision-based trigger would miss

A configuration that overrides a **free** parameter changes **no object's
revision**. The base value is untouched — that is ADR-007's whole design — and
only `effectiveParameterValue()` differs. Nothing downstream looks dirty, so a
trigger built on revisions would not fire, and the assembly would keep
transforms computed from the old value while the document said otherwise.
Exactly the stale-state failure.

Comparing the **resolved** placements catches it, because `placementOf()`
reads the value in force. Measured, in two forms:

| Change | Measured trigger | Arm ends at |
| --- | --- | --- |
| activate a configuration overriding the free parameter that drives a placement | `ConfigurationChanged` | 90 mm |
| edit that override while the configuration is already active | **`PlacementChanged`** | 120 mm |

The second is the one no other signal would have caught: the active
configuration did not change, no object's revision changed, and the assembly
still had to re-solve.

## Canonical vs derived state

The hard gate. Regeneration may write derived transforms and nothing else.

| Expected | Measured |
| --- | --- |
| placement intent after several passes | unchanged — still the 50 mm it was authored with, while the solve puts the component at 0 |
| mate definitions | unchanged, compared in full |
| configuration state | unchanged |
| the `.bcad` file after a solve | **byte-identical** to the file saved before it |
| canonical state after a **failed** regeneration | unchanged |

The first row is the clearest statement of the separation: the document says
the arm sits 50 mm up, the solve puts it at 0, and both are true of different
things — intent and result.

## Suppression

| Case | Measured |
| --- | --- |
| a component suppressed in a configuration | has **no transform at all**, not a stale one from the build it was in |
| the suppressed component's mate | inactive; does not fail the pass even with a broken target |
| a mate suppressed | the component it held is free, and settles at its intent |
| switching between two builds that solve differently | correct transforms every time, over six round trips |

The last is the stale-state failure in its most likely form — two builds whose
answers differ, switched back and forth. The arm is held at 0 in one build and
free at 50 mm in the other, and it is measured at the right one on every pass.

## Unresolved references

```text
unresolved  ->  the mate FAILS, and the assembly publishes nothing
inactive    ->  no resolution is attempted, and the pass succeeds
```

Never stale geometry, never a nearest binding, and never a previous successful
target reused: the solve builds its system fresh on every call, so there is no
prior resolution to fall back to.

Recovery is measured: repairing the mate makes the next pass succeed and
restores the transforms, with canonical state having survived the failure
untouched.

## Cycle detection

The regenerator already detects cycles, fails every member with a diagnostic
naming the cycle, and blocks everything downstream. This milestone measures
that an assembly in a document that also contains a cycle behaves correctly:

| Expected | Measured |
| --- | --- |
| the cycle is reported | `cycles` non-empty, both parameters in `failed` |
| no hang, no recursion | the pass returns |
| the assembly, which is not in the cycle | still solves, transforms correct |

The last row is the one worth having: a cycle somewhere in the document must
not take down an assembly that does not depend on it.

## Regeneration order

Measured from the pass's own `regenerated` list, by position:

```text
sketch  <  part  <  components  <  mates  <  the assembly solve
```

Each of those is a separate assertion. The solve runs last by construction —
it is a final pass — which is what lets a face target resolve against a body
built earlier in the same pass.

## Failure atomicity

| Expected | Measured |
| --- | --- |
| a broken mate | `Failed`, with an error recorded against it |
| transforms published | **none** — not a partial set, and not the previous pass's |
| canonical state | unchanged |
| the document afterwards | still usable — repairing the mate makes the next pass succeed |

**No partial derived state.** If anything in force failed or was blocked, the
pass publishes nothing and drops what it had. A set of transforms with one
component missing would render as a plausible assembly with a part in the
wrong place, which is worse than an empty one that says nothing.

## Determinism

| Expected | Measured |
| --- | --- |
| two identical documents, built and regenerated independently | same trigger, status, DOF, count, and **bit-identical** transforms |
| ten edit-and-return cycles | returns to **exactly** the same transform every time |
| Debug / Release / Debug-shared | the same tests asserting the same hand-derived values |

The drift case is the one this milestone needs specifically: regeneration
re-solves repeatedly, and a solver that accumulated state across passes would
drift. It does not, because the solve starts from intent every time and
nothing is seeded — the property ADR-005 chose deliberately and
`P13-SOLVE-001` measured.

## Save/load + regenerate

| Expected | Measured |
| --- | --- |
| a loaded document regenerates to the same transforms | **bit-identical** to before the save |
| the file after a solve | byte-identical to the file before it — no derived state persisted |
| a document loaded with its part missing | regenerates as broken, publishing nothing |
| and then repaired | regenerates successfully, transforms correct |

## Independent validation

Every expected position is derived by hand from the geometry and the change
made, never read back from the solver.

| Case | Expected | Measured | Result |
| --- | --- | --- | --- |
| Coincident planes, arm authored 50 mm up | arm at 0 | 0 | PASS |
| Distance 30 mm | arm at 30 mm | 0.030 | PASS |
| Concentric, placement moved to 80 mm | arm at 80 mm | 0.080 | PASS |
| Coincident to an end cap, depth 10 → 25 mm | arm at 25 mm | 0.025 | PASS |
| free parameter overridden to 90 mm | arm at 90 mm | 0.090 | PASS |
| that override edited to 120 mm | arm at 120 mm | 0.120 | PASS |
| mate suppressed | arm free, at its intent 50 mm | 0.050 | PASS |
| component suppressed | **no transform** | absent | PASS |
| mate target broken | **no transforms at all** | 0 published | PASS |
| unrelated part edited | no re-solve | `NotNeeded` | PASS |

## Adversarial review

**No production defect.** All 22 cases passed on their first run, trigger
classifications included, which is unusual in this project and worth being
suspicious of rather than pleased about — so the review below spends its
effort on whether the tests could pass while the code was wrong.

### Cleared

| Question | Answer |
| --- | --- |
| Can unrelated objects be regenerated unnecessarily? | No — measured: editing one part leaves the other `UpToDate` and absent from `regenerated`, and the assembly reports `NotNeeded` |
| Can a required solve fail to trigger? | Not for any input the solve reads. Six trigger cases are measured, each landing the component at a hand-derived position — including the two a revision-based trigger would miss entirely |
| Can a suppressed mate still contribute equations? | No — the pass asks for `activeMates()`, so P13-CONF-001's semantics come through unchanged; measured as the arm settling free at its intent |
| Can a stale resolved reference survive target loss? | No — the system is rebuilt on every solve, so there is no prior resolution to reuse, and a broken target publishes nothing at all |
| Can a cycle recurse forever? | No — the regenerator's existing topological ordering reports cycles as `Failed`; measured, and the assembly outside the cycle still solves |
| Can regeneration order depend on container iteration order? | No — the order is the dependency order, asserted by position: sketch < part < components < mates, and the solve last by construction |
| Can a failed regeneration partially commit derived transforms? | No — publishing is all-or-nothing, measured as 0 transforms when one mate fails, with both components absent |
| Can configuration switching reuse stale solve state? | No — six round trips between two builds whose answers differ, each measured at the right one |
| Can save/load persist derived state as canonical? | No — the file is byte-identical across a solve, and a reloaded document re-derives bit-identical transforms |
| Can an external unresolved reference appear regenerated successfully? | No — that was the point of P13-REF-001's component handler, and the mate handler now closes the same hole for targets |
| Can Debug and Release differ in affected-node order? | The order is the graph's topological order over `std::set`/`std::map` of IDs, which is value-ordered rather than address- or hash-ordered; the three presets run the same assertions |
| Can object deletion leave dangling dependency edges? | No — the graph is rebuilt from the document each pass, and `forgetMissing` drops results for objects that no longer exist |
| Can repeated regeneration accumulate numerical drift? | No — ten edit-and-return cycles return to a **bit-identical** transform. Structural rather than lucky: the solve starts from intent every pass and nothing is seeded (ADR-005) |

### Finding 1 — everything passing first time is itself a risk

Twenty-two cases, no failures. On the previous four milestones one or two
hand-derived expectations were wrong each time, and that is the normal rate
for predictions about code just written. A clean first run can mean the
predictions were right, or it can mean the assertions were too weak to
disagree with anything.

So the assertions were re-examined for that specifically. What protects them:

**The positions are the check, not the triggers.** Each trigger case also
asserts where the component ended up — 30 mm, 80 mm, 25 mm, 90 mm, 120 mm —
each derived by hand from the change made, and each *different* from the
others. A test that only asserted `trigger == ObjectChanged` would pass on an
implementation that re-solved for everything; asserting the resulting position
would not.

**The negative cases are real.** `NotNeeded` over five consecutive passes, and
`NotNeeded` after an unrelated part is built and confirmed present in
`regenerated`, cannot both hold on an implementation that always re-solves.

**The absent-transform cases distinguish empty from stale.** Asserting
`transform(arm) == nullptr` after a break fails on an implementation that kept
the previous pass's map, which is the specific defect being guarded against.

What would still pass while wrong: an implementation that re-solved slightly
more often than necessary in some case not enumerated here. The trigger is
derived from the solve's inputs rather than from a revision proxy, which
bounds that — but "no unnecessary re-solve, ever" is not claimed, only for the
cases measured.

### Finding 2 — the mate handler can fail a document that previously regenerated

Recorded because it is a deliberate behaviour change with real blast radius,
not a defect.

Before this milestone a mate whose target did not resolve was **silent**
during regeneration: a face is named through its feature's roles, so the
dependency graph reports nothing missing when the role stops existing, and a
handler-less mate regenerated as if all were well. Any document in that state
regenerated "successfully" while its assembly was unsolvable.

With the handler, such a document now **fails** — correctly. The full
regression across all three presets is what establishes that no existing test
or reference model was relying on the old silence, and it is the reason this
milestone's blast radius is the whole suite rather than the assembly tests.

The trade is the one this project keeps making: an explicit failure is worth
more than a plausible wrong answer, and the silence was the defect.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 438/438 | 0 | **1483/1483** | 209.1 s |
| `release` | 438/438 | 0 | **1483/1483** | 192.2 s |
| `debug-shared` | 438/438 | 0 | **1483/1483** | 224.8 s |

Then the milestone's related tests, five times over until failure — 1052
tests selected by the regeneration, dependency, reference, resolution,
configuration, suppression, solver, mate, component, placement, datum,
parameter, expression, object, persistence, sketch, CLI and architecture
names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **1052/1052 ×5** | 757.3 s |
| `debug` | **1052/1052 ×5** | 763.8 s |

1483 = the 1461 of `P13-STREF-001` plus this milestone's 22. All fifteen
stage exit codes are 0.

**Qualified tree.** The harness records a git tree ID per source directory
before building. Recomputed from the working tree after the run, all eight are
identical, so the tree that was qualified is the tree that is committed:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           8f49c42cde0ccef8653f03d994c7c56ce29cef46
src               5db7e70796d668aef5e640482c63ba28fbb3b0f7
tests             d0063843a2edbf1ebdd1802c811bec6e214443d2
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`ninja: warning: premature end of file; recovering` heads each build log. It
is ninja's own `.ninja_log`, damaged when runs were killed during
`P13-SOLVE-001`, and it makes ninja rebuild more rather than less — all 438
targets were built in every preset regardless. It is not a compiler warning,
and none appears in any of the three build logs.

**Test discovery, checked by name.** Each of the 22 new cases was looked for
individually in the qualification's own ctest log by its own name; all 22 are
there, and none is reported other than `Passed`. A file that compiles but
never registers would otherwise leave the suite green and the milestone
unverified.

## Known limitations

- **The solve is all-or-nothing.** One broken component or mate means no
  transforms are published for any component, not even the ones that are
  fine. That is deliberate — a partial set renders as a plausible assembly
  with a part in the wrong place — but it does mean a large assembly gives up
  all its positions for one broken mate.
- **Re-solve granularity is the whole assembly.** A constraint system is
  global, so there is no per-component incrementality to be had. What is
  avoided is solving when nothing relevant moved, not solving less of the
  system.
- **The trigger compares resolved placements every pass**, which is O(number
  of components) work per regeneration even when nothing changed. That is
  cheap beside a solve, and it is what catches the free-parameter override,
  but it is not free.
- **There is no undo of an assembly edit.** Regeneration keeps derived state
  current; commands and undo are `P13-CMD-001`.
- **Nothing renders the transforms yet.** They are published and available
  through `Regenerator::transform()`; no viewer or exporter consumes them.
- **`features::Regenerator` now has two registration mechanisms** — handlers
  and final passes — and a second result map. That is a real increase in its
  surface, and ADR-008 records it as the price of a derived result that is not
  an object's.

## Result

```text
TASK:            P13-REGEN-001 — Dependency / regeneration
IMPLEMENTATION:  +41 Regenerator.hpp and +28 its .cpp for the final-pass
                 mechanism; +48/-1 assembly Resolution.hpp and +202/-1 its
                 .cpp for the mate handler, the solve pass and its trigger.
                 No second regeneration path, no change to the existing
                 dirty tracking, ordering or cycle detection.
TESTS:           22 new cases in 1 file (644 lines)
VALIDATION:      every expected position derived by hand from the change
                 made; every trigger predicted before it was measured
REGRESSION:      1483/1483 on debug, release and debug-shared, each from
                 clean; 1052/1052 five times over in release and debug;
                 0 compiler warnings in all three builds; all 22 new cases
                 confirmed by name in the qualification's own log
ADVERSARIAL:     2 findings, 0 production defects
RESULT:          PASS
EVIDENCE:        this directory
```

**The line four milestones carried forward is closed.** Regenerating a
document now solves its assembly and publishes the transforms beside the
bodies.

What is claimed: the solve runs when one of its own inputs moved and not
otherwise, measured in both directions; it publishes the transforms of the
build in force and never a stale or partial set; regeneration writes derived
state and nothing else; a mate whose target does not resolve fails rather
than passing silently; and repeated regeneration does not drift.

What is **not** claimed: that the solve is incremental — a constraint system
is global, so what is avoided is solving when nothing relevant moved, not
solving less of the system; that a broken mate still yields the transforms of
the components that are fine (it deliberately does not); or that any edit can
be undone, which is `P13-CMD-001`.

## Revision

| When | What |
| --- | --- |
| 04:5x | ADR-008: the assembly solve is a final pass. Candidates compared before implementing |
| 05:0x | final-pass mechanism, mate handler and the solve registered as a pass |
| 05:2x | all 22 cases passed on the first run, trigger classifications included |
| 05:36 | full debug suite 1483/1483 — nothing in the existing suite relied on the mate handler's old silence |
| 06:44 | three presets and both repeat stages PASS on the final tree |
