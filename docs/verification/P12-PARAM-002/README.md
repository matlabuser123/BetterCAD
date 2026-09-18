# P12-PARAM-002 — Design Equations and Configurations

```text
TASK:            P12-PARAM-002
SCOPE:           persistent design equations between engineering parameters,
                 and named configurations that drive a part family from one
                 document
IMPLEMENTATION:  Configuration and ConfigurationTable as document-level state,
                 Document::effectiveParameterValue() and the three readers
                 routed through it, four undoable commands, an optional
                 `configurations` key in the native format, and
                 --configuration on the CLI
TESTS:           1211 tests per preset, 100% passed in Debug, Release and
                 Debug-shared after a clean rebuild of each; 37 new cases;
                 1022 x 5 repeats in Release and Debug; 0 compiler warnings in
                 377 translation units
VALIDATION:      volumes, centroids, bounds and derived parameter values from
                 closed forms derived by hand -- V = W^3/8 for the canonical
                 family and V = 0.04 W^3 - 0.000256 pi W^3 for the bracket --
                 none of them read back from BetterCAD
RESULT:          PASS
EVIDENCE:        this directory
REVISION:        qualified on the tree committed as this milestone; the eight
                 tree IDs are listed under Final Result
```

## Scope

Two halves, and the first was already built.

`P12-PARAM-001` delivered the equation engine: a unit-aware grammar with
parameter references, `+ - * /`, unary minus and parentheses; dimensional
analysis; expressions as edges of the document's dependency graph, with
topological evaluation, cycle detection, failure propagation and a
deterministic order; and save/load of both the expression and its last
evaluated value. **A design equation is a parameter expression.** This
milestone therefore adds nothing to that engine. What it does is extend the
qualification to equations driven by configurations, and build the second
half.

The second half is **named configurations**: one parametric document
describing a family of parts.

## Architecture

```text
base parameter value
        |
        v
configuration override        (the active configuration only)
        |
        v
effective parameter value     Document::effectiveParameterValue()
        |
        v
expression evaluation         equations follow, and are never duplicated
        |
        v
feature regeneration
```

**Configurations are document-level state, not document objects.** They are
not geometry, they take no part in the dependency graph, and they have their
own name space. They sit beside `parameters_` and `metadata_` in `Document`
(`include/bettercad/core/document/Configurations.hpp`,
`src/core/document/Configurations.cpp`). Their IDs come from the document's
one allocator, so no ID is ever reused by a parameter, an object or another
configuration, and a `ConfigurationId` deliberately does **not** widen to
`ObjectId`.

**A configuration never changes the document's canonical state.** A free
parameter's stored value is its *base* value and no configuration touches it;
the value in force is the base with the active configuration's override
applied. `Document::setParameterValue()` therefore always edits the base, and
switching configurations moves nothing.

That decision is what makes the rest simple, and it is the reason
`Small -> Large -> Small` restores Small's values **by bits**: the base values
never moved, and the overrides are stored numbers.

### Three readers, and nothing else to change

A parameter's value reaches geometry through exactly three places, and each
now asks for the effective value:

| Reader | File | What it drives |
| --- | --- | --- |
| `evaluateParameterExpression` | `src/core/document/ParameterExpressions.cpp` | every equation |
| `applyDrivingParameters` | `src/sketch/SketchRegeneration.cpp` | driven sketch constraints |
| `detail::drivingValue` | `src/features/SolidSupport.hpp` | every feature parameter |

So a configuration reaches the whole model, and **no feature knows that
configurations exist**. The sketch layer takes the overrides as a
`ParameterOverrides` argument that defaults to empty, so its coupling is
unchanged and every existing call means what it meant.

## Equation Semantics

Unchanged from `P12-PARAM-001`, and re-qualified here under configurations:

- literals with units, parameter references, `+ - * /`, unary `+`/`-`,
  parentheses, with precedence and left associativity;
- dimensional analysis: an expression must give exactly the parameter's
  dimension, and nothing is coerced;
- a parameter with an expression is *driven* and its value cannot be set
  directly.

What this milestone adds is that the **inputs** to an equation are the values
in force. Setting `width` in a configuration moves `height = width / 2` with
it, so a configuration never duplicates a derived value
(`Configurations_DerivedParametersAreNeverDuplicated`).

## Configuration Semantics

> A configuration is a named set of overrides to the document's **free**
> parameters. Everything else -- the equations, the sketches, the features --
> is shared.

- **Flat, not inherited.** Every configuration overrides the base document
  directly. No chains, because nothing in this milestone needs them.
- **The base configuration** is "none active": the values the parameters
  themselves hold. It is not a stored configuration and needs no entry.
- **Only free parameters.** A driven parameter's value comes from its
  expression; an override would be a second answer to the same question.
- **Overrides are dimension-checked** against the parameter they name.
- **Documents are not duplicated.** A configuration holds only the values it
  changes -- in the canonical family, exactly one number each.

### The invariant, and the hole this milestone closed

> **A parameter's value comes from its expression or from a configuration,
> never from both.**

Every path that creates an *override* enforced this from the start:
`setConfigurationOverride`, `insertConfiguration` and the file reader all
refuse a driven parameter. The paths that add an *expression* did not, so a
parameter could become driven while a configuration still set its value. That
was found by testing the invariant rather than the feature, reproduced, and
closed in `Document::setParameterExpression` and `Document::restoreParameter`:

```text
parameter 'width' is overridden by configurations 'Small', 'Medium' and
'Large'; clear the overrides to drive it by an expression
```

`Configurations_ADrivenParameterIsNeverAlsoOverridden` pins both directions.

Related: removing a parameter removes it from every configuration
(`ConfigurationTable::forgetParameter`), so an override can never dangle.

## Dependency Graph

Untouched. Configurations add no nodes and no edges: they change the *values*
that flow through the graph that expressions already build. Evaluation is
still topological with ties by ascending ID, and the existing cycle,
failure-propagation and blocking rules apply unchanged.

Measured under an active configuration:

- a chain `A -> B -> C -> D` re-evaluates in dependency order, not creation
  order (`Configuration_ChainsOfEquationsFollowTheOverride`: the parameters
  were created D, C, B, A and evaluate B, C, D);
- a cycle is still refused, nothing is evaluated, every parameter keeps its
  value, and the override stays in force
  (`Configurations_ACycleIsStillRefusedWhileAConfigurationIsActive`);
- an override that makes an equation fail (`span / count` with `count = 0`)
  is reported against the parameter, which keeps its last good value
  (`Configurations_AnOverrideThatBreaksAnEquationIsReported`).

A configuration can neither create nor break a cycle, because a parameter in
a cycle is driven and a driven parameter is never overridden.

## Implementation

| Area | Change |
| --- | --- |
| Core | `Configurations.hpp/.cpp`: `Configuration`, `ConfigurationTable`, `ParameterOverrides`, `validateConfigurationName` |
| Core | `Document`: the configuration table, `createConfiguration`, `insertConfiguration`, `removeConfiguration`, `setConfigurationOverride`, `clearConfigurationOverride`, `renameConfiguration`, `restoreConfiguration`, `setActiveConfiguration`, `activeOverrides`, `effectiveParameterValue`, `checkOverride` |
| Core | `Id.hpp`: `ConfigurationIdTag` / `ConfigurationId`, which does not widen to `ObjectId` |
| Core | `Commands.hpp/.cpp`: `CreateConfigurationCommand`, `ModifyConfigurationCommand`, `DeleteConfigurationCommand`, `SetActiveConfigurationCommand` |
| Core | `ParameterExpressions.cpp`: equation inputs are the values in force |
| Sketch | `SketchRegeneration`: `applyDrivingParameters` / `regenerateSketch` take the overrides (defaulted, so every existing call is unchanged) |
| Features | `SolidSupport.hpp`: `drivingValue` returns the value in force; `Regenerator` and `Validation` pass the document's overrides |
| IO | `DocumentJson.cpp`: an optional `configurations` key, written only when the document has any |
| CLI | `info` lists configurations and marks the active one; `info` and `validate` take `--configuration <name>` |

## Tests

37 new cases, in four layers, all tagged `[configurations][p12]`:

| File | Cases | What they cover |
| --- | --- | --- |
| `tests/core/document/ConfigurationTests.cpp` | 8 | what a configuration is, what it refuses, IDs, names, chains |
| `tests/features/ConfigurationFeatureTests.cpp` | 15 | the closed-form family, switching, the reference family, stable references, undo/redo, atomicity, determinism, performance |
| `tests/io/ConfigurationFileTests.cpp` | 6 | save/load/regenerate, transparent JSON, old files, malformed data |
| `tests/cli/ConfigurationCliTests.cpp` | 6 | `info`, `--configuration`, `validate`, failures |
| `tests/support/ConfigurationModels.hpp` | — | `BoxFamilyModel` and `BracketFamilyModel` |

## Independent Validation

Nothing here is checked against a BetterCAD-generated number. Every expected
value is a closed form derived by hand and written into the test.

### The canonical family — `V = W^3 / 8`

One free parameter and two equations, chosen so the volume is exact:

```text
width  = W                 free, and the only thing a configuration sets
height = width / 2
depth  = width / 4

V      = W (W/2)(W/4) = W^3 / 8
centroid = (W/2, W/4, W/8)
bounds   = (0, 0, 0) to (W, W/2, W/4)
```

Release build of the qualified tree. `expected` is the closed form; `measured`
is what the kernel produced. The tolerance is `kRelTight`, the project's bound
for well-conditioned double-precision algebra.

| Configuration | W (mm) | height | depth | Expected V (mm^3) | Measured V (mm^3) | Abs error | Rel error | Tolerance | Result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Base (none active) | 100 | 50 | 25 | 125000 | 125000.00000000002910 | 2.91e-11 | 2.33e-16 | 1e-12 | **PASS** |
| Small | 40 | 20 | 10 | 8000 | 8000.00000000000000 | 0 | 0 | 1e-12 | **PASS** |
| Medium | 80 | 40 | 20 | 64000 | 64000.00000000002183 | 2.18e-11 | 3.41e-16 | 1e-12 | **PASS** |
| Large | 160 | 80 | 40 | 512000 | 512000.00000000017462 | 1.75e-10 | 3.41e-16 | 1e-12 | **PASS** |

The centroid is checked at `(W/2, W/4, W/8)` and the bounds at
`(0,0,0)-(W, W/2, W/4)`, both to 1e-9 mm; the largest deviation measured over
the whole suite is 1.42e-14 mm. `height` and `depth` are overridden by no
configuration -- they are equations, and follow.

### The bracket family

```text
width         = W                       free
height        = width / 2
thickness     = 0.08 * width
edge          = 2 * thickness           = 0.16 W
hole_diameter = thickness               = 0.08 W
hole_space    = width - 2 * edge        = 0.68 W

V = W (W/2)(0.08 W) - 2 pi (0.04 W)^2 (0.08 W)
  = 0.04 W^3 - 0.000256 pi W^3
```

The tolerance is `kRelApproximatedIntersection` (1e-9), the project's bound
for bodies whose faces the kernel approximates; the holes are cylinders
meeting planes, so the measured error is far smaller.

| Configuration | W (mm) | thickness | hole dia | hole space | Expected V (mm^3) | Measured V (mm^3) | Abs error | Rel error | Tolerance | Result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Small | 50 | 4 | 4 | 34 | 4899.469035085127 | 4899.469035085128 | 9.10e-13 | 1.86e-16 | 1e-9 | **PASS** |
| Medium | 100 | 8 | 8 | 68 | 39195.752280681016 | 39195.752280681016 | 0 | 0 | 1e-9 | **PASS** |
| Large | 200 | 16 | 16 | 136 | 313566.018245448126 | 313566.018245448184 | 5.82e-11 | 1.86e-16 | 1e-9 | **PASS** |

The derived parameters are checked against their closed forms at every size
too, to `1e-12`: `height = W/2`, `thickness = 0.08 W`, `edge = 0.16 W`,
`hole_diameter = 0.08 W`, `hole_space = 0.68 W`.

## Reference Family

`BracketFamilyModel` is a mounting bracket across four qualified feature
kinds, driven by one free parameter through a chain of five equations:

```text
PlateSketch   rectangle, width and height driven
Plate         extrude, depth driven by thickness
Bore          hole, diameter driven by hole_diameter, drilled from z = 0
Holes         linear pattern of Bore, spacing driven by hole_space
TopSketch     circle attached to the NAMED end cap of Plate
```

Two details are engineering decisions rather than conveniences:

- **The holes are drilled from the plate's start plane, `z = 0`.** A hole's
  start face is a geometric signature (a plane and a side), not a stable
  name, so drilling from the top would name a plane whose height is itself
  parametric and would break at the first configuration switch. The start
  plane is the one plane no configuration moves.
- **The attached sketch names the end cap**, whose height *is* parametric
  (`0.08 W`). That is deliberately the hard case, and it is what the
  stable-reference test measures.

## Stable References

`TopSketch` names the plate's end cap. Its height is `0.08 W`, so every
configuration moves it. Measured
(`ConfigurationFamily_AttachedSketchesFollowTheFaceTheyName`):

| Configuration | W (mm) | End cap z (mm), expected | Named face found at | Sketch placement at |
| --- | --- | --- | --- | --- |
| Small | 50 | 4 | 4 | 4 |
| Medium | 100 | 8 | 8 | 8 |
| Large | 200 | 16 | 16 | 16 |

all to `1e-9` mm. After `Small -> Large -> Small` the sketch is back at
`z = 4`, and the test also asserts that **nothing** lies at Large's `z = 16`
in the Small configuration, so the reference cannot have snapped to a plane
that merely used to be right.

Configurations reach references through the same parameter-value path that
`setParameterValue` already used, which `P12-STREF-001` and `P12-SKETCH-003`
qualified; no reference-resolution code was changed by this milestone. The
case where a parameter change *removes* the named geometry and resolution
must fail with `NotFound` is covered there and still passes
(`SketchOnFace_*`, in the regression comparison below).

## Failure Paths

Every one of these is refused with a structured diagnostic, and the document
is left exactly as it was. Most are refused at the boundary, so a document
can never hold an invalid configuration in the first place.

| Case | Code | Where it is refused |
| --- | --- | --- |
| override of a driven parameter | `FailedPrecondition` | `setConfigurationOverride`, `insertConfiguration`, file load |
| a parameter becomes driven while an override exists | `FailedPrecondition` | `setParameterExpression`, `restoreParameter` |
| wrong dimension | `DimensionMismatch` | `requireOverridable` |
| unknown parameter | `NotFound` | `requireOverridable`, file load |
| unknown configuration | `NotFound` | `setConfigurationOverride`, `setActiveConfiguration` |
| non-finite value | `InvalidArgument` | `Configuration::setOverride` |
| duplicate configuration name | `AlreadyExists` | `ConfigurationTable::add`, `rename` |
| empty or invalid name | `InvalidArgument` | `validateConfigurationName` |
| deleted parameter still overridden | — | removed from every configuration |
| expression cycle | reported | `evaluateParameterExpressions` |
| equation failure under a configuration | reported | `evaluateParameterExpressions` |
| feature regeneration failure | reported | `Regenerator` |
| corrupt persisted configuration | `ParseError` / the underlying code | file load, with the JSON path |

**Atomicity.** `Configurations_AFailingConfigurationChangesNothing` takes a
valid Small model, switches to a configuration whose `width` is 0 mm,
and checks that regeneration fails, that no body is left, and that the
document's configurations and base values are untouched. Repairing the
configuration and regenerating restores the original solid exactly.

Malformed persisted configurations are reported with their JSON path, e.g.
`configurations.defined[0].overrides[0].parameter: no parameter with ID 987`
and `configurations.active: no configuration named 'Gone'`.

## Undo / Redo

Four commands in the existing architecture; no separate history subsystem.
`Configurations_UndoAndRedoOfEveryOperation` covers create, modify an
override, delete and switch, each undone and redone:

- **create** -- redo restores the same `ConfigurationId`, so anything that
  referred to it still does;
- **modify** -- the volume follows the override and comes back, checked
  against `W^3/8` at both values;
- **delete** -- deleting the active configuration falls back to the base one,
  and undo restores it *as the active one*, with its overrides;
- **switch** -- undo walks back through the previous active configurations to
  the base one.

## Persistence

The native format gains one optional key. A document with no configurations
writes **no key at all**, so a file written before this milestone is byte for
byte what it was:

```json
"configurations": {
  "active": "Medium",
  "defined": [
    { "id": 6, "name": "Small", "overrides": [ { "parameter": 1, "si_value": 0.04 } ] }
  ]
}
```

The active configuration is stored by **name**, not by number, so the file
stays readable. An override stores the parameter's ID and an SI value; the
dimension and the display unit are the parameter's own, and loading checks
that the parameter exists, is free, and has that dimension.

The real round trip -- build, select a configuration, save, destroy, load,
regenerate -- is `Configurations_SaveLoadRegeneratesTheSameFamily`, run for
Small, Medium and Large. It compares the whole document (`equivalent`), the
configuration count, the active configuration's name, the effective value of
`width` **by bits**, and the regenerated volume **by bits**; then switches to
Large after loading and checks the volume against `160^3/8` again.
`ConfigurationFamily_SaveLoadKeepsTheWholeFamily` does the same for the
bracket.

Backward compatibility is
`Configurations_FilesWrittenBeforeThisMilestoneLoadUnchanged`: a document with
expressions and no configurations writes text containing no `configurations`,
loads with none, keeps its effective values by bits, and **re-saves to
identical text**.

## Determinism

| What | Result |
| --- | --- |
| the same configuration regenerated twice | volume, area and centroid identical by bits |
| a document built twice from scratch into the same configuration | identical by bits |
| parameter values after `Small -> Large -> Small` | identical by bits |
| parameter values reached by wandering vs. directly | identical by bits |
| save -> load -> regenerate | volume identical by bits |
| saving the same model twice | identical text |
| across Debug, Release and Debug-shared | identical: same measured values, MD5 `b1e61d7a1b40825e6cfaa711748dbc43`, **0 differing lines** |

`Configurations_TheValuesInForceAreExactlyReproducible` builds the family,
wanders through Large, Small and Medium, then selects a configuration, and
compares the bit patterns of `width`, `height` and `depth` with a document
that went straight there. They are equal, and Small is exactly
`(0.04, 0.02, 0.01)` m.

## Performance

Measured on an idle machine, five runs per preset, after the competing
baseline build had finished; the first attempt was discarded because a
concurrent build was saturating the CPU and made Release appear slower than
Debug. These are **measurements, not guarantees**.

| Measurement | Debug (median of 5) | Release (median of 5) |
| --- | --- | --- |
| configuration switch + regenerate | **73.2 ms/cycle** (71.4-74.8) | **78.7 ms/cycle** (69.2-81.5) |
| regenerate with nothing changed | **0.733 ms/pass** (0.573-0.843) | **0.053 ms/pass** (0.052-0.095) |

The switch-and-regenerate cost is the same in both presets, and the idle pass
is about 14x faster in Release. That is what the split predicts rather than a
regression: a switch rebuilds the model, so its cost is dominated by the
geometry kernel, which is the same prebuilt OCCT 8.0.1 library in both
presets -- only BetterCAD's own 377 translation units are compiled
differently. The idle pass does no kernel work at all (it walks the
dependency graph and evaluates expressions), so it shows the compiler
difference in full.

There is no prior configuration-switching figure to regress against; this is
the first. Nothing was profiled and no optimization was attempted, so no
speedup is claimed.

## Regression

**Test names.** `qualification/regression-comparison.txt`: every one of the
**1173** test names `P12-LOFT-001` ran is present and passed in all three of
this milestone's qualified CTest logs; 37 names are new.

| Log | Distinct names | Baseline missing | Baseline not passed | New |
| --- | --- | --- | --- | --- |
| Debug | 1210 (1211 entries) | 0 | 0 | 37 |
| Release | 1210 (1211 entries) | 0 | 0 | 37 |
| Debug-shared | 1210 (1211 entries) | 0 | 0 | 37 |

**Measured values.** `qualification/all-values-comparison.txt`: every measured
value of every pre-existing test, from a Release build of `P12-LOFT-001`'s own
commit (`230ceda`, built in a separate detached worktree of that revision) and
from this milestone's qualified Release build.

**Three lines differ, and all three are the same expected change**: the usage
text of `info` and `validate`, which now documents the option this milestone
adds --

```text
before:  Usage: bettercad-cli info <file.bcad>
after:   Usage: bettercad-cli info <file.bcad> [--configuration <name>]
```

The three entries are CLI tests asserting that a usage error names its
command; each still passes. **No unexpected change**: not one volume, area,
centroid, bounding box, topology count or diagnostic message of a pre-existing
test moved.

**Tolerances.** `qualification/deviations.txt`: every tolerance is met with
orders of headroom, and the tightest is met at rounding.

| File | Kind | Tolerance | Checks | Largest deviation |
| --- | --- | --- | --- | --- |
| `ConfigurationFeatureTests.cpp` | absolute (mm) | 1e-9 | 43 | 1.42e-14 |
| `ConfigurationFeatureTests.cpp` | relative | 1e-12 | 52 | 5.68e-16 |
| `ConfigurationFeatureTests.cpp` | relative | 1e-9 | 4 | 1.86e-16 |
| `ConfigurationFileTests.cpp` | relative | 1e-12 | 4 | 3.41e-16 |
| `ConfigurationTests.cpp` | relative | 1e-12 | 9 | 1.16e-16 |

No tolerance was loosened for this milestone.

## Known Limitations

1. **Switching configurations does not reproduce the solid bit for bit.** The
   parameter values do, and everything BetterCAD computes from them does, but
   the sketch solver starts from the geometry the sketch currently holds, so
   the point it converges to depends on the path taken.

   This is the solver's behaviour and **predates this milestone**: with no
   configuration anywhere, setting `width` to 160 mm and back to 100 mm gives
   a volume of `0.00012500000000000003` m^3 where building 100 mm directly
   gives `0.000125` -- one unit in the last place. It was measured before it
   was attributed, rather than assumed to be a configuration bug.

   Measured over `Small -> Medium -> Large -> Small` followed by four
   `Small -> Large -> Small` laps and a comparison with a freshly built Small:
   the largest relative volume difference is **1.27e-15**. Each lap is
   compared with the *first* result, not the previous one, so a drift that
   accumulated would show as a growing difference; it does not grow, and the
   topology is identical every lap. `kRelTight` (1e-12) is three orders
   clear. Removing the warm start would be a change to qualified sketch
   behaviour, not a fix, so it was not made.

2. **Configurations are flat.** A configuration overrides the base document;
   there is no inheritance between configurations. Nothing in this milestone
   needed it.

3. **Only values can be overridden.** A configuration cannot suppress a
   feature, change a feature's operation, or change topology -- only the
   values of free parameters. This is why no configuration in this milestone
   can remove a named face.

4. **Editing a parameter edits the base.** With a configuration active,
   `setParameterValue` changes the base value, not the configuration's
   override; the override is edited through the configuration. This is
   deliberate -- the document's canonical state stays the engineering intent
   -- and is pinned by
   `Configurations_TheBaseValuesAreNeverTouched`.

5. **A driven parameter cannot be overridden**, and a parameter that is
   overridden cannot be given an expression. The overrides must be cleared
   first; the diagnostic names them.

## Final Result

Qualified by `qualification/run-qualification.cmd`, which for each preset
configures, removes every build output, rebuilds with warnings as errors, and
runs CTest only after a successful build; then repeats this milestone's
related tests five times in Release and Debug. Logs in `qualification/`.

| Preset | Configure | Clean | Build | Warnings | TUs | Tests | Runtime |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Debug | 0 | 0 | 0 | **0** | 377 | **1211 / 1211** | 121.56 s |
| Release | 0 | 0 | 0 | **0** | 377 | **1211 / 1211** | 125.54 s |
| Debug-shared | 0 | 0 | 0 | **0** | 377 | **1211 / 1211** | 117.75 s |
| repeat x5, Release | — | — | — | — | — | **1022 / 1022** | 487.87 s |
| repeat x5, Debug | — | — | — | — | — | **1022 / 1022** | 453.02 s |

**The qualified tree is the committed tree.** The eight Git tree IDs
`qualify.cmd` recorded from a scratch index before building were compared with
the working tree after the run and are identical:

```text
  apps              9b7b03a72ef1a773304a38dd02690818f66d50a3
  include           0347b09b01baf7ad55ec6a67d3a8e3256676e3a1
  src               b5f9b6e92cc4ee1451f67fda6983f89c4b222435
  tests             ba239110bc166901662ac3494ba2283d400a9a5d
  examples          07f6ea93fbb93e97a4e6e989ef7636a612d26848
  cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
  CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
  CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

An earlier run of this qualification was **discarded and restarted from
clean** after two gaps were found against the acceptance list -- an explicit
`Small -> Medium -> Large -> Small` cycle, and a cycle refused while a
configuration is active. Both are now tests, and the results above are the
restarted run's.

```text
TASK:            P12-PARAM-002
IMPLEMENTATION:  named configurations over the existing equation engine: the
                 value in force is the base value with the active
                 configuration's override applied, and three readers ask for
                 it, so a configuration reaches the whole model
TESTS:           1211 per preset, 100 % in Debug, Release and Debug-shared
                 after a clean rebuild of each; 37 new cases; 1022 x 5
                 repeats in Release and Debug; 0 warnings in 377 TUs
VALIDATION:      V = W^3/8 and V = 0.04 W^3 - 0.000256 pi W^3, derived by
                 hand; worst relative error 3.4e-16 against a 1e-12 tolerance
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P12-PARAM-002 ticked; next is P12-REF-001
```
