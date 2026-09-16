# P12-PARAM-001 — Parameter Expression Evaluation Verification

## Status

**PASS.** Parameter expressions are parsed with units, evaluated with
dimensional analysis, and take part in the document's dependency graph and
regeneration. A parameter whose expression fails keeps its last value and is
reported; what depends on it is blocked. Cycles, unknown names and dimension
errors are refused with structured diagnostics that name the token. Expressions
and their evaluated values round-trip through the native file format.

Debug, Release and Debug-shared each passed **777/777** tests with **0
compiler warnings**, and every test of the P11 qualification still passes in
all three. The Debug preset's first clean build was incomplete (a locked object
file, see Qualification) and was repeated from a verified clean: 281 of 281
translation units compiled, 777/777 tests passed.

Date: 2026-09-16. `main` was at `5e2194f` (documentation only, after
`P11-QUAL-001`) before this milestone. Every number below was measured in this
session and is recorded in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-PARAM-001`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| Unit-aware grammar: literals with units, parameter references, `+ - * /`, unary minus, parentheses, precedence | IMPLEMENTED | `Expression_LiteralsCarryTheirUnitsInSi`; `…AppliesPrecedenceAndLeftAssociativity`; `…UnitIsTheLongestSymbolEndingAtANameBoundary`; `…ReferencesAreSortedUniqueNames` |
| Dimensional analysis: `Length + Angle` fails, `Length / Length` is dimensionless | IMPLEMENTED | `Expression_DimensionalAnalysisCombinesAndChecksDimensions`; `DocumentExpression_DimensionErrorsAreStructuredFailures`; `DocumentExpression_EvaluatesEveryDimension` |
| Expression dependencies in the document dependency graph | IMPLEMENTED | `DocumentExpression_DependenciesAreGraphEdges`; `ParameterExpressions_DrivePlateAndReevaluateWhenWidthChanges` |
| Cycle detection across expression references | IMPLEMENTED | `DocumentExpression_CyclesAreRefusedAndKeepTheirValues`; `ParameterExpressions_CycleFailsParametersAndBlocksGeometry` |
| Unknown-symbol, malformed-syntax and dimension diagnostics naming the token | IMPLEMENTED | `Expression_RejectsMalformedSyntaxNamingTheToken`; `…EvaluationFailuresNameTheOffendingToken`; `DocumentExpression_UnknownAndNonParameterNamesAreReported` |
| Deterministic evaluation order | IMPLEMENTED | `DocumentExpression_EvaluatesInDependencyOrderNotCreationOrder`; `Expression_EvaluationIsDeterministic`; `ParameterExpressions_RegenerationIsDeterministic` |
| Failure propagation: a failing expression keeps its last value and is reported | IMPLEMENTED | `DocumentExpression_FailedExpressionKeepsLastValueAndBlocksDependents`; `ParameterExpressions_FailedExpressionBlocksGeometryAndKeepsLastValue` |
| Save/load of expressions and evaluated values | IMPLEMENTED | `ParameterExpressions_SaveLoad_PreservesExpressionsAndEvaluatedValues`; the four other `ParameterExpressions_*Load*`/`Saving*` tests; `…ExampleFileMatchesTheBuilder` |
| Evidence | this directory | — |

Also in scope because the deliverables need them: validation
(`ParameterExpressions_ValidationReportsEachExpressionProblemOnce`), undo/redo
(`DocumentExpression_UndoRedoRestoresExpressionAndValue`), refusal of direct
values on driven parameters (`DocumentExpression_DrivenParameterRefusesDirectValues`)
and the CLI (`ExpressionCli_*`, `cli.info.driven-plate`,
`cli.validate.driven-plate`, `cli.export-step.driven-plate`).

**Not in scope, and not implemented:** functions (`sqrt`, `sin`, …), powers,
constants (`pi`), conditionals, and expressions anywhere but in parameters
(feature fields still take a literal or one parameter). Design equations and
configurations are `P12-PARAM-002`.

## Design

```text
Expression (core, layer 0)                include/bettercad/core/parameters/Expression.hpp
  parse(text) → postfix steps + sorted names; no document needed
  evaluate(resolver) → DimensionedValue (SI value + Dimension)
ParameterExpressions (core)                include/bettercad/core/document/ParameterExpressions.hpp
  resolveExpressionNames(document, expression)
  evaluateParameterExpression(document, id)      one parameter, nothing modified
  evaluateParameterExpressions(document)         every driven parameter, in dependency order
buildDependencyGraph (core)                parameter → parameter edges; DocumentGraph::unresolved
Document (core)                            syntax checked on every store; driven values refused;
                                           evaluated values stored through a private entry point
Regenerator (features)                     evaluates first, then the dirty set, as before
validateDocument (features)                evaluates on its copy; classifies expression problems
```

**Grammar.** A recursive-descent parser produces a postfix program; evaluation
is a loop over it with a small stack, with no recursion and no owning
pointers. The rules that decide ambiguous text:

- a name directly after a number is always a unit (`2 width` is refused, so
  there is no implicit multiplication);
- the unit is the longest catalog symbol that ends where a name would end
  (`3 m/s` is a velocity; `3 mm/speed` is 3 mm divided by `speed`);
- a name anywhere else is a parameter, even one that spells a unit
  (`a / s`).

Limits: 512 bytes, 32 levels of parentheses and unary signs, 64-character
names. Literals are converted with the unit catalog's exact factors, so
`12.5 mm` is bit-for-bit `12.5 * units::mm`.

**Dimensions.** Values carry a run-time `Dimension` (`DimensionedValue`, now
one type shared with `ModifyParameterCommand`). `+` and `-` require equal
dimensions; `*` and `/` combine them. A result must have exactly the
parameter's dimension. Nothing is converted or coerced. Division by zero and
non-finite intermediate values are errors.

**When expressions are evaluated.** Setting an expression checks its syntax
only; the value is computed when the document's expressions are evaluated,
which the regenerator does at the start of every pass. This is the
architecture's regeneration flow (a parameter change makes its dependents
dirty; the engine evaluates them in order), and it keeps three P1–P2 tests
valid that set expressions naming parameters that do not exist
(`2 * height`, `height * 2`, `h / 2`): such text is stored and reported when
evaluated.

**Driven parameters.** A parameter with an expression is driven:
`Document::setParameterValue`/`setParameterSiValue` refuse it
(FailedPrecondition), and so does a `ModifyParameterCommand` that changes only
its value. A command that sets a value together with a new or cleared
expression is accepted (the P2 test "everything at once" does this). Evaluated
values reach the parameter only through `Document::storeExpressionValue`, a
private function the evaluator reaches through a friend class.

**Graph and regeneration.** `buildDependencyGraph` adds an edge from each
parameter an expression names. Names that are not parameters go to
`DocumentGraph::unresolved` with NotFound (unknown) or InvalidArgument (an
object's name). `evaluateParameterExpressions` walks the graph's topological
order (ties by ID): cycle members are not evaluated, parameters downstream of
a failure or a cycle are blocked, and only values that change are stored, so
revisions advance only on effective changes. The regenerator then treats a
changed parameter like an edited one (its dependents are dirty), fails each
failed expression, and blocks what depends on it. Parameters are never listed
in `RegenerationReport::regenerated`; `updatedParameters` lists the driven
parameters whose value changed. With no change, a second pass changes nothing,
not even the document revision.

**Files.** The `expression` field keeps its format-version-1 form: the text
next to `si_value`, which now holds the last evaluated value. No format version
change was made:

- the field always described intended behaviour ("stored, not evaluated
  *yet*"); P12 implements it;
- a reader that ignores expressions (an older BetterCAD) still finds the values
  that were saved;
- the P9 test `Invalid document files are rejected with the JSON path`
  requires `"version": 2` to be refused, so a version bump would have changed a
  P0–P11 test;
- a stored value that disagrees with its expression (a hand-edited file, or one
  written before P12) is replaced at the next regeneration and reported in
  `updatedParameters`, never silently
  (`ParameterExpressions_Load_StaleValueIsReevaluatedAndReported`).

Loading refuses text that does not parse, with the JSON path. Unknown names
load (a missing reference is a valid document state) and are reported by
regeneration and validation.

## Independent Validation

### The acceptance examples

`Expression_AcceptanceExamplesEvaluateFromTheirInputs`, with `width` = 100 mm
and `edge_distance` = 15 mm. The expected values are computed in the test with
the same double operations the text describes, so they are compared exactly;
they are also compared with the decimal values.

| Expression | Expected | Actual | Exact | Decimal |
| --- | --- | --- | --- | --- |
| `width / 2` | 0.1 / 2 = 0.05 m | 0.05 m (bits `0x3fa999999999999a`) | equal | 50 mm within 1e-15 |
| `0.1 * width` | 0.1 × 0.1 = 0.010000000000000002 m | the same | equal | 10 mm within 1e-15 |
| `width - 2 * edge_distance` | 0.1 − 2 × 0.015 = 0.07000000000000001 m | the same | equal | 70 mm within 1e-15 |

With `width` = 160 mm the same texts give 80, 16 and 130 mm (within 1e-15
relative).

### Geometry driven by expressions

`DrivenPlateModel` (`tests/support/DrivenPlateModel.hpp`): a plate
`width` × `height`, extruded by `thickness`, with two holes of radius 5 mm
`hole_spacing` apart at `hole_y`:

```text
height = width / 2        thickness = 0.1 * width
hole_spacing = width - 2 * edge_distance        hole_y = height / 2
```

The expected volume is written out by hand from the definitions, without the
expression engine: V(w) = (w · w/2 − 2π · 5²) · w/10.

| width | Expected volume | Regenerated volume | Relative error |
| --- | --- | --- | --- |
| 100 mm | 48429.20367320510558784 mm³ | 48429.20367320509831188 mm³ | 1.5e-16 |
| 160 mm | 202286.72587712816311978 mm³ | 202286.72587712819222361 mm³ | 1.4e-16 |

Tolerance 1e-12 relative (as for P11's planar parts). Also checked at both
widths: every driven value (1e-15 relative), the body's bounds
(w, w/2, w/10 within 1e-7 mm; the bounds are the kernel's exact bounds), and
both hole centres in the solved sketch (15 and w − 15 mm along x, w/4 along y,
within 1e-9 mm). Undo back to 100 mm restores height, thickness and hole
spacing bit for bit.

**Evaluation order.** The plate's driven parameters were created in the order
hole_y, height, thickness, hole_spacing. They are evaluated in dependency
order: height, hole_y, thickness, hole_spacing. A second chain
(`DocumentExpression_EvaluatesInDependencyOrderNotCreationOrder`) is created in
reverse and evaluates b, c, d, each bit-identical to the same arithmetic
written in C++.

## Diagnostics

Every failure is an `Error` with a code and a message that names the
offending token and its byte offset. Examples, all asserted verbatim by the
tests:

| Case | Code | Message |
| --- | --- | --- |
| `width +` | ParseError | `expected a number, a name or '(' at offset 7, found the end of the expression` |
| `(width + 2 mm 3)` | ParseError | `expected ')' at offset 14 to close the '(' at offset 0, found '3'` |
| `2 width` | ParseError | `unknown unit 'width' at offset 2 (a name directly after a number must be a unit; write '*' to multiply)` |
| `caf\xC3\xA9` | ParseError | `… at offset 3, found the byte 0xC3` (no UTF-8 sequence is cut) |
| 513 bytes | InvalidArgument | `the expression is 513 bytes long; the limit is 512` |
| `width + draft` | DimensionMismatch | `cannot add 'width' (length) and 'draft' (angle) at offset 6` |
| `width * draft` for a length | DimensionMismatch | `parameter 'height' = width * draft: the result has dimension quantity [m*rad], but the parameter has dimension length` |
| `width / gap`, gap = 0 | InvalidArgument | `parameter 'ratio' = width / gap: division by zero at offset 6: 'gap' is zero` |
| deleted input | NotFound | `parameter 'height' = width / 2: unknown parameter 'width' at offset 0` |
| a sketch's name | InvalidArgument | `'Sketch1' is not a parameter but a document object of type 'test_consumer' at offset 0` |
| `b * 2` for b | FailedPrecondition | `parameter 'b' = b * 2: the expression uses the parameter 'b' itself at offset 0` |
| cycle | FailedPrecondition | `dependency cycle: hole_spacing, width` |
| setting a driven value | FailedPrecondition | `parameter 'height' is driven by the expression 'width / 2'; clear the expression to set its value` |
| malformed text in a file | ParseError | `parameters[1]: parameter 'height': expression 'width / ': expected a number, …` |

**Validation** reports each problem once, under the first check that finds it:
unparsable text and object names under document consistency, unknown names
under missing references (every unknown name of an expression), cycles under
dependency cycles, and evaluation failures under feature regeneration as
"… failed to evaluate: …". Sketches that use an unevaluated parameter are not
solved with an unknown value; they are reported as "was not regenerated".
Blocked parameters are reported as "was not evaluated".

## Tests

36 new Catch2 test cases, all tagged `[p12]`, and 3 process tests:

- `tests/core/parameters/ExpressionTests.cpp`: 10 (grammar, units,
  precedence, dimensions, diagnostics, limits, determinism);
- `tests/core/document/ParameterExpressionTests.cpp`: 10 (document storage,
  driven parameters, graph edges, order, cycles, failure propagation,
  dimensions, unknown names, undo/redo);
- `tests/features/ExpressionRegenerationTests.cpp`: 7 (acceptance, no-change
  pass, failure and recovery, dimension error, cycle, validation,
  determinism);
- `tests/io/ExpressionFileTests.cpp`: 6 (round trip, determinism, stale
  values, malformed text, unknown names, the example file);
- `tests/cli/ExpressionCliTests.cpp`: 3 (info, validate, export);
- `tests/CMakeLists.txt`: `cli.info.driven-plate`,
  `cli.validate.driven-plate`, `cli.export-step.driven-plate`, running the
  real executable on `examples/models/driven_plate.bcad`.

Shared test code added: `tests/support/DrivenPlateModel.hpp` and
`tests/cli/CliRunner.hpp` (so no legacy test file was changed).

The Release run of `[p12]` records 4696 passed assertions and 0 failed
(`expression-values-release.txt`).

## Qualification

`qualify.cmd` (in this directory, run through `run-qualification.cmd`)
configured, cleaned and rebuilt each preset with warnings as errors, from
`cmd` under code page 65001, and ran CTest only after a successful build. It
then repeated the related tests five times in Release and Debug. It recorded
the Git tree IDs of the sources it built (`qualification-times.txt`); no
source, test or CMake file changed during the run or afterwards.

| Preset | Configure | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- |
| Debug (first run) | exit 0 | exit 0, **clean incomplete** | 280 | 0 | 777/777 passed (110.8 s) |
| Release | exit 0 | exit 0 | 281 | 0 | **777/777 passed** (108.2 s) |
| Debug-shared | exit 0 | exit 0 | 281 | 0 | **777/777 passed** (119.8 s) |
| Debug (rerun from a verified clean) | exit 0 | exit 0 | 281 | 0 | **777/777 passed** (106.7 s) |

**The Debug clean was incomplete in the first run.** `build-debug.log` starts
with `ninja: error: remove(src/sketch/CMakeFiles/bettercad_sketch.dir/constraints/SketchConstraints.cpp.obj):
The process cannot access the file because it is being used by another
process.` `cmake --build --clean-first` still built, so one object file
(from the previous Debug build of the same, unchanged source) was not
recompiled: 280 files instead of 281. That is not a clean rebuild, so Debug was
rebuilt by `rerun-debug.cmd`: configure, clean (attempt 1 succeeded, 297 files
removed), build (281 files, 0 warnings) and CTest (777/777). Both runs' logs are
kept (`*-debug.log` and `rerun-*-debug.log`). The lock is the OneDrive/scanner
behaviour recorded in `TODO.md`; `qualify.cmd` for later milestones cleans
explicitly and retries.

281 = P11-QUAL-001's 274 translation units + `Expression.cpp`,
`ParameterExpressions.cpp` and the five new test files.

**Repeats** (`ctest -R "[Ee]xpression|[Pp]arameter|[Rr]egenerat|[Vv]alidat|[Dd]ependenc|[Dd]ocument|[Cc]ommand|[Dd]riven|P9|cli\." --repeat until-fail:5`):
231 tests, the 39 new ones among them.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 231/231, each test 5 times (1155 passed runs) | 142.8 s |
| Debug | 231/231, each test 5 times (1155 passed runs) | 138.3 s |

The word "Failed" appears in those logs only in test names
(`…FailedExpressionBlocksGeometryAndKeepsLastValue`).

**Fresh processes across configurations.** `bettercad-cli validate
examples/models/driven_plate.bcad` from the Debug, Release and Debug-shared
executables printed byte-identical output (MD5
`aad6706a5b9017b7bf07c435c0e2ee8a`; `cli-determinism.txt`). The test
`ParameterExpressions_ExampleFileMatchesTheBuilder` passed in every preset: the
regenerated model saves to exactly the committed file, so the evaluated values
are bit-identical in all three configurations.

## Legacy Regression

`regression-comparison.txt` (`compare-regression.py`) compares every log by
test name with `../P11-QUAL-001/release-ctest.log` (738 entries, 737 distinct
names):

- all 737 names are present and passed in the Debug, Release, Debug-shared
  and Debug-rerun logs; no entry in any log failed or was not run;
- the 39 new names are exactly the new tests listed above.

**The P0–P11 regression suite remains green.** No legacy test file was
changed. Changes to existing production code:

- `Document`: syntax check on store, refusal of direct values on driven
  parameters, the private evaluated-value entry point;
- `ModifyParameterCommand`: refuses a value-only change of a driven parameter;
- `buildDependencyGraph`: expression edges and `unresolved`;
- `Regenerator`: evaluates expressions first; `updatedParameters`;
- `validateDocument`: evaluates on its copy; expression checks;
- `DimensionedValue` moved from `Commands.hpp` to
  `core/units/DimensionedValue.hpp` (and gained `operator==`);
- documentation comments (`Parameter.hpp`, `ReferenceModels.hpp`).

The P9 bracket fixture has `slot_depth = depth * 0.6` with a stored 12 mm;
0.02 × 0.6 is exactly the double 0.012, so evaluating it changes nothing and
every P9 test is unaffected.

## Known Limitations

- **No functions, powers or constants.** `sqrt(area)`, `x^2` and `pi` are
  refused as syntax errors.
- **Expressions live in parameters only.** A feature field takes a literal or
  one parameter; a derived value needs a driven parameter.
- **Unit symbols after numbers win.** `3 m/s2` is 3 m divided by the parameter
  `s2`, because `m/s` does not end at a name boundary there. Parentheses or
  spaces make intent explicit.
- **Renaming a parameter does not rewrite expressions** that use it; they fail
  with NotFound until edited (`DocumentExpression_UnknownAndNonParameterNamesAreReported`).
- **Values are evaluated at regeneration.** Between an edit and the next
  regeneration a driven parameter holds its last evaluated value. Setting an
  expression does not evaluate it.
- **Old files are re-evaluated.** A file whose stored value disagrees with its
  expression gets the expression's value at the next regeneration (reported in
  `updatedParameters`); there is no format-version distinction.
- **The parameter-table format** (`ParameterJson.hpp`, a container without a
  document) stores expression text without checking it.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run;
  `rerun-debug.cmd`: the Debug rerun.
- `qualification-times.txt`, `rerun-debug-times.txt`: every step's start, exit
  code and time, the HEAD and the Git tree IDs of the qualified sources.
- `configure-{debug,release,debug-shared}.log`,
  `build-{debug,release,debug-shared}.log`,
  `ctest-{debug,release,debug-shared}.log`: the first run.
- `rerun-{configure,clean,build,ctest}-debug.log`: the Debug rerun.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `compare-regression.py`: the legacy comparison.
- `cli-determinism.txt`: the three configurations' CLI output.
- `expression-values-release.txt`, `values.py`, `values-header.txt`: the
  measured values.

## Final Result

```text
TASK:            P12-PARAM-001 Parameter expression evaluation
IMPLEMENTATION:  Expression (grammar, units, dimensions), ParameterExpressions
                 (names, evaluation, document-wide ordered evaluation), graph
                 edges, driven-parameter rules, regenerator and validation
                 integration, file behaviour
TESTS:           36 unit + 3 process tests; 777/777 in Debug, Release,
                 Debug-shared (and the Debug rerun); 231 related tests x5
VALIDATION:      acceptance expressions exact; driven plate volumes 1.5e-16
                 from the hand-written formula; values bit-identical across
                 configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-PARAM-001/
TODO:            P12-PARAM-001 deliverables ticked
```
