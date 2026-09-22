# ADR-018 — The projection convention is sheet intent, defaulting to first angle

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-VIEW-001
Builds on: ADR-011 (drawing intent is canonical)
           ADR-013 (view orientation and drawing scale)
           ADR-017 (drawing identity model)
```

## Context

ADR-013 fixed what a view *looks along* and how big it draws. It said nothing
about where a **projected** view is placed relative to the view it derives
from, and that is a separate convention with a separate standard behind it.

Given a Front view, a Top view can go above it or below it, and a Right view
can go to its right or its left. Both answers are correct engineering; they
are different national standards:

```text
first angle    ISO 128 / ISO 5456, used across Europe and Asia.
               The object is imagined between the viewer and the plane, so
               each view projects THROUGH the object onto a plane behind it.
               Top goes BELOW the front. Right-side goes to the LEFT.

third angle    ASME Y14.3, used in the United States and Canada.
               The plane is between the viewer and the object, so each view
               projects onto a plane in front of it.
               Top goes ABOVE the front. Right-side goes to the RIGHT.
```

The two place every projected view on the opposite side. A drawing read in
the wrong convention is not slightly wrong — it is mirrored, and a part
machined from it is wrong in a way that inspection may not catch until
assembly.

This has to be decided before any projected view is placed, because the
placement rule is the thing being written. `P14-VIEW-001`'s own brief says so:
"Define whether the project follows first-angle or third-angle projection.
This must not remain implicit."

## Options

1. **Third angle, fixed in code.**
2. **First angle, fixed in code.**
3. **Per-sheet intent**, persisted, with a default.

## Decision

**Option 3.** The projection convention is a field of `SheetDefinition`,
persisted as drawing intent, defaulting to **first angle**.

```cpp
enum class ProjectionConvention : std::uint8_t { FirstAngle, ThirdAngle };
```

A projected view derives its placement from its parent and the convention of
the sheet it sits on.

## Rationale

The convention is not a property of the software; it is a property of the
drawing, and it is one that a reader must be told. ISO 128 requires the
convention to be indicated on the drawing by its symbol, and every real
drawing office stamps it in the title block. A value that has to be *shown*
to be read correctly is intent by definition (ADR-011), so it belongs in the
document rather than in the code that renders it.

Fixing either convention in code (Options 1 and 2) would also make BetterCAD
unusable for one hemisphere without a rework that reaches every projected
view's placement. That is a large, late change to avoid a small, early one.

**The default is first angle because the rest of the project is ISO.** The
codebase already implements ISO 965 threads, ISO 286 tolerances, ISO 273
clearance holes, ISO 216 sheets and ISO 5455 scales; `P14-SHEET-001` chose the
ISO A series and deliberately declined to approximate ANSI sizes. A drawing
system that used ISO paper and ISO scales and then placed its views by ASME
would be internally inconsistent. Someone working to ASME sets the field;
someone working to ISO writes nothing.

**Why the sheet and not the document.** ISO 128 places the symbol on the
sheet, and the title block that carries it is already a `SheetDefinition`
field (`P14-SHEET-001`). Putting the convention beside it keeps the symbol and
the placement rule reading from one value, so a sheet cannot show one
convention and be laid out by the other.

The cost is that two sheets of one drawing could in principle carry different
conventions, which no sensible drawing does. That is accepted rather than
prevented: a cross-sheet consistency rule is a validation question for a
later milestone, and the alternative — a document-level field with a
per-sheet symbol read from somewhere else — is the split that lets the two
disagree.

Adding a field to `SheetDefinition` costs no format version bump. The policy
at `include/bettercad/io/DocumentFile.hpp:30-58` requires one only for "a
change that an existing reader would get **wrong**", and an added optional key
is not that; a sheet written without it reads as first angle, which is what a
sheet written before this ADR meant.

## Consequences

- `SheetDefinition` gains one field. Written only when it is not the default,
  so every sheet already committed is byte-identical.
- A projected view has **no orientation of its own**. It has a parent and a
  direction, and its orientation and placement are derived from the parent's
  plus the sheet's convention. A projected view that stored its own
  orientation could contradict its parent, which is the defect this shape
  prevents rather than validates against.
- Changing a sheet's convention re-places every projected view on it. That is
  correct — it is what changing the convention means — and it is derived, so
  nothing has to migrate.
- The symbol itself is not drawn. It is derived presentation, and belongs
  with the rest of the title block's graphics in a later milestone.
- A drawing whose sheets disagree is expressible. Recorded as a limitation.

## Rejected alternatives, and what would make them right

**Fixing third angle (Option 1)** would be right for a product targeting only
North America. Nothing else in BetterCAD does.

**Fixing first angle (Option 2)** is tempting because it is the default
anyway, and it would save a field. It becomes wrong the first time anyone
needs an ASME drawing, and at that point the change reaches every projected
view's placement rather than one enum.

## Verification

For `P14-VIEW-001`: under first angle a Top view derived from a Front view is
placed **below** it and a Right view **to its left**; under third angle both
are opposite; the two conventions place the same view on opposite sides of
the same parent, checked as coordinates rather than as a flag; a sheet
written with the default convention is byte-identical to one written before
this ADR; and a projected view's orientation is derived from its parent, so
no test can give one an orientation its parent contradicts.
