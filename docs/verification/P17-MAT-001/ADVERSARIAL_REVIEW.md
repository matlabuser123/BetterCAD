# P17-MAT-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the structural material resolution before an
          element formulation is built on it
QUESTIONS: 27 from the brief, plus 4 of the reviewer's own
FINDINGS: 3 -- 1 that changed what the milestone could honestly claim, 2 test
          defects of my own
PRODUCTION DEFECTS: 0
GATE-BLOCKING: 0
```

## Findings

### F1 — the validation this milestone was asked to add already happens EARLIER (CHANGED THE CLAIM)

The checklist asks P17 to validate `E > 0`, validate `-1 < nu < 0.5` and reject
NaN and infinity. The first draft of the test file did the obvious thing: create
materials carrying those values and assert the structural resolver refuses them.

**Eleven of those cases failed**, and not at the resolver:

```text
REQUIRE( id.has_value() ) ... false
with message:
  the mechanical properties are not valid: Poisson's ratio -1 is outside
  -1 < nu < 0.5
  the mechanical properties are not valid: density must be greater than zero,
  not 0 kg/m^3
```

`createMaterial` refused the material. And so does `setMaterialMechanical`, via
the same validator — a refused edit leaves the previous value intact.

**So an unusable value is not something P17 must catch. It is something P15
makes unrepresentable.** That is a stronger guarantee than validating at the
solver, and it means a report of "PASS, P17 validates E and nu" would have been
technically defensible and misleading: the honest statement is that the values
cannot exist to be validated.

What changed:

```text
the boundary tests        now assert that P15 REFUSES the value at entry --
                          creation and edit, E, nu and density, including NaN
                          and both infinities. That is the guarantee that
                          exists
the delegation            is still there and is still tested, through the
                          reachable half of P15's contract: a required property
                          that is ABSENT. The same requireLinearElasticConstants
                          call would refuse an out-of-range value if an import
                          path, a file or a future API ever produced one
the enum                  has no "present but invalid" value, because P15 folds
                          out-of-range into not-available deliberately and
                          adding one would mean deciding here what out-of-range
                          means
```

Recorded as the milestone's main result rather than buried: the implementation
is thin **because the audit found it should be**, and the evidence is what
demonstrates the reuse is real.

### F2 — a read-only test that could not construct the state it needed (FIXED)

`StructuralMaterial_ResolutionIsDeterministicAndReadOnly` tried to prove that
repeated failed resolutions change nothing, by setting `nu = 0.9` and resolving
eight times. The edit was refused — the same F1 lesson, landing a second time in
my own test file.

Fixed by using the failure that **is** reachable: an absent property. The test
now removes the Poisson ratio, resolves eight times, and asserts the document
revision did not move and that `nu` is **still absent** — not filled with 0.3,
not filled at all. Which is the stronger version of what it was trying to say.

### F3 — I guessed what `traceabilityGaps` reports (FIXED)

The provenance test asserted `CHECK_FALSE(gaps.empty())` for a material with no
provenance recorded, on the assumption that absent provenance is a gap. It is
not — the call returned none.

The assertion was a guess about an API I had not read, and the property I
actually wanted was different anyway: that a material with **no provenance at
all still resolves**, because validation decides whether a number is usable and
provenance says where it came from, and P15 states that "No consumer requires
PROVENANCE". That is what the test asserts now, with the gap count merely
reported through `INFO`.

## The brief's 27 questions

**Can P17 create a second canonical structural material database?** No, and
verified by search rather than asserted: **zero** raw `double` or `float` data
members in the module, zero hardcoded `210`, `7850`, `0.3`, `"steel"` or
`"aluminium"` outside comments, and `sizeof(StructuralMaterial) < 128` asserted.
It holds an id, a revision, a mode, four constants P15 derived and an optional
density.

**Can P17 silently default a missing E?** **a missing nu to 0.3?** **a missing
density?** No, and the read-only test is the proof in the strongest available
form: after eight failed resolutions the property is still absent. P15's rule is
that missing data is reported and never filled, and nothing here fills it.

**Can `E = 0` pass?** **Negative E?** **NaN E, because `E <= 0` is false for
NaN?** None of them can reach a document — `createMaterial` refuses all three,
including NaN, which is the one a range comparison alone would miss. Each is a
section of the validity test.

**Can `nu = 0.5` pass?** No. It is refused at entry, and it is the bound that
matters: `lambda = E nu / ((1 + nu)(1 - 2 nu))` divides by zero there, so the
isotropic constitutive matrix is singular for a displacement-only formulation.
`nu = 0.49` **does** pass, and there is a test saying so — no arbitrary
near-incompressible threshold was invented.

**Can NaN nu pass?** No, refused at entry.

**Can a no-gravity analysis incorrectly require density?** No, and this is
tested from both sides: one material with E and nu and no density is `Ready` for
`FeaLinearStatic` and incomplete for `FeaLinearStaticWithGravity`, with
`missingMechanical == { Density }`. The requirement comes from
`requiredProperties`, which is **read** rather than restated — P15's own comment
says over-requiring "is as wrong as under-requiring and is harder to notice".

**Can a gravity analysis proceed without density?** No; `MaterialProblem::RequiredPropertyUnavailable`,
and the diagnostic names the density.

**Can a custom complete material be rejected for being custom?** No. The custom
test uses deliberately distinctive values — `E = 123 GPa`, `nu = 0.27`,
`rho = 4567 kg/m³` — so an accidental fallback to a built-in or a hardcoded
steel would be visible rather than plausible. It resolves, and its completeness
report is `Ready`.

**Can an incomplete custom material borrow values from a built-in?** No. A
custom material with E and no nu fails, and the diagnostic names the material by
designation.

**Can P17 lose material provenance?** It does not hold provenance, which is the
point: it holds the `MaterialId`, so everything P15 knows is one lookup away.
What would be lossy is copying it; what is recorded is the reference.

**Can the same `MaterialId` with a changed E leave a result current?** No, and
this is the question the whole source stamp exists for. Measured: id unchanged,
designation unchanged, `materialRevision` 1 → 2, `staleReasons == { Material }`,
and `StructuralMaterial::operator==` false. Currentness comparing the id alone
would have called it current.

**Can material reassignment leave a result current?** No; `material` changes and
the reason is reported.

**Can an E or nu edit mark the P16 mesh stale?** No, and four independent things
are asserted not to move: `Mesher::currency` stays `Current`, the `MeshStamp` is
unchanged, `geometryRevision` is unchanged, and the whole
`MeshControlDefinition` compares equal. A coupling would break at least one.

**Can a material edit regenerate geometry or change the mesh revision?** No —
the geometry revision and the mesh stamp are both asserted unchanged across the
edit.

**Can result currentness ignore material state?** No; `StructuralResultSource`
carries `material` and `materialRevision`, and P17-DATA's truth table mutates
each alone.

**Can a density-only edit incorrectly invalidate a no-gravity result?** It
**does** invalidate it, and that is a decision with a stated cost rather than an
oversight. `materialRevision` is a per-material counter and there is no
per-property revision in P15; inventing one in P17 would be a second revision
mechanism for material state. The cost is one unnecessary re-solve; the
alternative risks a result presented as describing a model it does not.
`INVALIDATION_MATRIX.md` records both options and the reasoning.

**Can a solver-ready material cache return stale properties?** There is no
cache. **Zero** occurrences of `static`, `cache` or `mutable` in the
implementation: resolution is a handful of lookups and is redone every time,
which is simpler than a cache and impossible to get stale.

**Can structural material resolution depend on GUI state?** No GUI, io,
persistence or command dependency in the module, and the layering check passes
over 455 files.

**Can it mutate P15 material to repair invalid data?** No. Every `Document`
parameter in the module is `const` — all four, checked — and there is no call to
`setMaterial*`, `modifyObject`, `addObject` or `removeObject`. Nothing clamps,
takes an absolute value or substitutes.

**Can P17 validation duplicate P15 completeness and drift later?** **Zero**
occurrences of `isFinite`, `<= 0`, `>= 0.5`, `minPoissonRatio` or
`maxPoissonRatio` in the implementation, and the density requirement is read
from `requiredProperties` rather than hardcoded — one line that makes the two
impossible to disagree.

**Can diagnostics be nondeterministically ordered?** No. The property lists come
from P15's report, whose ordering is "defined and deterministic … nothing here
is built from an unordered container", and the resolution is asserted identical
over 16 repeats.

**Can zero targeted tests run while qualification claims PASS?** Checked with
`-N` first: `StructuralMaterial` 19, `StructuralData` 21, `StructuralInput` 10,
`compile_fail.structids` 12, `architecture.` 14.

## Four of the reviewer's own

**Is a thin implementation a thin milestone?** It is the question this milestone
has to answer honestly, because the code added is small. The work was the audit
and the proof: finding that P15 validates at entry, that `requiredProperties` is
the single requirement table, and that `isAvailable` folds range into presence —
then writing tests that fail if any of those stops being true. A milestone that
had re-implemented the checks would have *looked* substantial and introduced the
drift the brief warns about in §37.

**Does the mode belong on the analysis definition, or was that convenient?** It
belongs there, and the brief says why: "Analysis intent decides consumer kind.
Do not infer gravity merely because density exists." A mode stored anywhere else
would have to be passed in by every caller, and the one that forgot would get
the wrong requirement. Putting it on the definition also means it is covered by
`analysisRevision`, so changing the mode stales a result — which is correct,
since it changes what the material must provide.

**Did adding it break something it should have?** Yes, and that is the better
answer. P17-DATA-001 asserted `sizeof(StructuralAnalysisDefinition) ==
sizeof(MeshControlId)`, meaning "nothing has been added", and adding the mode
fired it. The assertion's **intent** — catch derived state — was right; equality
was too strict a proxy, because it also catches the intent fields the definition
exists to grow. It is now a bound plus trivial copyability, with a comment
saying that the trivially-copyable half has a half-life too: `P17-LOAD-001`
giving the definition a collection of loads will break it legitimately, and the
instrument will have to change again. Second milestone running in which an
assertion I wrote was a proxy rather than the property.

**Could `MaterialProblem` have an unreachable value, as P17-ARCH's enum did?**
Checked deliberately, since that is the defect two milestones ago found in
itself. All three are reached: `BodyNotFound` by deleting the feature,
`NoMaterialAssigned` by assigning nothing, `RequiredPropertyUnavailable` by a
material missing a nu. A fourth for "present but invalid" was considered and
**not** added, precisely because nothing could return it.

## Result

```text
QUESTIONS:                      27 + 4 = 31
FINDINGS:                       3
PRODUCTION DEFECTS:             0
CLAIMS CORRECTED:               1  (F1 -- the validation happens at P15's entry,
                                   not at P17, and the evidence says so)
TEST DEFECTS OF MINE:           2  (F2, F3 -- both from assuming a state was
                                   constructible or an API behaved a way I had
                                   not read)
GATE-BLOCKING:                  0
VERDICT:                        PASS
```
