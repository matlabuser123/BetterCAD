# P14-TOL-001 — Tolerances, Fits and the GD&T Foundation

```text
TASK:      P14-TOL-001
STATUS:    PASS
BASELINE:  8427f35 (P14-ANNO-001), clean tree, HEAD == origin/main
```

## TASK

Give a dimension what the part is made to, and give a feature what it is held
to: deviation pairs and limits, the fit notation of ISO 286, the datum letters
of ISO 5459 and the feature-control frame of ISO 1101 — as **engineering
meaning**, never as text that happens to look right.

The milestone's own gate, from `TODO.md`:

```text
tolerance intent preserved
+ datum references stable
+ GD&T data model coherent
+ persistence correct
+ export representation correct
```

## BASELINE

`P14-ANNO-001` at `8427f35`, verified qualified before anything was written,
not assumed:

```text
ctest-debug.log         100% tests passed out of 1915
ctest-release.log       100% tests passed out of 1915
ctest-debug-shared.log  100% tests passed out of 1915
ctest-repeat-debug.log  100% tests passed out of 1914
ctest-repeat-release.log 100% tests passed out of 1914
TODO.md:512-528         every box [x], evidence linked
git                     HEAD == origin/main == 8427f35, tree clean
```

## SCOPE

### In

```text
a deviation pair, signed from the nominal, shown as +/- or as two limits
the fit designations of ISO 286, resolved from the tables for holes D-H
the ISO 5459 datum letter rule, shared with the datum feature symbol
the ISO 1101 feature-control frame: characteristic, zone, magnitude,
    ordered datums -- validated, drawn, persisted
ten geometric characteristics and their symbols
a report of the datums a frame cites that nobody has defined
```

### Out, and why

```text
shaft fundamental deviations      not transcribed in this build; the
                                  designation is kept and fitDeviations()
                                  fails by name rather than estimating
hole positions outside D-H        same
material-condition modifiers      MMC, LMC and their regardless-of-size
  (M, L)                          default need a size-tolerance link this
                                  foundation does not have
composite frames                  two rows of requirement against one
                                  feature; needs the single row first
profile and symmetry              need a profile or median-feature contract
angular tolerances                the deviations here are lengths, and a
                                  length on an angle means nothing; refused
general tolerance classes          ISO 2768; a document-level default, not a
  (f, m, c, v)                    dimension's own intent
drawing commands and undo         P14-CMD-001
headless workflows                P14-CLI-001
PDF / SVG / DXF                   P14-EXPORT-001
```

## ARCHITECTURE

One decision was architecturally significant and is recorded as
[ADR-020](../../architecture/decisions/ADR-020-a-datum-reference-is-a-letter.md):
**a datum reference is a letter**, held in an ordered vector, bound to nothing
— with the two alternatives (an `AnnotationId` naming the datum symbol, and a
direct geometry reference) compared and rejected there.

Everything else follows decisions already in force:

| Decision | How this milestone obeys it |
| --- | --- |
| ADR-011 — intent canonical, presentation derived | a tolerance stores an interval and a display mode; every string, cell and line is computed on the call |
| ADR-012 — the drawing reference vocabulary | a datum reference is a letter, so **no new reference kind is introduced**; a frame's target is the same `AnnotationTarget` every annotation uses |
| ADR-016 — the scene is the export boundary | a frame produces `SceneItems` in sheet millimetres and nothing else |
| ADR-017 — drawing identity | a frame is an `Annotation`, a toleranced dimension is a `Dimension`; no new document object |

**No parallel system was created.** The brief's prohibition was checked
against what already existed:

```text
ISO 286 tables      standards::HoleTolerances (layer 0, qualified in
                    P12-HOLE-001 against independent copies) -- reused, not
                    re-transcribed
datum symbols       AnnotationType::Datum (P14-ANNO-001) -- kept; its
                    letter rule was EXTRACTED to validateDatumLetter() so
                    the symbol and every frame that cites one share it
references          ADR-012's vocabulary, unchanged
placement / scene   P14-ANNO-001's leader, arrowhead and text primitives
number formatting   P14-DIM-001's formatter, extended once (below) rather
                    than duplicated
```

## BLAST RADIUS

The change is confined to `drawing` (layer 4) and its two JSON serializers.
Every file that names the new types:

```text
include/bettercad/drawing/Tolerance.hpp     new
src/drawing/Tolerance.cpp                   new
include/bettercad/drawing/Dimension.hpp     tolerance field, three functions
include/bettercad/drawing/Annotation.hpp    frame field, one enumerator
include/bettercad/drawing/Annotations.hpp   undefinedDatums()
src/drawing/Dimension.cpp                   validation, formatting, precision
src/drawing/Dimensions.cpp                  measure() assembles the text
src/drawing/Annotation.cpp                  validation, shared datum rule
src/drawing/Annotations.cpp                 the frame's cells, undefinedDatums
src/io/json/DimensionJson.cpp               tolerance persistence
src/io/json/AnnotationJson.cpp              frame persistence
tests/drawing/ToleranceTests.cpp            new
```

Two of those are **shared infrastructure**, and they set the regression set:

```text
Dimension.cpp's formatter   every dimension on every drawing writes through
                            it. A precision rule was added, so the whole
                            dimension suite is in the regression set.
Annotation.cpp's validator  every annotation is checked by it. The datum
                            letter rule moved, so the whole annotation suite
                            is in the regression set.
```

Nothing outside `drawing` and `io/json` references the new types — confirmed
by search, not assumption. No CLI surface exists for drawings yet
(`P14-CLI-001`), so none was added.

## IMPLEMENTATION

### One interval, two ways of writing it

```cpp
struct DimensionTolerance {
    Length lower{};          // signed from the nominal, and lower < upper
    Length upper{};
    std::optional<FitDesignation> fit{};   // a fit XOR a deviation pair
    ToleranceDisplay display = PlusMinus;  // or Limits
};
```

`20 ±0.05` and the pair `20.05 / 19.95` are **one stored fact with a display
mode**. Both are written from what `intervalOf()` returns, so they cannot come
to describe different parts — which a stored string could.

The signs are stated once and obeyed everywhere: both deviations are signed
from the nominal, so `±0.05` is `-0.05, +0.05`. **The nominal need not lie
inside the interval**, and that is deliberate: `H7` puts the whole of it at or
above the nominal, and a contract demanding otherwise could not express `H7`.

### Fits are read from the standard, never copied

A fit stores its designation. The numbers are resolved on every call through
`standards::limitDeviations`, so a drawing citing `H7` follows ISO 286 rather
than a copy of it — and the same `H7` gives 15 µm at 8 mm and 21 µm at 20 mm,
because the standard's size ranges are not linear. There is a test for exactly
that.

Where the tables do not reach, it **fails by name**:

```text
g6          "the fundamental deviations of shafts are not tabulated in this
             build, so g6 has a known tolerance width but no known limits;
             the designation is kept and no numbers are invented"
K7          "the hole position K is not tabulated in this build, which
             carries D, E, F, G and H; ..."
```

`fitWidth()` still answers in both cases, because the standard tolerance IT is
shared by holes and shafts — so a shaft's tolerance width is known even where
its position is not. That asymmetry is the honest one.

### The feature-control frame

```cpp
struct FeatureControlFrame {
    GeometricCharacteristic characteristic;  // ten of ISO 1101's fourteen
    ToleranceZone zone;                      // Width or Cylindrical
    Length tolerance{};
    std::vector<DatumReference> datums{};    // primary, secondary, tertiary
};
```

A **vector**, never a set: `A|B|C` and `B|A|C` compare unequal, serialize
differently and draw differently, and there is a test for each.

`validate()` refuses what is incoherent whatever standard is being followed:

```text
a tolerance that is not finite and greater than zero
a form characteristic citing a datum        a flat face is flat by itself
an orientation, position or runout          parallel to WHAT?
    characteristic citing none
a cylindrical zone on a characteristic      a flatness zone is two planes
    that holds a surface or a circle
more than three datums, or one twice
a datum letter ISO 5459 does not use
```

### Datums that nobody defined

A datum reference is a letter and binds to nothing (ADR-020), so a frame can
cite a datum no symbol defines. That is **reported, not refused**:

```cpp
Result<std::vector<char>> undefinedDatums(const Document&, AnnotationId frame);
```

Refusing would make the order an engineer works in part of what is legal —
frames and datum symbols are placed in either order. The report is computed
from the datums that exist now, in the order the frame cites them, so defining
`C` removes it and deleting the symbol brings it back.

## TESTS

`tests/drawing/ToleranceTests.cpp` — **43 new test cases**. The
`[tolerance]` tag also selects the four ISO 286 hole-standards cases that
`P12-HOLE-001` left behind, so the filter reports 47:

```text
[tolerance]    4272 assertions in 47 test cases
[drawing]      6701 assertions in 267 test cases
[annotation]   1307 assertions in 34 test cases
[dimension]     931 assertions in 34 test cases
```

The last two are worth reading twice. This milestone moved the datum-letter
rule out of the annotation validator and added a precision rule to the
dimension formatter -- both **shared infrastructure**. Their suites are
unchanged case for case and assertion for assertion against the milestones
that qualified them (`P14-ANNO-001`: 1307/34; `P14-DIM-001`: 931/34), so
neither milestone's qualified behaviour moved. `[drawing]` went from 5592/224
to 6701/267: exactly the 43 new cases.

Grouped:

```text
the interval          symmetric, asymmetric, the nominal outside it, a
                      reversed one, a non-finite one, one of no width
presentation          +/- and limits are the same interval; display
                      precision does not change it; the view scale does not
fits                  H7's tabulated deviations; the same H7 at two sizes;
                      a shaft kept as notation with no numbers; an
                      untabulated position; fit XOR deviations; the
                      designation validated on its own; a size-range
                      boundary; clearance from two resolved intervals
datums                one letter rule, reaching the same verdict word for
                      word through the datum symbol and through a frame
frames                datum order is the requirement; form vs related
                      characteristics; the cylindrical zone rule; five
                      incoherent frames; every characteristic has a symbol
                      and a round trip
drawn                 cells in reading order; the text inside its own
                      compartment; no diameter sign on a width zone; a
                      cell sized by characters not bytes; no rounding;
                      7 mm high at four scales while its target moves
references            a dimension follows its model and keeps its
                      tolerance; a frame whose feature is gone is
                      unresolved; an angular dimension takes no length
                      tolerance
undefined datums      reported in the frame's order; live, not cached;
                      empty for anything that is not a frame
persistence           the whole definition round trips; datum order
                      survives and the file shows it; ten malformed files
export                the exported representation is complete on its own;
                      a frame reaches the whole sheet's scene
determinism           read and drawn six times, bit for bit
```

## INDEPENDENT VALIDATION

Every expected value is computed in the test or quoted from the standard.
**No expected value is produced by calling the code under test.**

```text
20 +/-0.05        -> [19.95, 20.05]     arithmetic, written out
20 +0.10/-0.02    -> [19.98, 20.10]     arithmetic, written out
H7 at 20 mm       -> [20.000, 20.021]   ISO 286-1: over 18 up to and
                                        including 30, IT7 = 21 um; H
                                        places EI at zero
H7 at 8 mm        -> width 0.015        ISO 286-1: over 6 up to 10,
                                        IT7 = 15 um
H7 at 30 mm       -> width 0.021        the range INCLUDES its upper limit
H7 at 40 mm       -> width 0.025        and the next range differs
H7 at 100 mm      -> [100.000, 100.035] ISO 286-1: over 80 up to 120,
                                        IT7 = 35 um
g6 at 20 mm       -> width 0.013        ISO 286-1: IT6 = 13 um; the
                                        deviations are NOT known
H7/H6 at 20 mm    -> clearance          C_min = D_min - d_max = -0.013
                                        C_max = D_max - d_min = +0.021
frame height      -> 7.0 mm             ISO 1101 draws the frame twice the
                                        lettering high; 2 x 3.5
```

### The quoted figures are not circular

Quoting a standard is only validation if the figure does not come from the code
being tested. It does not, and this was checked rather than assumed.

`P12-HOLE-001` transcribes every table **twice, from different sources**:
BetterCAD's own table from the ISO 286-2:2010 preview, and the tests' reference
table from the Wikipedia "IT Grade" article and MISUMI's excerpt of JIS B 0401.
Its `crosscheck.py` reads both sources and compares them with both
transcriptions: **21 checks, 0 failed**.

Every figure asserted here matches the TESTS' transcription
(`tests/core/standards/HoleStandardsTests.cpp:91-103`, column `H7` of
`kLimitDeviations`, and `H6` beside it):

```text
size range            H6      H7      used here for
over  6 up to  10      9      15      H7 at 8 mm
over 18 up to  30     13      21      H7 and g6 at 20 mm, and the 30 mm
                                      boundary
over 30 up to  50     16      25      H7 at 40 mm, the range ABOVE the
                                      boundary
over 80 up to 120     22      35      H7 at 100 mm
```

So the chain is: the standard, read by two independent transcriptions, checked
against each other and against their sources in a qualified milestone — and
this milestone asserts against the one that is not wired into the code path it
exercises. The tables are not re-validated here; they are relied on, with the
reliance stated.

## ADVERSARIAL REVIEW

The review was run against the final diff, and it found **five defects plus one
latent hole**. Four were in the implementation, one was in the fix for the
third, and all six are fixed with a regression test each.

### 1. A cell was sized by its BYTES, not its characters

`cell.size()` on a UTF-8 string. `U+2316 POSITION INDICATOR` is three bytes, so
a one-character symbol compartment was drawn three characters wide — and two
frames with the same number of characters came out different widths depending
on which symbols they held.

```text
found by   asking what `.size()` means for a string holding a symbol
fixed by   counting code points: a continuation byte is 10xxxxxx and starts
           no character
test       Tolerance_ACellIsSizedByItsCharactersAndNotItsBytes -- the
           symbol's compartment and a datum letter's are one character each,
           so they must be the same width
```

### 2. The frame rounded its own tolerance

The magnitude was written with a fixed two decimals. A 0.005 mm zone — ordinary
in precision work — would have been **drawn as `0.01`**: a figure twice what
the model holds, on the face of the drawing, where nothing downstream could
catch it.

```text
found by   asking what the formatter does to a value smaller than its
           precision
test       Tolerance_AFrameWritesItsToleranceWithoutRounding -- 0.2, 0.05,
           0.005 and 0.0125 each written whole
```

### 3. A dimension's tolerance was written to the NOMINAL's precision

The same defect one level up, and worse, because it reached both
presentations:

```text
decimals = 0, +/-0.05     ->  "100 +/-0"        a requirement no part can meet
decimals = 2, H7 at 100   ->  "100.04" / "100.00"
                              neither the standard's width (35 um) nor its
                              position, presented as ISO 286
```

The engineer's `decimals` is a presentation choice about the nominal. Applied
to a tolerance it stops being one.

```text
fixed by   decimalsWithoutRounding(value, atLeast) -- the smallest precision
           that writes the value whole, never fewer than asked for, capped at
           six (a nanometre). ONE helper, used by the deviation text, the
           limits text and the frame's cell, so the three cannot come to
           round differently.
           The LIMITS text takes its precision from the DEVIATIONS and not
           from the limits: a limit carries the measured length, and asking
           how many decimals 111.803399 needs would put six on the drawing.
tests      Tolerance_ATolerancesOwnPrecisionSurvivesTheNominalsFormat
           Tolerance_AFitsLimitsAreTheStandardsAndNotARounding
```

### 4. A deviation pair of no width was accepted

`DimensionTolerance{}` — every field defaulted — validated, and read `±0`. So
the way to put an impossible requirement on a drawing was to leave a field
unset. The frame already refused a zone of no width for exactly this reason;
the deviation pair did not.

```text
fixed by   requiring lower < upper strictly, with the reason in the message
           ("leave the tolerance off instead")
test       Tolerance_AToleranceOfNoWidthIsRefused -- the default and an
           equal pair refused, and the smallest real width accepted, so the
           rule refuses only what it says it refuses
```

### 5. The fix for #3 had the same bug it was fixing

`decimalsWithoutRounding` first compared the rounding error against an
**absolute** 1e-6. That calls every value below a micrometre "already whole",
and answers *no decimals* — for precisely the values that most need them. It
was found by the test written for the cap, not by reading the code.

```text
fixed by   a RELATIVE slack: it absorbs the representation error of a number
           that came through metres, and nothing else
test       the cap case in Tolerance_ATolerancesOwnPrecisionSurvivesTheNominalsFormat
```

### 6. A latent optional dereference

`measure()` read `*measured.length` for a toleranced dimension. The invariant
holds — an angular dimension with a tolerance is refused when it is built and
when it is loaded — but the cost of it ever moving is reading an empty
optional. It is now checked, with the invariant written down beside it.

### What was attacked and held

| Question | Answer |
| --- | --- |
| Can the two presentations describe different parts? | No. Both are written from one `intervalOf()`; asserted equal bit for bit |
| Can the drawing scale change what the part is made to? | No. Asserted at 1:1, 1:2 and 5:1 |
| Can display precision change the interval? | No. Asserted bit for bit at 0, 1, 3 and 4 decimals |
| Can a fit go stale against the standard? | No. Only the designation is stored; the same `H7` gives 15 µm at 8 mm and 21 µm at 20 mm, and there is a test |
| Does a size-range boundary fall the wrong way? | No. 30 mm is *in* the lower range, and 30 mm ± 1e-10 and ± 1e-12 mm all give 21 µm while 40 mm gives 25 |
| Could `isSymmetric` be fooled by `-0.0`? | The only pair that reaches it is now a real width; the zero pair is refused |
| Is a second datum, reference or tolerance system introduced? | No. A datum is a letter (ADR-020), so ADR-012's vocabulary is untouched; the ISO 286 tables are `P12-HOLE-001`'s |
| Can a frame rebind to the wrong feature? | It binds to no feature. Its *target* resolves through the one annotation path, and fails by name when the feature goes |
| Did a datum letter rule get duplicated? | It was *de*-duplicated: the symbol's rule moved into `validateDatumLetter` and both sides now give the same message, word for word — asserted |
| Could a set have crept in for datums? | A vector, asserted unequal for `A|B|C` vs `B|A|C`, in memory, in the file and on the sheet |
| Can a malformed file produce a drawable but wrong frame? | No. Ten corruptions refused, each by the rule a caller in memory meets |
| Can a limit be stored and go stale? | Nothing measured is written. Asserted: a limits dimension's file contains neither limit |
| Was a tolerance loosened or a test weakened to pass? | No. Tolerances are 1e-9 mm against values that are exact arithmetic; the two that are looser (1e-6 mm on a frame's height) are noted where they are |
| Is there behaviour that exists only for tests? | No back door was added; every test uses the public API |
| Could Debug and Release differ? | The formatter is integer arithmetic (`P14-DIM-001`); the new helper uses only `round` and `abs`. Covered by the three-preset qualification |
| Did the scope widen? | `apps` is byte-identical to the baseline: no CLI was touched (`P14-CLI-001` owns it) |

## FAILURE PATHS

Every refusal names what is wrong and what would be right:

```text
lower above upper       "a tolerance's lower deviation must not be above its
                        upper one; the deviations are signed from the
                        nominal, so a symmetric tolerance is -x and +x"
no width                "a tolerance of no width admits only the nominal
                        exactly, which nothing can be made to; leave the
                        tolerance off instead"
not finite              "a tolerance's deviations must be finite"
fit and deviations      "... not both: a fit already says what the
                        deviations are"
a shaft's limits        "... not tabulated in this build ... no numbers are
                        invented"
an untabulated hole     "the hole position K is not tabulated in this build,
                        which carries D, E, F, G and H ..."
a zone of no width      "a geometric tolerance must be finite and greater
                        than zero: a zone of no width admits nothing"
form with a datum       "... is a property of the feature by itself and
                        cites no datum; citing one says an orientation
                        tolerance was meant"
related with none       "... holds a feature against a datum, and this frame
                        cites none: parallel to what?"
a cylinder on a face    "a flatness tolerance holds a surface or a circle,
                        not a line, so its zone cannot be a cylinder"
four datums             "a feature-control frame cites at most three datums"
one datum twice         "datum A is cited twice in one frame"
I, O or Q               "ISO 5459 does not use I, O or Q as datum letters:
                        they read as 1 and 0"
an angular tolerance    "an angular tolerance is not part of this
                        foundation; the deviations here are lengths, and a
                        length on an angle means nothing"
a frame with no frame   "a feature-control frame must say what it controls"
a note with one         "a note annotation carries no feature-control frame"
```

A failed call changes nothing: `createDimension` and `createAnnotation`
validate before the document is touched, and no ID is consumed.

## PERSISTENCE

The real round trip — create, save, load, regenerate, compare — on three
toleranced dimensions (a deviation pair, a limits display, a shaft fit) and a
three-datum frame. `deserialize(serialize(intent)) == intent` compares the
**whole definition**, and the loaded document is then regenerated and measured
and drawn and compared to the original's output.

```text
what is written    the deviations, or the fit designation; the
                   characteristic, zone, magnitude and datums IN ORDER
what is NOT        every limit, every string, every cell, every line. A
                   limits dimension's file contains neither "100.05" nor
                   "99.95" -- asserted, because a stored limit is a number
                   that could go stale against the model (ADR-011)
```

Datum order is asserted twice: after the round trip, and in the file text
itself (`"A"` before `"B"` before `"C"`).

Ten malformed files are refused, each by the rule a caller in memory meets:
an unknown characteristic, zone, display or fit role; a two-letter fundamental
deviation; a grade of 99; datums that are not a list; a two-letter datum; four
datums; and a lower deviation above its upper — that last written by position
so the test does not assume how a double was spelled.

## DETERMINISM

```text
measured and drawn six times      identical text, identical interval bit for
                                  bit, identical lines and texts
undefinedDatums six times         identical
cells                             ordered by the frame's vector, not by any
                                  container's iteration
annotations on a sheet            ascending ID order (P14-ANNO-001)
no clock, no seed, no thread, no unordered container, no locale
```

## REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **1958 / 1958** (289.70 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **1958 / 1958** (297.39 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **1958 / 1958** (310.39 s) |

```text
qualification finished Wed 23/09/2026 13:33:09.09, 0 stage(s) failed
```

The no-op rebuild is the fresh-binary proof: each `rebuild-*.log` holds one
line, `[1/8] Checking git revision`, and no compile or link step -- so the
binary that ran the tests was built from the tree that was frozen, and was not
left over from an earlier one. `P14-DIM-001` and `P14-ANNO-001` were both bitten
by a stale binary, which is why this is checked rather than assumed.

Baseline was 1915 tests; this milestone adds **43**.

### The determinism gate

```text
repeat release  exit 0   1957 / 1957, each test run five times (1199.52 s)
repeat debug    exit 0   1957 / 1957, each test run five times
```

`ctest --repeat until-fail:5`, so a test that passed once and failed on a later
run would fail the stage. The selection is stated honestly: the filter's
`[Uu]nit` alternative matches the `unit.` prefix every Catch2 test carries, so
the repeat set is the **whole suite** bar one -- `gui.launch.smoke`, which
launches a window and does not belong in a determinism repeat.

### The recurring filesystem fault did NOT recur

The repeat stage failed in `P14-DIM-001` (debug) and again in `P14-ANNO-001`
(release), both times with `cannot replace 'built.bcad': Permission denied` --
OneDrive or a scanner holding a file a CLI test rewrites. It did not happen
this time, in either preset, across ten passes of the suite.

That is **not** evidence the problem is gone. It is an intermittent fault in
the filesystem the build tree sits on, it has now been seen in two milestones
and two presets, and a clean run is exactly what an intermittent fault looks
like between occurrences. `TODO.md` still carries the open decision to move
build output off OneDrive, and this run does not close it.

### The harness itself

`verify-harness.cmd` was run before the qualification and **passes**. It points
the harness at a preset that does not exist -- a real failed stage, not a
simulated one -- and requires a non-zero exit:

```text
QUALIFICATION FAILED: 3 stage(s) failed.
PASS: a failed stage gave qualify.cmd exit 3.
```

One note on process, because it is the trap the run-numbering in `P14-DIM-001`
was written for: the first attempt at this check was invoked wrongly
(`cmd //c verify-harness.cmd`, whose working directory did not carry), so the
harness **never started** -- and the wrapper still reported exit 0. That is a
non-result, not a pass, and it is recorded as one. The check was re-run with an
absolute path and an explicit exit-code capture, which is the run quoted above.

### The qualified tree is the committed tree

Tree IDs from a scratch index, taken three times -- before the first build, by
the harness after the last test run, and again before the commit -- identical
in all three:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           707ac3d2da79b3d86cf0e418b63c1a57a2a4f6f9
src               1a482b11bf477bbe9b84f5e243c45f18da05f281
tests             bb99c1ecf5cd06b653090d7f97a7a43822285e63
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**byte-identical to the P14-ANNO-001 baseline**: the scope did not widen, no
CLI was touched, and the build configuration was not altered to obtain a pass.

One regression run was started and then deliberately **voided**: a defensive
guard was added to `measure()` while it was running, so the binary under test
was no longer the tree on disk. It was stopped rather than reported. The tree
was rebuilt and the qualification run once, from clean, on the final source.

## KNOWN LIMITATIONS

```text
1  Shaft fundamental deviations are not tabulated. g6 keeps its notation and
   its width; its limits FAIL by name. No number is invented.
2  Hole positions outside D-H are not tabulated, and JS is not known at all
   (P12-HOLE-001: the sources disagree on its rounding).
3  No material-condition modifiers. MMC and LMC change what a positional
   tolerance means and need a link to the size tolerance that this
   foundation does not have.
4  No composite frames, no all-around or between modifiers, no projected
   tolerance zones.
5  Profile and symmetry characteristics are absent: they need a profile or
   median-feature contract. Ten of ISO 1101's fourteen are here.
6  Circular runout is drawn with U+2197 NORTH EAST ARROW. Unicode has no
   codepoint for the symbol; this is the conventional substitute and is
   said to be one in the source.
7  A cell's width uses a stated character proportion (0.7 of the height).
   Nothing here knows a font. The frame is therefore proportioned, not
   typeset.
8  Renaming a datum symbol from B to C silently changes what every frame
   citing either letter requires. Undetectable in this model, and left to
   P14-STREF-001 (ADR-020).
9  A frame citing an undefined datum draws normally; only undefinedDatums()
   says otherwise, and a caller has to ask. A drawing checker belongs to a
   later milestone.
10 Dimensions still have no scene representation -- P14-DIM-001 shipped
   measure() and no draw(). So a toleranced dimension's export form is the
   MeasuredDimension, and a writer must use text AND lowerText. Asserted,
   so a writer that used only one would be using half the requirement.
11 Angular tolerances are refused rather than approximated.
12 A frame's leader starts at its placement point and its box extends to the
   right, so a target to the RIGHT is reached by an elbow that crosses the
   frame's first compartment. This is P14-ANNO-001's leader, used unchanged
   and deliberately -- giving the frame its own attachment rule would make
   it behave differently from every other annotation, which the brief
   forbids. Where a leader attaches belongs to a drawing-layout milestone.
```

## RESULT

```text
TASK:            P14-TOL-001 -- Tolerances, fits and the GD&T foundation
IMPLEMENTATION:  ADR-020; DimensionTolerance as a signed interval with a
                 display mode; ISO 286 fit designations resolved from the
                 qualified tables for holes D-H and refused by name
                 elsewhere; one datum-letter rule shared with the datum
                 feature symbol; FeatureControlFrame with ten ISO 1101
                 characteristics, ordered datums, validation, cells and
                 persistence; undefinedDatums() as a report; a precision
                 rule so no tolerance is rounded onto a drawing
TESTS:           43 new; 1958/1958 in debug, release and debug-shared, each
                 from clean; 1957/1957 five times over in release and debug
VALIDATION:      the interval arithmetic computed in the tests; ISO 286's
                 IT7 = 15, 21, 25 and 35 um at 8, 20, 40 and 100 mm and
                 IT6 = 13 um at 20 mm, quoted from the transcription that is
                 NOT wired into the code path under test and cross-checked
                 against its sources in P12-HOLE-001; the ISO 1101 frame
                 height of twice the lettering; clearance from two resolved
                 intervals
ADVERSARIAL:     5 defects found, 5 fixed, 6 regression tests (one for a
                 latent optional dereference found with them)
WARNINGS:        0 in all three builds
DETERMINISM:     measured and drawn six times, bit for bit; the repeat gate
                 clean in both presets
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-TOL-001 -> [x]
CARRIED OPEN:    P14-HLR-001's assembly-to-assembly occlusion validation,
                 still blocked on P14-ASM-001 and untouched here
OPEN DECISION:   move build and test output off OneDrive. The fault did not
                 recur in this milestone, which does not close it.
NEXT:            P14-ASM-001 -- assembly drawing views
```

## FILES

```text
qualification/qualify.cmd               the harness, carried from P14-ANNO-001
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree
                                        IDs before the first build and after the
                                        last test run
qualification/configure-*.log           three presets
qualification/clean-*.log               three presets
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               1958/1958 in each preset
qualification/ctest-repeat-*.log        1957/1957 five times over, debug and release
```
