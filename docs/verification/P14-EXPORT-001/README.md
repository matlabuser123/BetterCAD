# P14-EXPORT-001 — PDF / SVG / DXF Export

```text
STATUS:    PASS
MILESTONE: P14-EXPORT-001
DATE:      2026-09-25
BASELINE:  c405333 (P14-CLI-001)
```

## TASK

Complete the neutral vector drawing scene, and write it out as PDF, SVG and
DXF. A writer transcribes the scene and computes nothing.

## SCOPE

Authorized by `TODO.md`, `P14-EXPORT-001`. Not started here: `P14-REFMOD-001`,
`P14-QUAL-001` or anything later. No DXF import, no 3D DXF, no general CAD
interoperability.

## EXPORT ARCHITECTURE

```text
intent -> model -> views, dimensions, annotations -> DrawingScene -> PDF|SVG|DXF
                                                     ^^^^^^^^^^^^
                                                     the boundary
```

`ADR-016` decided this boundary before any of it existed. The signatures are
what hold it:

```cpp
Result<std::string> svgDocument(const DrawingScene&);
Result<std::string> dxfDocument(const DrawingScene&);
Result<std::string> pdfDocument(const DrawingScene&);
```

**A writer takes a scene and nothing else.** No `Document`, no `Body`, no
regenerator, no scale, no resolver. It is not that the writers are careful not
to reach the model — there is nothing in their signatures to reach it through.
That is the gate's first requirement, met by construction.

**No new dependency.** All three formats are written here. PDF is 250 lines
because an uncompressed PDF 1.4 with one page is a small format; DXF is a list
of group-code pairs; SVG is XML. Bringing in a library would have added a
supply-chain surface for something this size, and would have made byte
determinism somebody else's decision.

## DRAWING SCENE CONTRACT

What existed: `SceneLine` (a polyline with a style), `SceneText`,
`SceneItems`, and `LineStyle`. `Scene.hpp` said in as many words that the rest
— "the sheet frame, layers, hatch regions, line weights, the self-validating
assembly of a finished sheet" — was this milestone's.

What this milestone added:

```text
SceneArc          an EXACT circular arc: centre, radius, start, signed sweep
Length width      on every primitive: the pen, in paper millimetres
DrawingScene      one finished sheet: a page size and its items
validate(scene)   the page is positive, the items are sound, and everything
                  drawn is ON the page
sheetScene(...)   the assembly: frame, then each view in ascending ID order --
                  its edges styled by what HLR already decided, its section
                  hatch, its dimensions, its annotations
drawDimension()   what a dimension DRAWS, which did not exist at all
```

**`drawDimension()` is the one piece of new drawing engineering.** A dimension
knew its value and had no drawn form: no extension lines, no dimension line, no
arrowheads. Three writers each constructing those would have disagreed, so it
is built once, in the module that owns dimensions, and the writers transcribe
it.

## COORDINATE SYSTEM

Stated once, in the scene, so three writers cannot each decide:

```text
millimetres, on the page
origin at the sheet's BOTTOM-LEFT corner
+X right, +Y UP
the page occupies [0, width] x [0, height]
```

PDF and DXF agree with that and transform nothing but the unit. **SVG's y grows
downward**, so its writer flips once, in a function called `flip()`, and
nowhere else.

## PHYSICAL UNITS

```text
SVG   width="420.0000mm" height="297.0000mm" viewBox="0 0 420.0000 297.0000"
DXF   $INSUNITS 4  (millimetres), said rather than left to be guessed
PDF   pt = mm * 72 / 25.4, in one function
```

A4: 210 mm is 595.2756 pt and 297 mm is 841.8898 pt. The test checks the
generated page box against those two figures **computed in the test**, not
through the writer's own helper.

## TEXT, AND WHAT EACH FORMAT CAN CARRY

Units are not the only thing a format can be wrong about. The scene holds text
as UTF-8, and **two of the three formats cannot take UTF-8**:

```text
SVG   declares encoding="UTF-8" and carries the bytes. Nothing to do.
PDF   a simple font has a SINGLE-BYTE encoding. Helvetica is written with
      /WinAnsiEncoding, so a character becomes the one byte that gives it.
DXF   R12 predates Unicode: text is in the drawing's code page, and the file
      must SAY which. $DWGCODEPAGE ANSI_1252, so a reader never guesses.
```

`src/io/TextEncoding.hpp` holds the one transcoder the two single-byte formats
share — a UTF-8 decoder and a WinAnsi encoder, beside its sources as a private
header. The characters that matter are the ones on real drawings:

```text
Ø  U+00D8   the diameter callout       -> WinAnsi D8, PDF \330
±  U+00B1   a symmetric tolerance      -> WinAnsi B1, PDF \261
°  U+00B0   an angular dimension       -> WinAnsi B0, PDF \260
```

A character the encoding has no byte for becomes `?`. That is one `?` per
CHARACTER and not per byte, and it covers the GD&T symbols, which no
single-byte encoding and none of PDF's fourteen standard fonts can reach — see
`KNOWN LIMITATIONS`. Control characters go the same way, which matters most for
DXF: a newline inside a text value would corrupt not the text but the
**line structure** of a line-based format.

This section exists because the writers got it wrong first. The adversarial
review below is where that is recorded.

## TESTS

**26 new ctest entries**: 20 export cases in `tests/io/DrawingExportTests.cpp`,
3 CLI export cases in `tests/cli/DrawingCliTests.cpp`, and 5 process tests
driving the built executable (one earlier process test, which asserted that no
drawing exporter existed, is replaced by the ones that exercise it).

```text
[io][export][p14]    475 assertions in 20 cases
[cli][export][p14]   125 assertions in  3 cases
[io][export][p14][text]
                      45 assertions in  3 cases   (the three added by review)
```

Three of those twenty exist because of the adversarial review below, and they
are the ones that failed before it.

### The two fixtures

**The analytic one**, which the brief specifies and which exercises the
WRITERS: A4 landscape, 297 x 210 mm, a 100 mm line at (20,20)-(120,20), a
hidden line at y = 40, a circle of radius 10 at (200,100), and the text "TEST"
at (30,180). Deliberately asymmetric, because a forgotten or doubled y flip
cannot hide in a symmetric drawing.

**The production one**, which exercises the SCENE: a regenerated document with
two sheets, a base view, a projected view, an assembly view over two
occurrences (one standing behind the other, so there is real occlusion), a
dimension carrying an ISO 286 H7 fit, a note, a feature-control frame, a BOM
table and two balloons. It is what proves the boundary is COMPLETE — a scene
that could not carry something shows up here and nowhere else.

### The mapping to the checklist

| `TODO.md` item | Where |
| --- | --- |
| Vector scene representation | `SceneArc`, `Length width`, `DrawingScene`, `sheetScene()`, `drawDimension()` |
| PDF / SVG / DXF export | one writer each, no dependency |
| Preserve sheet size / scale | the page checked in all three formats; a 1:2 view drawn at half size with its pen and lettering unchanged |
| Preserve line types / weights | style to layer/linetype/dash in each format; weights in paper mm |
| Preserve dimensions / text / symbols | the production fixture, read back; and the text ENCODING, per format — see the adversarial review |
| Preserve hidden-line representation | dashed in, dashed out; turned off, gone, and nothing else moved |
| Validate PDF dimensions independently | page box read from the file, compared with 841.8898 x 595.2756 pt computed in the test |
| Validate SVG geometry structurally | parsed as elements and attributes, every coordinate checked |
| Validate DXF entities/read-back | parsed as group codes, every entity and coordinate checked |
| Validate deterministic output | byte-identical, five times, in all three formats |

## INDEPENDENT READ-BACK

**Every check reads the generated file with a parser written in the test.** No
writer is asked what it thinks it wrote. A writer agreeing with itself proves
nothing; the mistakes worth catching are serialization mistakes, and those are
only visible from outside.

```text
DXF   a group-code reader: code on one line, value on the next. That is the
      whole format, which is why forty lines can read it honestly.
SVG   an element and attribute scraper.
PDF   an object and content-stream reader: the /MediaBox, and the `m`/`l`
      operators converted back from points to millimetres IN THE TEST.
```

This is the honest limitation to state: the parsers are mine, not a third
party's. They share no code with the writers, and they read the real bytes, so
they catch a serialization mistake — but they cannot catch a case where writer
and reader are wrong about the format in the same way. Against that, the
formats chosen are ones whose correctness is externally checkable by eye
(`%PDF-1.4`, `$INSUNITS 4`, `<svg width="420.0000mm">`), and the numbers are
checked against figures computed independently of any helper.

### What the read-back measured

```text
page       DXF $EXTMAX 297.0000     SVG width="297.0000mm"
           PDF /MediaBox [0 0 841.8898 595.2756]  -- A4, in points
100 mm     DXF  x2 - x1 = 100.0000
line       SVG  "20.0000,190.0000 120.0000,190.0000"
           PDF  120.0000 - 20.0000 mm, converted back from points
circle     DXF  CIRCLE, centre (200.0000, 100.0000), radius 10.0000
           SVG  <circle cx="200.0000" cy="110.0000" r="10.0000">
           PDF  Bezier `c` operators, no circle operator exists
text       DXF  TEXT "TEST" at (30.0000, 180.0000), height 5.0000
           SVG  <text x="30.0000" y="30.0000" font-size="5.0000">TEST</text>
```

**The flip, measured.** The scene puts the line at y = 20 from the bottom; SVG
counts from the top, so it must read 190 on a 210 mm page. The text at y = 180
must read 30. Those two differ from their unflipped values, which is the point:
a missing flip gives 20 and 180, a doubled one gives 20 and 180 again.

## LINE TYPES, WEIGHTS AND HIDDEN LINES

**Styles come from what hidden-line removal already decided.** Nothing is
reclassified in the scene assembly and nothing in a writer:

```text
Visible sharp / outline  ->  continuous, 0.5 mm   (the thick line)
Hidden                   ->  dashed, 0.25 mm
Visible smooth (tangent) ->  continuous, 0.25 mm
Section hatch            ->  thin, 0.25 mm
Dimensions, extensions   ->  thin
The sheet frame          ->  continuous, 0.5 mm
```

Per format:

```text
SVG   stroke-width in mm, stroke-dasharray in mm
DXF   a LAYER and a LINETYPE per class (VISIBLE/HIDDEN/CENTRE/THIN,
      CONTINUOUS/DASHED/CENTER), and code 370 lineweight in hundredths of a
      millimetre. A receiving package turns hidden detail off by turning off
      a layer.
PDF   `w` for width and `d` for the dash array, both in POINTS
```

**A line weight never meets a view's scale.** Tested by scaling a drawing's
geometry and requiring the widths in the file to be unchanged, and again by
halving a view's scale in the production fixture and requiring the same stroke
widths and the same text heights.

**Hidden lines are rendered, never recomputed.** Turning a view's
`showHidden` off removes every dashed line from the scene and from the DXF's
HIDDEN layer — and the visible lines are compared element by element and are
IDENTICAL. Turning hidden detail off changes what is shown and nothing about
where anything is.

**Coincident lines were already resolved.** A lone cube produces NO hidden
lines: its back edges draw exactly where its front ones do, and P14-HLR-001's
ISO 128 precedence dropped the hidden one before the scene ever saw it. That
is why the occlusion test needed one occurrence standing behind another — and
finding that out is itself the check that the exporter is not re-running HLR.

## DETERMINISM

**Byte-identical, for all three formats.** Not "structurally the same": the
same bytes, five times over, and again against a separately constructed copy
of the same scene so the answer cannot come from anything remembered.

That is a choice about what goes IN the files rather than a normalisation
applied afterwards:

```text
PDF   no /CreationDate, no /ModDate, no /Producer, no /Creator, no /ID,
      no compression -- asserted absent by a test
SVG   fixed element and attribute order
DXF   fixed entity order, the scene's own
```

One number formatter for all three: fixed notation (never scientific, which
DXF and a PDF content stream cannot read), four decimals, `std::format` so no
locale can reach it, and negative zero written as zero — because `-0.0000` and
`0.0000` are the same point and different strings.

Tested under a comma-decimal locale, with `printf` shown writing `1,25`, and
the output required to be unchanged in every format.

## CLI EXPORT

Three commands, on `P14-CLI-001`'s spine:

```text
export-pdf <file.bcad> <file.pdf> [--sheet <selector>] [--configuration <name>]
export-svg ...
export-dxf ...
```

One path through all three: load, regenerate, assemble the sheet's scene, hand
it to a writer. The CLI chooses the writer and builds nothing.

**A drawing that does not regenerate is not written.** Exporting a sheet whose
references have stopped resolving would be publishing a drawing nobody can
trust, so it is a failure with a diagnostic and no file.

**A document with several sheets and no `--sheet` is refused rather than
guessed at**: writing the wrong page silently is worse than being asked which.

## ADVERSARIAL REVIEW

**A credible defect was found, and fixed before `[x]`.** It is recorded first
because it is the reason this milestone was qualified twice.

### The defect: the writers copied bytes where they had to transcode

The question that found it was the milestone's own gate turned on the text
rather than the geometry: *a writer must transcribe the scene — does it?*

The scene holds text as UTF-8. SVG declares `encoding="UTF-8"` and takes the
bytes, which is correct. **PDF and DXF are single-byte formats**, and both were
copying the UTF-8 bytes through:

```text
the scene says          Ø20            one character, U+00D8
UTF-8 is                C3 98          two bytes
PdfWriter escaped       \303\230       two bytes, as two characters
a reader applying /WinAnsiEncoding shows   Ã˜20
```

`±0.05` came out as `Â±0.05` the same way. DXF was worse in kind: R12 stores
text in the drawing's code page and the file **named no code page at all**, so
a reader fell back on its own locale's — the same drawing reading differently
on two machines.

This is not cosmetic and it is not a rendering nicety. `Ø` and `±` are the
diameter and symmetric-tolerance characters; they are on nearly every
dimensioned drawing BetterCAD can currently produce, from
`Annotations.cpp:191` and `Dimension.cpp:452`. **A diameter callout that reads
`Ã˜20` is a drawing that says something other than the model does**, which is
precisely the failure the export gate exists to prevent.

Nothing caught it because **no test had put a non-ASCII character through a
writer.** Every text assertion in the milestone used `"TEST"`, a part name or a
number.

### The fix

`src/io/TextEncoding.hpp`: a UTF-8 decoder and a WinAnsi encoder, shared by the
two writers that need one. SVG does not use it and does not need to.

```text
PdfWriter   literal() transcodes, then escapes the resulting BYTE
            -- \330 for Ø, which is the byte /WinAnsiEncoding wants
DxfWriter   $DWGCODEPAGE ANSI_1252 is DECLARED, and text is transcoded to it
```

Two smaller things came with it. The PDF centres text by an estimated width
taken from the **character** count, not the byte count — `Ø20` is three
characters and was being centred as four. And a control character now becomes
`?`, which matters for DXF specifically: a newline inside a code-1 value would
corrupt not the text but the **line structure** of a line-based format, and
every pair after it would be read one line out.

### The regression tests, shown to fail

Three cases, `[io][export][p14][text]`, stating every byte explicitly rather
than trusting the test file's own encoding. **They were run against the
pre-fix writers**, which is the only thing that makes a regression test worth
having:

**All three cases failed.** The assertions below are the ones the run
printed; the listing was truncated, so this is a floor and not a total:

```text
    CHECK( text == expected )                              DXF
    CHECK( text.find('\xC3') == std::string::npos )        DXF
    CHECK_THAT( content, !ContainsSubstring("\303\230") )  PDF
    CHECK( dxfText(*dxf) == "? 0.1 A" )                    the substitution
    CHECK( dxfText(*dxf) == "A?B?C?D" )                    malformed input
```

With the fix in place: 45 assertions, 3 cases, all passing.

### What could not be fixed, and why it is a limitation and not a defect

WinAnsi has no GD&T symbol, and **neither has any of PDF's fourteen standard
fonts** — not Symbol, not ZapfDingbats. `⌖` position, `⌭` cylindricity, `⏤`
straightness, `⏥` flatness and `⌰` runout have no glyph to map to, so no
encoding choice reaches them. Only an embedded font would, and a font
subsystem is not this milestone's scope and would put a binary asset in the
repository.

Those characters become `?`. That is a deliberate choice between two bad
options: **`?` is visibly missing, and nobody reads it as a tolerance.** A
substituted ASCII notation — `PERP`, `//`, `POS` — would have been inventing
GD&T semantics in an exporter, which the milestone's constraints forbid in as
many words. SVG carries all of them correctly today.

### The other questions

**Could this pass its tests and still be geometrically wrong?** The read-back
parsers are written in the tests and share no code with the writers, so a
serialization mistake is visible. They cannot catch writer and reader being
wrong about a format in the same way — stated as a limitation, and mitigated by
checking the numbers against figures computed independently of any helper
(`841.8898 pt` from `297 × 72 / 25.4` in the test).

**Is the scene's validation real, or does it only pass what is already
correct?** It rejected two cases from this milestone's own material: a
dimension with no placement, and a view with no placement, both of which sit at
`(0,0)` and draw off the page. The tempting fix was a CLI-side default. It was
**declined** — a default in the CLI and not in the core would have made the CLI
a second implementation of drawing semantics, which P14-CLI-001's gate forbids.
The example script and the tests place them instead, and the behaviour is
recorded in `examples/scripts/build_drawing.txt` where someone will meet it.

**Is the exporter secretly re-running HLR?** A lone cube produced no hidden
lines at all, which looked like a broken hidden-line test. It is not: the
cube's back edges project exactly onto its front ones and P14-HLR-001's ISO 128
precedence drops the hidden one **before the scene is built**. The occlusion
fixture was changed so one occurrence stands behind another. Finding that out
*is* the check — an exporter computing its own hidden lines would not have
agreed with the kernel's earlier decision.

**Did we weaken a test or move a tolerance?** No tolerance was changed in this
milestone. The export comparisons are exact string and byte equality, not
tolerant comparison, because a file is either the same or it is not.

**Hidden global state? Could Debug and Release differ?** The three writers are
pure functions of a `DrawingScene` and hold no state between calls. Byte
equality is asserted across five runs, across separately constructed copies of
the same scene, and under a comma-decimal locale. All three configurations are
qualified below.

**Could order of operations matter?** The scene fixes the order — views in
ascending ID order — and the writers iterate it. No unordered container reaches
the output.

**Did we cross an architectural boundary?** The writers take a
`DrawingScene` and nothing else; there is no `Document` in any of their
signatures to reach the model through. `TextEncoding.hpp` is a private header
beside its sources, as `ARCHITECTURE.md` requires, and `architecture.layering`
passes.

## FULL REGRESSION

**QUALIFIED TWICE.** The first qualification was abandoned part-way, not
because it failed — Debug had already passed 2165/2165 and Release had built
clean — but because the adversarial review found the encoding defect while it
ran. Changing a writer after the freeze voids the qualification, so it was
stopped and run again from clean on the fixed tree. The first run's logs are
not kept: they describe a tree that is not the one committed.

```text
preset         tests        warnings   no-op rebuild
debug          2168/2168    0          compiled 0, linked 0
release        2168/2168    0          compiled 0, linked 0
debug-shared   2168/2168    0          compiled 0, linked 0
```

2168 is +26 on P14-CLI-001's 2142, which is exactly the 26 entries this
milestone adds and no accidental loss anywhere else. The three text-encoding
cases are part of that count: the discarded first qualification reached 2165.

Three presets, each configured and built from a CLEAN tree, and each proved to
be a fresh binary by a no-op rebuild that compiled 0 files and linked 0.

```text
ctest --repeat until-fail:5
release        2167/2167
debug          2167/2167
```

Five consecutive passes of every selected test in both configurations. (2167
rather than 2168 because the selection is a filter, not the whole suite.)

The repeat selection is everything that reaches the export boundary: the scene
and its validation, the sheet assembly, the hidden-line classification the
styles come from, sections and hatch, dimensions, annotations, GD&T, BOM and
balloons, the views and sheets that place them, assemblies and configurations
underneath, the CLI, persistence, the layering checker and the reference
models. `Scene.hpp` and `Dimensions.hpp` changed, so everything that draws is
in scope whether or not it exports.

### The qualified tree is the committed tree

```text
                    before the first build   after the last test   
-----------------   ----------------------   ----------------------
apps                b4333caf4f123b67ec...    b4333caf4f123b67ec... 
include             dd241259cdb0732acf...    dd241259cdb0732acf... 
src                 5bd82f3d60fb7848ef...    5bd82f3d60fb7848ef... 
tests               3d83b52f4ee6828ebb...    3d83b52f4ee6828ebb... 
examples            621c626920107b3a37...    621c626920107b3a37... 
cmake               a84e909339b24bc7ff...    a84e909339b24bc7ff... 
CMakeLists.txt      a0adbb9c1d3583aab4...    a0adbb9c1d3583aab4... 
CMakePresets.json   951b53d6b7412e6057...    951b53d6b7412e6057... 
```

Identical, so nothing moved under the qualification. The full hashes are in
`qualification/qualification-times.txt`.

**The discarded run's trees differ, which is the point.** It qualified
`src b6cadeed...` and `tests 7545d1d3...`; this one qualified
`src 5bd82f3d...` and `tests 3d83b52f...`. The encoding fix changed both, and
no amount of "it had already passed 2165/2165" makes the earlier run evidence
for the tree being committed.

```text
qualification started  Fri 25/09/2026  6:15:04
qualification finished Fri 25/09/2026  7:52:55, 0 stage(s) failed
```

## KNOWN LIMITATIONS

Stated because they are real, not because they are comfortable.

**GD&T symbols reach SVG only.** `⌖ ⌭ ⏤ ⏥ ⌰ ∥ ⟂ ∠ ○ ↗` have no glyph in
WinAnsi, in an R12 code page, or in any of PDF's fourteen standard fonts. They
are written as `?` in PDF and DXF, and correctly in SVG. Closing this means
embedding a font, which is a subsystem of its own. **A drawing whose tolerances
must be read should be exported as SVG until then.** The substitution is
pinned by a test so it cannot change silently.

**The substitution is not reported.** A writer returns a document or an error,
and has no way to say "this was written, with three characters replaced". The
CLI therefore prints an ordinary success. Surfacing it is a diagnostics change
across the writer signatures and the CLI, deliberately left out of a milestone
whose scope is the boundary itself.

**PDF text is positioned by an estimated width.** Centred and right-aligned
text uses an average advance rather than Helvetica's real metrics, so it can
sit up to about half a character off. It moves TEXT and never geometry, and
only for anchors that are not left-aligned. Real metrics mean a font metrics
table, which belongs with font embedding above.

**An arc is validated by its whole circle.** `validate()` checks the arc's full
bounding box, not the extent actually swept, so an arc that stays on the page
while its circle would not is refused. Conservative in the safe direction — it
never passes something that draws off the page — but it can refuse a legal
drawing. No case in the reference models hits it.

**A view or dimension with no placement draws at the sheet's corner** and is
refused by the scene rather than exported. Correct behaviour, and an awkward
first encounter; recorded in `examples/scripts/build_drawing.txt`.

**Malformed UTF-8 reaches SVG unfiltered.** PDF and DXF replace it, because
they transcode. SVG passes bytes through, so invalid UTF-8 in a part name would
produce an SVG that is not well-formed XML. The text would have to arrive
invalid from the document, which the JSON layer is not known to permit. Not
demonstrated either way, and not claimed as safe.

**The read-back parsers are ours.** They share no code with the writers and
read the real bytes, so they catch a serialization mistake; they cannot catch
writer and reader misreading a format the same way. No third-party validator
(Ghostscript, a DXF library, an SVG validator) was run — none is in the
project's toolchain, and adding one is a dependency decision this milestone is
not authorized to make.

**Scope, deliberately.** No DXF import, no 3D DXF, no PDF compression, no
embedded fonts, no colour, no raster. One sheet per file.

## RESULT

```text
TASK:            P14-EXPORT-001 -- PDF / SVG / DXF export
IMPLEMENTATION:  the DrawingScene completed (SceneArc, line weights, the sheet
                 assembly, a dimension's drawn form, page validation), and one
                 writer per format taking a scene and nothing else
TESTS:           2168/2168 in debug, release and debug-shared, 0 warnings;
                 5x determinism repeat 2167/2167 in release and in debug
VALIDATION:      every file read back by an independent parser written in the
                 test; PDF page box against 841.8898 x 595.2756 pt computed in
                 the test; byte-identical output five times in all three formats
ADVERSARIAL:     one credible defect found and fixed -- PDF and DXF copied
                 UTF-8 bytes into single-byte encodings, so a diameter callout
                 read "Ã˜20". Three regression tests, shown failing against the
                 pre-fix writers. Qualification re-run from clean on the fix.
RESULT:          PASS
EVIDENCE:        docs/verification/P14-EXPORT-001/
TODO:            P14-EXPORT-001 [x], 15 checkboxes; Next -> P14-REFMOD-001
```

The gate the brief set was that an exporter must never inspect the CAD model to
calculate engineering meaning. It cannot: a writer's signature is
`Result<std::string>(const DrawingScene&)`, and there is nothing in it to reach
a model through. What the review showed is that **transcription is not the easy
half.** Copying bytes is not transcribing them, and the drawing that came out
said `Ã˜20` where the model said `Ø20` — a wrong callout produced by an
exporter that never touched the model at all.
