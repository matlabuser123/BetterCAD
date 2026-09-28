# P15-PERSIST-001 — Persisting canonical material engineering data

**STATUS: PASS**, on the second qualification attempt. 2716/2716 in each of three
presets, 0 warnings, 35308 test executions, 0 failures. The first attempt also passed
every stage and was voided by me for a single blank line at a file's end -- see
[qualification-void/](qualification-void/README.md) and FULL REGRESSION. Full detail at
the end.

**TASK** — a deterministic, backward-compatible persisted form for BetterCAD's canonical
material intent: identity, metadata, properties, assignments, provenance and custom
materials. Nothing derived.

---

## BASELINE

```text
git status --short   clean
HEAD                 5f3ff7be429771722593f857b44490ab9cc707f5
origin/main          5f3ff7be429771722593f857b44490ab9cc707f5   (equal)
HEAD^{tree}          926850c7ea6f258dcd2e4e67c9ddd53b3f581317
git log -1           5f3ff7b BetterCAD: add material commands and exact undo redo
suite before         2685 tests
```

## PREREQUISITES

All ten verified complete in `TODO.md` — 161 `[x]`, **0** open boxes between them —
each recording `RESULT: PASS` with evidence:

```text
P15-ARCH-001 16   P15-UNITS-001 20   P15-MAT-001 18   P15-MECH-001 18
P15-THERM-001 14  P15-ASSIGN-001 16  P15-MASS-001 17  P15-CUSTOM-001 13
P15-PROV-001 14   P15-CMD-001 15
```

The external build path is still the qualified one: all three `-ext` presets are defined
and their build trees exist under `%LOCALAPPDATA%\bc-build`, outside the synchronised
folder (INFRA-QT-DEPLOY-001).

## EXISTING PERSISTENCE AUDIT

| Persisted domain | Schema location | Identity strategy | Malformed data | Reusable for P15? |
| --- | --- | --- | --- | --- |
| document header, parameters | `DocumentJson.cpp` | UUID; `last_allocated_id` restores the counter | `requireObject` with an allowed-key list — **unknown keys rejected** | YES |
| objects (sketch, features, datums, components, mates, sheets, views) | one `*Json.cpp` per kind, dispatched by `dynamic_cast` | `{id, type, name, data}`; `restoreObject` keeps the ID | unknown `type` → parse error naming it | **YES — the pattern a material follows** |
| configurations | `DocumentJson.cpp`, optional root section | keyed by ParameterId | absent = base configuration | **YES — the pattern the assignment follows** |
| quantities | everywhere | — | — | bare SI doubles, `.si()` |
| command history | **not persisted at all** | — | — | out of scope (item 32) |

**Four things the audit settled, each of which would otherwise have been work:**

**1. No version bump is needed, and that is the format's own rule.**
`DocumentFile.hpp` states it: *"adding a kind, an object type or an optional field needs
no bump. Changing what an existing field means needs one."* P15 adds an object type and
an optional section, so the version stays **2**.

**2. Backward and forward compatibility are already defined.**
*"new reader, old file → loads. An absent section means 'none of those'. old reader, new
file → REFUSES. An unrecognised object type is a parse error naming it, never a silent
skip, so no data is lost."*

**3. Atomic load is structural.** `documentFromJson` returns `Result<Document>` — it
builds a **new** document, so a failed load cannot reach an existing one.

**4. Duplicate-ID rejection and allocator restoration already exist.** `insertObject`
refuses an ID in use with `AlreadyExists`; `last_allocated_id` is persisted and the
loader refuses a value below an ID in use.

So the work was the material's own mapping, the assignment section, and proving all of
the above holds for materials rather than assuming it.

## SCHEMA VERSION

**Unchanged at 2.** `kOldestReadableDocumentVersion` stays 1. Tested: a version-1 file
loads and gains no materials, a version-3 file is refused by name, and a P15 file says
`"version": 2`.

## CANONICAL MATERIAL SCHEMA

A real serialized document is in [schema-sample.json](schema-sample.json). The material
section of it, verbatim:

```json
{
  "id": 1,
  "type": "material",
  "name": "Steel",
  "data": {
    "designation": "Steel S235JR",
    "standard": "EN 10025-2",
    "family": "Carbon Steel",
    "notes": "Synthetic fixture, not measurements.",
    "origin": { "library": "bettercad", "entry": "steel-s235jr", "revision": 1 },
    "mechanical": {
      "density":        { "value": 7850.0, "at": 293.15 },
      "youngs_modulus": { "value": 210000000000.0 },
      "poisson_ratio":  { "value": 0.3 },
      "yield_strength": { "value": 235000000.0 },
      "hardness":       { "value": 60.0, "scale": "HRC" }
    },
    "thermal": {
      "thermal_conductivity": { "value": 50.0 },
      "melting_temperature":  { "value": 1811.0 }
    },
    "provenance": {
      "mechanical": [
        { "kind": "measured", "source": "Synthetic test report",
          "revision": "Rev A", "date": "2024-03-17", "property": "density" }
      ]
    }
  }
}
```

and the assignment, a root section beside `configurations`:

```json
"material_assignment": { "material": 1 }
```

Six decisions, each with a reason:

**ABSENT MEANS UNKNOWN.** A property nobody has measured is omitted, not written as
`null` or `0`. It is the one representation that cannot be misread — there is no number
in the file to mistake for a measurement. The sample above has five mechanical
properties because five are known; the other four are simply not there.

**SI, ALWAYS.** `210 GPa` is `210000000000.0`. A Quantity stores SI and converts only at
a boundary (P15-UNITS-001), so no display preference can become engineering authority.
The file contains no unit symbols at all.

**THE FILE'S KEYS ARE NOT THE DIAGNOSTIC STRINGS.**
`toString(MechanicalPropertyKind)` returns `"Young's modulus"` — display text, with an
apostrophe, that a later milestone may reword or translate. `MaterialJson.cpp` therefore
carries its own explicit key table, so rewording a diagnostic cannot break a saved
document. See ADVERSARIAL_REVIEW.md F3.

**ENUMERATIONS ARE STRINGS.** `"measured"`, `"HRC"`. A string says what it means and an
unrecognised one is rejected by name; an integer would silently become a different case
the day an enumerator is inserted in the middle.

**PROVENANCE IS KEYED BY PROPERTY NAME, NOT BY POSITION.** Each record carries
`"property": "density"`, so no reordering of the file or of the enumeration can reattach
a citation to the wrong value.

**ONE DENSITY.** It is written in `mechanical` only. Thermal consumers read that same
value (P15-THERM-001); two persisted copies could be made to disagree by a hand edit.

## MATERIAL IDENTITY

`restoreObject` keeps the ID, the mechanism every object kind already used. Tested with
three materials so the ID under test is not merely the first, comparing the whole
`materialIds()` list and the object name.

## ALLOCATOR RESTORATION

`last_allocated_id` is persisted; the loader refuses a value below an ID in use and then
calls `reserveIdsThrough`. Tested on a file whose IDs are deliberately
**non-contiguous** — a material was deleted before saving — and a material created after
load collides with none of the three, the deleted one included.

## METADATA

`designation`, `standard`, `family`, `notes`, written only when non-empty, plus the
object `name`, which is identity's neighbour and not identity. Two materials sharing a
designation round-trip as two materials.

## MECHANICAL PROPERTIES

```text
density  youngs_modulus  poisson_ratio  yield_strength
ultimate_tensile_strength  ultimate_compressive_strength  shear_strength
elongation  hardness (value AND scale)
```

**No key exists for a shear or bulk modulus**, and that absence is the design: ADR-027
gave `MechanicalProperties` no slot for either, so a file cannot claim one even
deliberately. Hardness carries its scale; all four scales are round-tripped individually
so the mapping is not accidentally constant.

## THERMAL / PHYSICAL PROPERTIES

```text
thermal_conductivity  specific_heat_capacity  thermal_expansion
melting_temperature   electrical_resistivity
```

Density is **not** here — see the schema decisions. `electrical_resistivity` is written
in ohm metres and read back into `materials::ElectricalResistivity`, its own type
(ADR-029), never a bare double. `melting_temperature` and every `"at"` are **absolute**
kelvin; tested with 1811 K and 293.15 K by exact value, because an interval reading
would not preserve them.

## UNKNOWN PROPERTY STATE

Tested over both enumerations: the known/unknown pattern after load equals the pattern
before, and an unknown property is `isUnknown()` with `value()` empty — never 0, NaN or a
default. The file is also checked to *lack* the keys for the unknown ones.

**Mutation-tested.** Writing a property as `0` when Unknown fails **14 test cases**.

## UNITS

`210 GPa` persists as `2.1e11` and is recovered in GPa, in MPa and bit-exactly in SI.
The file contains no `"GPa"` or `"MPa"`. Demanding values — `210e9`, `0.33`, `167.3`,
`12e-6`, `2.65e-8` — survive **two** round trips compared with `==` rather than a
tolerance, which is what catches a formatting loss that only shows on re-save.

## ASSIGNMENTS

One optional `MaterialId`, written as `{"material": <id>}` beside `configurations`, and
only when there is one — so a pre-P15 document is written back exactly as it was. The
**effective** material is derived and never written.

Tested: round-tripped by identity; resolved correctly when two materials share a
designation; preserved through a rename with no assignment rewrite; and absent from the
file when there is no assignment.

## UNRESOLVED REFERENCES

An assignment naming a deleted material is the legitimate `Unresolved` state
(P15-ASSIGN-001) and is written as-is. Erasing it on save would lose the user's intent;
rebinding it would be worse.

```text
A = Steel, B = Steel, P -> A, delete A
save/load   -> still unresolved A, effectiveMaterial null, B untouched
save/load   -> still unresolved A        (the second trip is the point: a loader that
                                          dropped it would show that on re-save)
```

**Mutation-tested.** A loader that fell back to a same-designation material fails 3 test
cases.

The loader deliberately does **not** require an assignment to resolve — refusing one
would turn a valid document into an unopenable one. A *structurally* malformed
assignment is rejected: a string, a negative number, a wrong key, an extra key, an
array. That is item 48's distinction.

## DUPLICATE-NAME PROOF

Names are unique; designations are not. Two materials designated `"Steel S235JR"` with
different moduli round-trip as two materials, each keeping its own values, and the
assignment names one of them by ID.

## PROVENANCE

All eight source kinds round-trip individually. The whole `MaterialProvenance` is
compared as one value, then `kind`, `source`, `reference`, `revision`, `condition`,
`notes` and `date` individually. The date is ISO 8601 in the file (`"2024-03-17"`),
locale-independent by construction.

A known value with **no** provenance stays known with no provenance, and the file has no
`provenance` section at all. An empty record is rejected on read, because an empty and an
absent record mean the same thing and one state must not have two representations.

## CUSTOM MATERIALS

A custom material is an ordinary document material, so it persists as one. Tested by
giving an imported material and its clone **different** densities — values the library
does not have at all — and checking each survives with its own.

## LIBRARY SNAPSHOT / ORIGIN

The origin key is **provenance, not a reference** (ADR-025). Nothing consults the library
at load time, and the strongest available proof is in the suite: a document whose origin
names `"acme"/"unobtainium-7" rev 42` — an entry in no library — loads completely with
its values intact. A missing, renamed or newer catalogue cannot make a file unreadable
or change what it computes.

## DERIVED-STATE EXCLUSION

A document with geometry, a material, an assignment and a **computed** mass is
serialized, and the text checked against 24 forbidden substrings:

```text
"mass"  "volume"  "centre_of_mass"  "center_of_mass"  centreOfMass  centerOfMass
"inertia"  inertia_tensor  "ixx" "iyy" "izz" "ixy" "ixz" "iyz"
shear_modulus  bulk_modulus  derived_shear  derived_bulk
completeness  fea_ready  thermal_ready  missing_properties
effective_material  resolved_material
```

The mass is computed **before** the save, so nothing is absent merely because it was
never asked for. The same test also rejects machine-specific paths: `C:\Users`,
`C:/Users`, `bc-build`, `OneDrive`.

### MASS RECOMPUTATION

```text
20 x 30 x 50 mm = 3e-5 m^3, density 1000  ->  0.03 kg
save / load                               ->  0.03 kg   (recomputed)
density -> 2000 after load                ->  0.06 kg
depth -> 100 mm after load                ->  0.12 kg
```

### G/K RECOMPUTATION

Available before the save, so it could have been written. After load the derived shear
modulus is still `isDerived()` and not `isKnown()`, and the file never names it.

### COMPLETENESS RECOMPUTATION

`ThermalTransient` reports the same missing conductivity before and after; supplying it
after load makes the report `Ready`, so it is genuinely rebuilt rather than replayed.

### EFFECTIVE MATERIAL

Only the direct assignment is written. An unresolved assignment loads as unresolved with
`effectiveMaterial` null — which a persisted cache would have had to contradict.

## MALFORMED INPUTS

Nineteen cases, each differing from a valid file in exactly one thing. Every one is
rejected, the diagnostic names the problem, and the valid file still loads afterwards:

| input | expected | actual |
| --- | --- | --- |
| `density: 0` | reject | "density" |
| `youngs_modulus: -1` | reject | "modulus" |
| `poisson_ratio: 0.5` | reject | "Poisson" |
| `thermal_conductivity: -10` | reject | "conductivity" |
| `melting_temperature: -1` | reject | "melting" |
| unknown mechanical key | reject | "unknown mechanical property" |
| unknown thermal key | reject | "unknown thermal property" |
| unknown top-level key | reject | names the key |
| `scale: "HRZ"` | reject | "hardness scale" |
| hardness with no scale | reject | "scale" |
| `kind: "rumour"` | reject | "source kind" |
| `date: "17/03/2024"` | reject | "ISO 8601" |
| `date: "2026-02-30"` | reject | "ISO 8601" |
| provenance for an unknown property | reject | "unknown property" |
| two provenance records for one property | reject | "more than once" |
| a value that is a string | reject | "number" |
| a property that is not an object | reject | "object" |
| half-filled origin key | reject | "library" |
| origin revision 0 | reject | "revision" |
| duplicate object id | reject | "already in use" |
| material with no `id` | reject | names the field |
| `NaN` / `Infinity` / `-Infinity` | reject | not JSON |
| `1e400` (overflows to infinity) | reject | "finite" |

**Validation is not duplicated.** `materialFromJson` hands the definition to
`features::Material::create`, the same entry point a caller uses, so a file goes through
exactly the validation a user does. A deserializer with its own weaker checks is how
invalid engineering data gets into a model.

## ATOMIC LOAD

Structural: `documentFromJson` builds a new `Document`. Tested through the file API — a
good file saved, a bad one attempted — with the original's material and assignment
checked intact and the good file still loading.

## BACKWARD COMPATIBILITY

| previous schema | loads? | migration? | materials fabricated? | geometry preserved? |
| --- | --- | --- | --- | --- |
| version 2, no material section | YES | none needed | **NO** | YES |
| version 1 (pre-ADR-024 chamfers) | YES | the existing chamfer path | **NO** | YES |
| version 3 (a future file) | **NO** | — | — | refused by name |

Tested: a document with only a sketch and an extrude loads with zero materials, no
assignment, and a mass request that fails **for want of a material** rather than
succeeding with an invented one. The file is checked to contain no occurrence of
"material" at all.

**The strongest evidence here already existed and still passes.** `"The document format
is transparent JSON"` asserts the exact file TEXT of a document with no materials,
byte for byte, against a literal in the test. It passes unchanged, which proves this
milestone's schema addition alters nothing about how a pre-P15 document is written —
not a key, not an order, not a byte. A golden-text test is the one form of
backward-compatibility evidence that cannot be argued with.

### MIGRATION

None, and none is needed: an absent section means "none of those". No P15 field changed
meaning, so nothing has to be reinterpreted.

**Legacy material-like fields (item 52):** audited. The only pre-existing use of the word
in a document is a metadata *property* — `{"material": "6061-T6"}` — in the golden
document's free-text `document.metadata.properties` map, and a drawing's
`MaterialRemoval`, which is the ISO 1302 surface-finish symbol. **Neither is migrated.**
A free-text metadata string is a note a user typed, not an engineering material with
identity and provenance, and promoting one into a `MaterialId` would fabricate exactly
the data ADR-028 forbids. Recorded as a deliberate decision.

## FULL ROUND TRIP

A rich document: an imported library material, a fully populated one, a second with the
same designation, one from scratch with no origin, an incomplete one, a clone, an
**unresolved** assignment from a deleted material, and geometry with mass properties.

## SECOND ROUND TRIP

`A -> save -> load B -> save -> load C`, with `equivalent(A,B)`, `equivalent(B,C)` and
`equivalent(A,C)` — whole-document canonical comparison, not field spot checks — plus
every material's definition and name compared individually and the unresolved assignment
still unresolved.

## DETERMINISTIC SERIALIZATION

```text
two saves of one unchanged document      byte-identical
save -> load -> save                     byte-identical
save -> load -> save -> load -> save     byte-identical
five repeated saves                      byte-identical
```

Save → load → save being byte-identical is the stronger claim: it says the loader
introduces no normalisation of its own.

The ordering is structural. `Json` is `nlohmann::ordered_json`, so key order is the
writer's insertion order, which is a fixed code path. Materials come out in ascending ID
order — checked against a fixture where that is deliberately *not* alphabetical — and
provenance arrays in property enumeration order, built in reverse so the order cannot
have come from insertion.

## ADVERSARIAL REVIEW

**PASS — 30 attacks, 5 findings.** Three were defects I introduced and fixed; two are
decisions recorded. Full text: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
F1  the assignment writer was placed INSIDE the configurations guard, so a
    document with no configurations would have saved with the assignment
    silently dropped                                    DEFECT, fixed
F2  three references bound into temporary documents     DEFECT, fixed
F3  the reader dispatched on a dll-imported constexpr
    static, which does not link in a shared build       DEFECT, fixed pre-freeze
F4  the diagnostic strings are not usable as file keys  DECISION, recorded
F5  five "cannot be saved" tests replaced, not deleted  DECISION, recorded
```

**F1 is the one worth carrying forward.** It compiled, linked, and would have passed any
test whose document happened to have a configuration. It was found by reading the patched
region after a clean build — not by a failing test, because no fixture I had at that
point reached the branch. A patch applied by script into a large function needs its
region read back.

Three of the five automatic-FAIL gates were **mutation-tested**:

```text
Unknown written as 0                     -> 14 test cases fail
an unresolved assignment rebinding        ->  3 test cases fail
(the third, a changed MaterialId, is structural: restoreObject cannot change one)
```

## FULL REGRESSION

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2716/2716      0
release-ext            0        0      0          0        2716/2716      0
debug-shared-ext       0        0      0          0        2716/2716      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   13580 = 2716 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   13580 = 2716 x 5, 0 failures
qualification finished 03:27:44, 0 stage(s) failed
```

2716 = the 2685 of P15-CMD-001, plus the 36 added here, minus the 5 obsolete
"cannot be saved" tests this milestone made impossible. All three presets discover the
same 2716, and both repeat stages show exactly 13580 passing executions — 2716 x 5 with
nothing skipped.

**35308 test executions in total, 0 failures**, 00:21:15 to 03:27:44 (3h 06m).

**This is the SECOND qualification, and the first was voided by me for one blank line.**
The first run passed every stage with the same counts. Then the pre-commit
`git diff --check` reported `tests/io/DocumentFileTests.cpp:741: new blank line at EOF` --
left behind when the five obsolete tests were removed, and a genuine violation, since
zero committed source files in this repository end with a blank line.

`tests/` is inside the source fingerprint, so fixing it changed the qualified tree.
CLAUDE.md leaves no room: *"if source or tests change after the freeze, the qualification
is void and is run again from clean"*, and *"never relax a gate to obtain one"*. Waving it
through would have meant arguing that whitespace is not really a source change, which is
the precedent that makes a freeze stop meaning anything.

So: three hours of machine time for one byte. The full account, with the first run's
passing results, is in [qualification-void/](qualification-void/README.md).

**The lesson is about ordering, not whitespace.** `git diff --check` costs a second and
was run where the brief puts it -- before the commit, which is after the freeze. It
belongs in the pre-freeze checklist beside the shared-preset build that caught the
dll-import defect earlier in this same milestone and cost nothing:

```text
git diff --check          one second      run it BEFORE the freeze
build the shared preset   twenty minutes  run it BEFORE the freeze
freeze, then qualify      three hours
```

Both of this milestone's avoidable costs came from running a cheap check after an
expensive one.

The tests carrying this milestone's claims were confirmed present and passing in **all
three** presets:

```text
"The document format is transparent JSON" -- the golden-text test   passed x3
the MaterialId round trip                                          passed x3
an unresolved assignment never rebinding                           passed x3
no derived engineering state in the file                           passed x3
byte-identical serialization                                       passed x3
the 19-case malformed matrix                                       passed x3
a pre-P15 document loading with no fabricated material             passed x3
```

**A defect was caught BEFORE the freeze, by building the shared preset first.** The
reader dispatched on `features::Material::kTypeName`, which binds a reference to a
dll-imported `constexpr static` and does not link in a shared build. The repository had
already solved this six lines above my change and written down why; the fix follows that
established pair exactly. See ADVERSARIAL_REVIEW.md F3.

This is the third time in P15 that only `debug-shared-ext` would have caught a
DLL-boundary defect. The difference is the timing: P15-PROV-001 lost a two-hour
qualification to one of these, whereas here it cost twenty minutes and the clock had not
started. The pre-freeze shared build is the check worth keeping — a grep for the
*previous* variant would not have found this one.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that does
not exist: 3 stages failed, exit 3.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot
replace" across every log. That matters more here than usual: this milestone writes and
reads more files than any before it, so the fault had more chances to show.

A pre-qualification smoke run of the whole suite on `debug-ext` reported 2715/2716. The
one failure was `cli.new.unicode-path`, whose cause is the invocation rather than the
tree: it needs code page 65001, which `qualify.cmd` sets and an ad-hoc `ctest` does not.
All three qualified presets pass it.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first build
and after the last test run are identical:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include 6d55546edc191bbc2ca37d1db1b17dcb68cd3fcd
src c0c52b09a0497e075aeeb22ff87b97bb53303c3f
tests e776a234afbaa721932e8ce8c796de2286a49c65
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint and cannot
affect the executable or the tests.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## KNOWN LIMITATIONS

1. **Command history is not persisted**, and this milestone did not add it (item 32).
   BetterCAD persists no history for any module; undo does not survive a save.
2. **No property-law representation.** A temperature-dependent conductivity has no
   persisted form, because it has no in-memory form yet either. A file containing one is
   **rejected** rather than misread as a constant, which is the safe direction and is
   tested.
3. **No cross-document reference domain.** An assignment holds a bare `MaterialId`,
   which is unique within one document — the only domain that exists (ADR-003 defers
   external references). If cross-document material references ever arrive, the
   assignment will need a domain alongside the ID, and this schema will need a field for
   it.
4. **Byte determinism is asserted within a preset, not across presets.** The
   qualification shows all three presets pass the same tests, including the
   byte-identity ones; it does not compare bytes produced by Debug with bytes produced
   by Release. Doing so would need a cross-preset artefact comparison the harness does
   not have.
5. **No locale is exercised in the tests.** The audit is by construction — dates are
   assembled from integers, numbers go through nlohmann's own serialization, and nothing
   uses a locale facet — rather than by running under a second locale, which the test
   infrastructure has no support for.
6. **The library's own data is still absent**, so an imported material persists with
   metadata and no property values. Not a persistence limitation; the library is
   populated by the milestone that transcribes it and records its sources.

## RESULT

```text
TASK:            P15-PERSIST-001
IMPLEMENTATION:  MaterialJson.cpp -- the material's canonical mapping; a
                 material_assignment root section; materials::parseDate, the
                 repository's first ISO 8601 reader. Schema version UNCHANGED at 2,
                 which is the format's own rule for adding an object type and an
                 optional field.
TESTS:           36 added, 5 obsolete removed; 2685 -> 2716
VALIDATION:      real round trips through documentToJson/documentFromJson, not
                 mapping unit tests; a 19-case malformed matrix; byte-identity
                 through save-load-save; an existing golden-text test proving no
                 pre-P15 document's bytes changed; three automatic-FAIL gates
                 mutation-tested
RESULT:          PASS (second attempt; the first is recorded as void)
EVIDENCE:        this directory; schema-sample.json is a real serialized document;
                 qualification/ for all 20 stage logs and the tree fingerprints;
                 qualification-void/ for the first attempt and why it was discarded
TODO:            updated -- 17 boxes ticked
```

**The audit did most of the design work.** The document format already had a policy for
this exact case — *"adding a kind, an object type or an optional field needs no bump"* —
already defined backward and forward compatibility, already made load atomic by returning
a new Document, and already rejected duplicate IDs and restored the allocator. The work
was the material's mapping, the assignment section, and proving all of that holds for
materials rather than assuming it.

**The strongest backward-compatibility evidence already existed.** A golden-text test
asserts the exact bytes of a document with no materials. It passes unchanged, so this
schema addition alters nothing about how a pre-P15 document is written — not a key, not
an order, not a byte.

**Three defects of mine, all fixed.** The worst was not the linkage error: my first patch
put the assignment writer inside the configurations guard, which compiles, links, and
silently drops the assignment from every document without a configuration. It was found
by reading the patched region, because no fixture I had then reached the branch.

## REVISION

Second revision. The first qualification passed and was voided for a whitespace defect
in a test file; see FULL REGRESSION and
[qualification-void/](qualification-void/README.md). Written against the tree qualified
above; no source or test file changed after the second freeze, and the eight tree IDs
recorded before the first build and after the last test run are identical.
