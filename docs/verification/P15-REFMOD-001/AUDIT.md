# P15-REFMOD-001 — reference-model architecture audit

Done before any design decision and before any code, because the brief forbids a second
reference-model framework and because every milestone in P15 has found the work already
done.

## Baseline

```text
branch               main
HEAD / origin/main   c676d40224e48cbb34053b9f9c2dec2edab0c75b (equal)
HEAD^{tree}          d42100389daf422beaf789041f241e3d239dd606
working tree         clean
predecessors         12 of 12 PASS
suite                2764 tests
build path           $penv{BETTERCAD_BUILD_ROOT}/<preset>, the -ext presets, intact
```

## What exists

Three reference suites already, from P11/P12 (parts), P13 (assemblies) and P14 (drawings):
38 files in `examples/reference_models/`, 29 in `tests/reference/`.

| Existing pattern | Reusable? | Required P15 extension | Risk |
| --- | --- | --- | --- |
| A model per file: a struct of IDs + a `build...ReferenceModel()` function | **Yes** | six material model builders | None |
| `ReferenceModels.hpp` + `Catalog.cpp`; **P13 and P14 each added their OWN header and catalog** | **Yes** | a fourth: `MaterialReferenceModels.hpp` + `MaterialCatalog.cpp` | None — the precedent is explicit and twice-followed |
| `main.cpp`, three suite loops with `--out` and `--drawings` | **Yes** | a fourth loop and a `--materials` filter | None |
| `BuildSupport.hpp`: `ModelBuilder`, `SketchBuilder`, `fixedDocumentId` | **Yes** | build box / cylinder / tube through the public APIs | None |
| `tests/reference/Analytic.hpp`: Green's theorem, Pappus, Gauss–Legendre, **nothing from BetterCAD or the kernel** | **Yes** | closed-form inertia, which it does not have | Low — the brief prescribes the formulas |
| `ReferenceTestSupport.hpp`: `kRel = 1e-10`, `kPositionMm = 1e-9`, and a check that prints expected / actual / abs / rel | **Yes** | the same shape for mass, centroid and inertia | None — this IS the analytical-table requirement, already established |
| `Fingerprint.cpp` plus `compare` and `toText` at 17 significant digits | **Yes** | determinism across builds and presets | None |
| `AllModelsTests.cpp`: build-in-one-process, regenerate-after-change, saved-models-match-builders, AnalyticToolkit | **Yes** | the material models join them | None |
| `bettercad_add_process_test`: PROCESS A runs the example runner with `--out`, PROCESS B+ drive `bettercad-cli` on what it wrote | **Yes** | the multi-process CLI workflow (brief section 34) | None — this is exactly that mechanism |

**Nothing in this milestone needs a new mechanism.** `Analytic.hpp` alone already satisfies
the brief's hardest requirement (section 35): expected values computed from exact
descriptions of the parts, with no call into BetterCAD, the kernel or the CLI.

## Architectural limits — verified, not assumed

These decide what several fixtures in the brief can honestly be. Each was checked in the
headers, not inferred from the prose of an earlier milestone.

### 1. ONE material per document

`Document` holds a single `std::optional<MaterialId> materialAssignment_`. There is no
container. **A document cannot carry two materials in force**, so "Part A to Material A,
Part B to Material B" (section 14) is not a single-document model.

### 2. NO per-occurrence material

`ComponentDefinition` has no material field — grepped, absent. An assembly's occurrences
cannot differ in material, and P15-ASSIGN-001 already has a compile-fail case proving the
field is absent rather than merely unused. Cross-document part references, which is what
would make per-part materials possible in an assembly, need ADR-003 and are deferred.

### 3. NO assembly mass aggregation

A case-insensitive search of `include/` and `src/` for an assembly mass, an aggregate mass,
a total mass or a mass sum returns nothing. Core provides `shiftedFromCentroid` and
`transformed` for ONE body and no aggregation over bodies. P15-CLI-001 declined to add it in
the CLI for exactly this reason.

**Consequence for RM-MAT-04.** The brief's sections 16 and 18 both say "if in scope", and
section 67 says to mark unsupported cells `N/A` with an explanation rather than PASS. So
RM-MAT-04 is a multi-material **document set** — two parts, two documents, two densities,
each independently validated — plus an assembly document that proves the assignment is
document-level and that two occurrences of one part definition both exist. Aggregate mass,
centre of mass and inertia are marked N/A against these three findings. Summing the two
masses in a test would validate the test's own arithmetic, not the product.

### 4. Assignment is configuration-INDEPENDENT (ADR-026)

So section 28's first branch applies: a model with two configurations, switched A to B to A,
must keep its material identity and assignment. That case is **in scope and testable**, not
N/A.

A second, sharper case comes free: a configuration that overrides a PARAMETER makes
`partMassProperties` refuse, because the carried regeneration defect means the volume in
hand would be the base configuration's. That refusal must hold here too.

### 5. Object names are unique; DESIGNATIONS are not

Section 44 asks for two materials both named "Steel". Two objects cannot share a name —
names are unique across objects and parameters, re-checked on every rename. The real
duplicate-label case is the **designation**, which is free text and legitimately duplicated,
and which P15-CLI-001 already made refuse rather than resolve. The fixture is built on
designations, and the impossibility of duplicate names is stated rather than worked around.

### 6. A supplied shear modulus cannot exist, so section 52's fixture must change

Section 52 suggests a material with E, nu and a supplied G inconsistent with them. **There is
no slot for a supplied G** (ADR-027) — that absence is a qualified invariant. A genuine,
detectable inconsistency does exist: `mechanicalInconsistencies` reports an **ultimate
tensile strength below the yield strength**. Queried for `FeaYieldStrength`, where both are
required and present, that gives `CompletenessState::Invalid`. That is the `Invalid` fixture
section 51 asks for, and it is the product's own rule rather than an invented one.

### 7. The consumer requirement matrix, read from the source

```text
MassProperties                density
FeaLinearStatic               E, nu
FeaLinearStaticWithGravity    density, E, nu
FeaYieldStrength              E, nu, yield
ThermalSteady                 k
ThermalTransient              density, k, cp
ThermoMechanical              E, nu, alpha
```

This is what RM-MAT-06's expectations are written against — not a guess at what an FEA
consumer "should" need. Note `ThermalSteady` needs the conductivity ONLY, and
`FeaLinearStatic` never touches a density.

### 8. The built-in library has four entries, all metadata-only

`al-6061-t6`, `steel-s235jr`, `stainless-304`, `abs`, each with a designation, a standard, a
family, and the note "Metadata only: this entry carries no property values."

So RM-MAT-05 **can** do the genuine chain section 48 asks for — import a library entry, give
the document-local copy values, clone it, edit the clone — and the library source is
structurally unchangeable because it is a compiled-in constant. That the entries carry no
values is a recorded P15-MAT-001 limitation, not something this milestone can fix.

## Synthetic engineering data

Every property value in this suite is **TEST / SYNTHETIC ENGINEERING DATA**, chosen to make
closed-form arithmetic exact and legible. Values are round and physically plausible, and each
material says so in its own `notes` field so that no reader can mistake one for a sourced
datasheet figure. The library entries these models import carry real designations and
standards and no values, which is exactly the separation P15-MAT-001 built.

## Consequence

New files, following the names already in use:

```text
examples/reference_models/MaterialReferenceModels.hpp    the catalog
examples/reference_models/MaterialCatalog.cpp            the dispatch
examples/reference_models/MaterialModels.cpp             the six builders
tests/reference/MaterialModelsTests.cpp                  the gates
```

Modified additively: `main.cpp` (a fourth loop), `examples/reference_models/CMakeLists.txt`,
`tests/CMakeLists.txt`, `tests/reference/Analytic.hpp` (closed-form inertia).

**No second reference-model framework, no new analytical toolkit, no new process harness.**
