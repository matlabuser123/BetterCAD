# P15-CUSTOM-001 — Custom materials and controlled overrides

**STATUS: PASS.** Qualified on the first attempt across three presets, 2589/2589
each, 0 warnings, 33657 test executions, 0 failures. Full detail at the end.

**TASK** — safe creation and editing of document-local custom materials, with the
immutability and identity of the built-in library preserved. Authorized by the
P15-CUSTOM-001 brief; the override model was already decided by ADR-025.

---

## BASELINE

```text
git status --short   clean
HEAD                 d99c9f97cae4180cbec2c0ba33248e4b5068f996
origin/main          d99c9f97cae4180cbec2c0ba33248e4b5068f996   (equal)
HEAD^{tree}          22d3784c0c48d6af05f8276d5becccab4288b7b0
git log -1           d99c9f9 BetterCAD: implement derived mass properties
suite before         2531 tests
```

## PREREQUISITES

All seven verified complete in `TODO.md` — 119 `[x]`, **0** open boxes between them —
and each records `RESULT: PASS` with evidence:

```text
P15-ARCH-001    [x] 16 boxes   docs/verification/P15-ARCH-001/
P15-UNITS-001   [x] 20 boxes   docs/verification/P15-UNITS-001/
P15-MAT-001     [x] 18 boxes   docs/verification/P15-MAT-001/
P15-MECH-001    [x] 18 boxes   docs/verification/P15-MECH-001/
P15-THERM-001   [x] 14 boxes   docs/verification/P15-THERM-001/
P15-ASSIGN-001  [x] 16 boxes   docs/verification/P15-ASSIGN-001/
P15-MASS-001    [x] 17 boxes   docs/verification/P15-MASS-001/
```

The external build path is still the qualified one: `debug-ext`, `release-ext` and
`debug-shared-ext` are defined in `CMakePresets.json` and all three build trees exist
under `%LOCALAPPDATA%\bc-build`, outside the synchronised folder
(INFRA-QT-DEPLOY-001).

## AUDIT — what already existed

**The audit's headline: most of this milestone was already built or already
structurally impossible to get wrong.** Three things were genuinely missing.

| Capability | Already there? | What P15-CUSTOM-001 did | Risk |
| --- | --- | --- | --- |
| create a user-defined material | YES, `createMaterial` | tested it | low |
| library → document clone | YES, `importLibraryMaterial` | tested it | low |
| snapshot semantics, not live inheritance | YES, ADR-025, structurally | proved the absence of an override model | low |
| edit metadata / mechanical / thermal | YES, three setters | tested non-clobbering and atomicity | low |
| **document → document clone** | **NO** | **added `cloneMaterial`, two overloads** | medium |
| **cross-document clone** | **NO** | same function, two documents | medium |
| **explicit property removal** | **NO** — only whole-struct replacement | **added `removeMaterialProperty`** | medium |
| a per-kind "is it there" query | NO | added `hasMaterialProperty` | low |
| library immutability | STRUCTURAL — `static constexpr`, no setters | 6 compile-fail cases + a field-by-field test | low |
| clone gets a new identity | STRUCTURAL — the ID is not in the definition | 2 compile-fail cases + tests | low |
| duplicate-ID insert refused | YES, `insertObject` → `AlreadyExists` | tested both doors | low |
| old ID never rebinds | YES, `IdAllocator` counts up | tested with an identical replacement | low |
| assignment / mass / mechanical / thermal integration | YES | tested through a custom material | low |
| supplied G or K | **UNREPRESENTABLE** — no slot (ADR-027) | stated; 2 compile-fail cases | n/a |
| base reference / override map | **ABSENT by ADR-025** | stated; 4 compile-fail cases | n/a |

Two of the brief's premises do not hold in this tree. Both are recorded as findings
rather than worked around — see ADVERSARIAL_REVIEW.md F1 and F2 — because in each case
the architecture's answer is the better one and following the brief literally would
have meant damaging it.

## SCOPE

Added: `cloneMaterial` (same-document and cross-document), `removeMaterialProperty`
(mechanical and thermal), `hasMaterialProperty`. 95 lines of header, 192 of
implementation, 1381 of tests.

Not added, deliberately:

- **No override map, no base reference, no `Inherited` property state.** ADR-025 chose
  snapshots. Building the alternative would have created a second material model.
- **No per-property setters** (`setDensity(...)` and so on). The existing whole-struct
  setters already validate, and a second write path would have been a second place for
  validation to drift. Read-modify-write through them is what the tests do, and it is
  what makes multi-property edits atomic by construction.
- Nothing from P15-PROV-001, P15-CMD-001, P15-PERSIST-001 or later.

## THE OVERRIDE MODEL

**It is a snapshot, and ADR-025 chose it before this milestone.** Stated plainly
because the brief offers two models and the wrong answer here would be the milestone's
worst possible outcome:

```text
time T0   library entry L  --import-->  document material D
          D receives L's values AS ITS OWN
          D records L's key and revision as provenance only

time T1   the library ships a corrected L
          D is UNCHANGED, and no lookup happens to notice
```

So for every property of every document material:

```text
state           Known | Unknown | Derived          (ADR-027)
where it lives  in that material, always
inherited?      not a representable state
```

The consequence worth stating in both directions: **a library correction can never
change what a saved document computes**, and equally, a material imported before a
correction does not pick it up. ADR-025 took that trade deliberately, and this
milestone did not revisit it.

Because there is no inheritance, the brief's items 15, 40 and 48 — override removal
versus masking, reporting a property's Local/Inherited source, clearing an override
versus masking an inherited value — **do not arise**. They are not skipped; the states
they distinguish cannot exist. Four compile-fail cases prove that rather than leaving
it as prose.

### No accidental partial override

In a snapshot model the gate takes this form: an edit to one property touches exactly
that property, and nothing else moves, becomes Unknown, or falls back to anything.
Tested by comparing the definition **member by member** after a removal —
designation, standard, family, notes, origin, all eight remaining mechanical
properties, and the whole thermal half — and again in the other direction for a
thermal removal, including the density, which thermal consumers read but which lives in
the mechanical half and must not be reachable from a thermal edit.

## CUSTOM MATERIAL CREATION AND IDENTITY

A material may be created with nothing known but a name: every stored property
Unknown, checked over the whole enumeration rather than a hand-written list, and
Unknown rather than zero.

Identity survives every kind of edit — rename, metadata, mechanical, thermal, and
**removal** — in one test that then checks the ID, the material count and the
enumeration. A second test renames four times.

**Names are unique; designations are not.** A second material named "Steel" is
refused with `AlreadyExists`, consumes no ID and leaves the first untouched;
`Document::uniqueName()` is the supported way to get a free one, so the document never
invents a name the user did not choose. Two materials *designated* "Steel" are
legitimate and both come back from `findMaterialsByDesignation`.

## LIBRARY CLONE, CONTENT AND IDENTITY

```text
source id   material:1   (an imported material, or any document material)
clone id    material:2   always a fresh allocation
```

| | Library L | Import at T0 | Clone at T0 | Clone after edits | L after edits |
| --- | --- | --- | --- | --- | --- |
| identity | no ObjectId at all | `material:1` | `material:2` | `material:2` | still none |
| designation | Aluminium 6061-T6 | copied | copied | changed | **unchanged** |
| standard | ASTM B221 | copied | copied | changed | **unchanged** |
| family | Aluminium Alloy | copied | copied | changed | **unchanged** |
| notes | "Metadata only…" | copied | copied | changed | **unchanged** |
| origin | n/a | L's key rev 1 | same key | changed | **unchanged** |
| density | **not present** | Unknown | Unknown / copied | changed | n/a |
| E | **not present** | Unknown | Unknown / copied | changed | n/a |
| nu | **not present** | Unknown | Unknown / copied | changed | n/a |
| k | **not present** | Unknown | Unknown / copied | changed | n/a |
| cp | **not present** | Unknown | Unknown / copied | removed | n/a |
| alpha | **not present** | Unknown | Unknown / copied | removed | n/a |
| yield known/unknown | **not present** | Unknown | pattern preserved | removed | n/a |
| hardness + scale | **not present** | Unknown | value AND scale copied | changed | n/a |
| reference temperature | **not present** | Unknown | preserved | preserved | n/a |

The "not present" column is the finding: **the built-in library carries metadata only**
(ADR-028 — a property value needs a recorded source, and P15-MAT-001 declined to invent
numbers). So the property rows are exercised on a **document-local** source, which is
where properties live in this architecture and is the `cloneMaterial` workflow the
brief asks for at item 26. The library rows are exercised on the real library.

`L after edits` is checked field by field, by `key()`, by `operator==`, and by
confirming the table is still the same four entries in the same order — not one spot
check.

## DEEP COPY AND STORAGE INDEPENDENCE

**Audited, not assumed.** `MaterialDefinition` is:

```text
std::string   designation, standard, family, notes
std::optional<MaterialLibraryKey>   origin      (two owning strings + int)
MechanicalProperties  mechanical                (9 MaterialProperty of value types)
ThermalProperties     thermal                   (5 the same)
```

A grep of both property headers finds **no pointer, reference, view, span or smart
pointer in any member**; the only `<span>` and `<string_view>` includes are for free
functions' return types. Copying a definition is therefore deep by construction, and
`MaterialLibraryKey` owning its strings is a deliberate P15-MAT-001 deviation from
ADR-025's `string_view` sketch made for exactly this reason.

Proven at runtime the hard way: the cross-document test **destroys the source
document** inside a scope and then reads every field of the clone, including its
hardness scale and thermal conductivity. A view or a pointer would dangle there.

Both directions are tested: editing the clone leaves the source equal to its starting
definition, and then editing the source leaves the clone equal to what it had become.

## LIBRARY IMMUTABILITY AND THE MUTABLE-ALIAS AUDIT

Every mutation path a caller might reach for, closed at compile time:

```text
entry->setDesignation(...)                  no setter exists
entry->designation_                         private
LibraryMaterial& = builtInMaterials()[0]    span<const>, discards qualifiers
MaterialDefinition& = *entry                no conversion
document.addObject(*entry)                  not a DocumentObject, has no ObjectId
entry->mechanical()                         no property storage on an entry at all
material->setDefinition(...)                findMaterial returns a const pointer
```

Six compile-fail cases, plus the runtime field-by-field comparison above. A runtime
test can only show that the library did not change; these show that it cannot.

## PROPERTY EDIT, REMOVAL, AND UNKNOWN VERSUS ZERO

```text
remove density   -> Unknown, succeeds, consumer fails with "has no density"
density = 0      -> REFUSED, old value still 2700, revision unmoved
remove E         -> Unknown, and the derived G becomes Unknown with it
E = 0            -> REFUSED
E = -1 GPa       -> REFUSED
remove G or K    -> REFUSED: derived and never stored; the diagnostic says to
                    remove the modulus or the ratio instead
```

Removal takes the **whole property record**, including the reference temperature the
value was specified at — there is nothing left to have recorded a temperature for. A
removal that changes nothing returns `false` and does not bump the revision.

`remove(ShearModulus)` refusing rather than silently doing nothing is the useful
behaviour, and the test then shows the documented alternative works: removing the
Poisson ratio does make the shear modulus unavailable.

**The unknown-is-not-zero gate was mutation-tested.** Changing `clearMechanical` to
store a zero density instead of Unknown made **6 test cases fail**.

## INVALID EDIT ATOMICITY

Nine invalid values, each required to leave the **whole definition** equal to what it
was and the document revision unmoved: E negative, density zero, density negative,
yield zero, nu = 0.5, nu = -1.5, k = 0, cp negative, and a mixed struct with valid
density + invalid E + valid yield where **none of the three** lands.

These setters replace a whole struct, so multi-property edits are transactional by
construction rather than by promise — which is what the mixed-struct case pins.

A related guarantee: no edit can store a property that **claims** to be `Derived`,
through a setter or through creation, so a clone cannot be seeded with one.

## DUPLICATE NAMES AND OLD-ID NO-REBIND

```text
Library L         designated "Aluminium 6061-T6"
Custom A          designated "Aluminium 6061-T6"
Custom B          designated "Aluminium 6061-T6"
-> all three resolve by identity; findMaterialsByDesignation returns all three
-> only the import has an origin key, which is what tells them apart

delete A; create B with identical name, metadata and properties
-> B.id > A.id, the assignment still names A and is Unresolved, findMaterial(A) null
```

No library-first, local-first or first-name-match preference exists to be wrong,
because no function returns "the" material for a designation.

## ASSIGNMENT, MASS, MECHANICAL AND THERMAL INTEGRATION

```text
assignment    survives rename + metadata + mechanical + thermal + removal;
              still Resolved, same MaterialId, and the consumer sees CURRENT values
mass          2700 kg/m^3 x 30000 mm^3 = 0.081 kg
              density -> 7850           = 0.2355 kg, same volume, same identity,
                                          inertia in the same ratio
              remove density            -> diagnostic naming the material, not zero
mechanical    G = E/(2(1+nu)) follows an E edit and then a nu edit;
              requireLinearElasticConstants agrees
thermal       requireThermalConductivity follows an edit;
              the transient set spans both halves, and removing the density
              breaks it while leaving the steady one working
incomplete    assignment SUCCEEDS on a material with no properties;
              massProperties then fails with "no density", and the elastic
              constants fail separately naming the modulus
```

That last row is the point the brief makes at item 50: assignment and property
completeness are separate concepts, and they fail separately and specifically.

## COPY / CLONE SEMANTICS

`Material::clone()` is the `DocumentObject` virtual that undo/redo uses and **keeps the
identity** — which is exactly why it is not the way to make a new material. Both doors
are closed and tested:

```text
addObject(original->clone())     refused: "already has object:1; use insertObject()"
insertObject(original->clone())  refused: AlreadyExists, "already in use"
cloneMaterial(document, id, n)   the supported route; fresh ID, deep copy
```

Nothing merged and nothing overwrote in either refusal: one material before, one
after, values intact.

## CROSS-DOCUMENT POLICY

**Supported.** `cloneMaterial(destination, source, id, name)` is the primary overload
and the same-document form delegates to it. The destination holds no reference to the
source afterwards — proven by destroying the source document and then using the clone.

Passing one document as both arguments is safe and the implementation says why: the
definition is copied out before `createMaterial` touches the document, and the source
pointer is never read afterwards, so the answer does not depend on whether the object
map invalidates pointers on insert.

## PERSISTENCE READINESS

Nothing in the canonical state depends on a pointer, an address or a container
position, so all of it is serializable when P15-PERSIST-001 arrives.

Until then the gap stays **loud**. A new io test takes a cross-document clone, removes
a property from it, and saves **both** documents: each must fail with a diagnostic
naming the type, and nothing may be left half-written. A writer taught to skip a
material it considered "just a copy", or to report success for the document that did
not originate the material, would fail there.

## PROVENANCE READINESS

What is representable today: `origin` (library, entry, revision) for an imported
material, absent for a from-scratch one; and `referenceTemperature` per property.

What is not: which *document* material a clone came from. A clone of an import carries
the same origin as the import, because origin records where the **values** came from
and cloning does not change them — so both statements are true and neither is a lie,
but the two cases are indistinguishable. Recorded as a limitation rather than
half-built: it needs a provenance model that separates a library source from a document
one, which is P15-PROV-001's subject. The state is *representable* — a clone is an
ordinary document material with its own ID — so a `clonedFrom` field can be added
beside `origin` without migrating anything.

## ADVERSARIAL REVIEW

**PASS — 26 attacks, 5 findings.** Two were defects in this milestone's own work, both
fixed. Three are recorded because the brief's premises do not hold in this tree.
**No production defect survived.** Full text:
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
F1  two materials cannot share a NAME; the brief assumes they can   ASSUMPTION, tested both ways
F2  the library has no property values, so the brief's central
    clone fixture cannot be written                                 ASSUMPTION, fixture relocated
F3  hasMaterialProperty answered the storage question for a
    derived kind, which is true and misleading                      DEFECT, fixed in design
F4  a test asserted PoissonRatio::of(0.5) refuses construction;
    it carries no range check, deliberately                         DEFECT, fixed
F5  a clone of an import is indistinguishable from a direct import  LIMITATION, recorded
```

One gate was **mutation-tested** rather than only asserted: making removal store a
zero instead of Unknown failed 6 test cases, and a field-by-field clone that dropped
the thermal half and the origin failed 5 assertions across several tests.

## DETERMINISM

```text
clone content                deterministic: two fresh documents built the same way
                             give equal definitions AND equal IDs
known/unknown pattern        compared over the whole property enumeration
property ordering            mechanicalPropertyKinds/thermalPropertyKinds are spans
                             over static arrays in declaration order
material enumeration         materialIds walks the document's ordered object map,
                             ascending ID; checked 8 times in a row
diagnostics                  std::format over stable inputs
```

No unordered container, hash, pointer ordering, wall clock, locale or random seed is
reachable from any of it.

## FULL REGRESSION

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2589/2589      0
release-ext            0        0      0          0        2589/2589      0
debug-shared-ext       0        0      0          0        2589/2589      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   12945 = 2589 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   12945 = 2589 x 5, 0 failures
qualification finished 02:27:33, 0 stage(s) failed
```

2589 = the 2531 of P15-MASS-001 plus the 58 added here, and all three presets
discover the same 2589. Both repeat stages show exactly 12945 passing executions,
which is 2589 x 5 with nothing skipped.

**33657 test executions in total, 0 failures**: three full suites plus two five-fold
repeats. Qualified on the **first attempt**, 23:22:34 to 02:27:33 (3h 05m).

The claims that matter most here are ABSENCES, so they were checked for presence in
**all three** presets rather than only where they were developed:

```text
all 20 compile_fail.custom cases                              passed x3
two materials cannot share an object name                     passed x3
removing a derived modulus is refused                         passed x3
a clone is deeply independent in every property               passed x3
a cross-document clone outlives its source document           passed x3
```

`debug-shared-ext` was worth watching in particular: an absence is only as good as
the whole build agreeing about it, and a shared build is where dll-import linkage of
header constants has bitten this repository before.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that
does not exist: 3 stages failed, exit 3. So a passing run is evidence rather than a
harness that cannot fail.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot
replace" across every log. No controlled rerun was needed.

A pre-qualification smoke run of the whole suite on `debug-ext` reported 2588/2589.
The one failure was `cli.new.unicode-path`, and the cause was the invocation, not the
tree: that test needs code page 65001, which `qualify.cmd` sets and an ad-hoc `ctest`
does not. Verified by rerunning it under `chcp 65001`, where it passes, and by all
three qualified presets passing it.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first
build and after the last test run are identical:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include 2e2692d641e365057b3040a0e0057ba6050c6ecb
src 9fc16cbd14cdab08bbef0009c7c248c5c8c36ce9
tests 77d4c574ee0939028032ab1563daffdf46620a6f
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint and
cannot affect the executable or the tests.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set. The change
is purely additive — 380 insertions, **0 deletions** — so no existing code was
recompiled differently and no existing warning was suppressed.

## KNOWN LIMITATIONS

1. **A clone of an imported material cannot be distinguished from a direct import.**
   See PROVENANCE READINESS and F5. P15-PROV-001's subject.
2. **The built-in library still carries no property values.** Not a P15-CUSTOM-001
   omission — ADR-028 requires a recorded source, and providing both is the job of the
   milestone that populates the library. The consequence for this milestone is that an
   imported material starts fully Unknown, which is tested rather than hidden.
3. **No per-property setters.** Editing goes through whole-struct replacement, so a
   caller changing one property reads, modifies and writes. That is what makes the
   edits atomic, but it is more verbose than `setDensity(...)` and a future milestone
   may add convenience wrappers over the same validation.
4. **A material still cannot be saved** (P15-PERSIST-001). Saving fails loudly and is
   tested to; nothing is dropped. Custom materials are not usable end to end until
   then.
5. **`removeMaterialProperty` is per-property, not transactional across several.**
   Removing three properties is three operations, each atomic. Nothing here promises a
   multi-removal transaction, and the header does not claim one.

## RESULT

```text
TASK:            P15-CUSTOM-001
IMPLEMENTATION:  cloneMaterial (same-document and cross-document),
                 removeMaterialProperty (mechanical and thermal, refusing the
                 derived kinds), hasMaterialProperty. 380 lines added, 0 removed:
                 nothing qualified was modified.
TESTS:           58 added (37 custom, 1 persistence, 20 compile-fail); 2531 -> 2589
VALIDATION:      the snapshot override model proved by 4 compile-fail absences;
                 library immutability by 6 more plus a field-by-field comparison;
                 deep copy audited (no pointer, view or smart pointer in any
                 member of the canonical state) and proven by outliving the source
                 document; two gates mutation-tested
RESULT:          PASS
EVIDENCE:        this directory; qualification/ for all 20 stage logs and the tree
                 fingerprints
TODO:            updated -- 13 boxes ticked
```

**What this milestone mostly did was prove that the architecture already got it
right.** Three functions were missing and were added. Everything else the brief
asked for was either already built (creation, import, editing, duplicate-ID
rejection, old-ID no-rebind) or structurally impossible to get wrong (library
immutability, identity in a clone, an override map) — and for the second group the
work was to turn "impossible" from an argument into 12 compile-fail cases and a
runtime audit.

Three of the brief's premises did not hold in this tree, and none was papered over:
names are unique where the brief assumed they duplicate, the library has no property
values where the brief assumed it does, and the override model was already chosen
where the brief offered two. Each is recorded as a finding with what was done
instead.

## REVISION

First revision. Written against the tree qualified above; no source or test file
changed after the freeze.
