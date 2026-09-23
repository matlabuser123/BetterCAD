# ADR-022 — A BOM is derived and has no identity; a table and a balloon are annotations

```text
Status:    Accepted
Date:      2026-09-24
Milestone: P14-BOM-001
Builds on: ADR-011 (drawing intent is canonical, projection is derived)
           ADR-016 (the drawing scene is the export boundary)
           ADR-017 (drawing identity model — which DEFERRED this decision here)
           ADR-021 (an assembly view is one hidden-line problem)
Answers:   ADR-017's open question, in its own words: "whether either needs
           its own identity is P14-BOM-001's to decide, on evidence, and this
           ADR declines to allocate types in advance"
```

## Context

`ADR-017` made `Sheet`, `View`, `Dimension` and `Annotation` document objects
and stopped there, saying in as many words that a BOM table "is an annotation
kind" and a balloon "is an annotation that points at an occurrence" — but that
whether either needs identity of its own was to be decided here, on evidence.
The evidence now exists, because `P14-ASM-001` shipped the thing a BOM needs:

```text
assembly::activeComponents()   the active occurrence set, under the active
                               configuration and both layers of suppression
drawnOccurrences(view)         the same set, as what an assembly view draws
ComponentDefinition::part      an ObjectReference: the qualified identity of
                               the part definition an occurrence places
```

Three questions have to be answered together, because the wrong answer to the
first makes the others unanswerable:

```text
identity     what, if anything, does a BOM row or a quantity have identity AS
grouping     what makes two occurrences "the same part"
numbering    what an item number is a function of
```

## Options

### 1. `Bom` and `Balloon` as new `DocumentObject` kinds, with stored rows

A `Bom` object owning `BomRow` sub-objects, each with a persisted item number,
quantity and part reference; a `Balloon` object naming a row.

```text
+ an item number could be edited and kept, which some drawing offices want
+ a balloon naming a ROW is a short, direct reference
- THE QUANTITY BECOMES A STORED FACT THAT CAN DISAGREE WITH THE ASSEMBLY.
  That is precisely the failure ADR-011 exists to prevent, and a wrong
  quantity on an issued drawing is a wrong purchase order
- two identity schemes appear that nothing else needs: row identity, and
  balloon-to-row references that must be repaired whenever rows appear or go
- suppressing the last occurrence of a part leaves a row object with nothing
  in it; the document now has to answer what an empty row means
- a balloon would name a row rather than the thing it points at, so the
  drawing would no longer know WHICH bolt it labels -- only which line of a
  table, which is the wrong way round
```

### 2. `Bom` as a `DocumentObject`; the balloon as an annotation

```text
+ the table's placement and columns get a natural home
- the BOM still has identity, and identity invites storage: the first person
  who wants a frozen item number stores it here and the quantity follows
- it duplicates what a sheet and a view already express. The table has no
  data of its own beyond where it sits and which assembly it lists, and both
  of those an annotation already carries
```

### 3. Neither has identity. The BOM is **derived**; the table and the balloon are **annotation kinds**

`billOfMaterials(document, view)` computes rows from the view's active
occurrences on every call. `AnnotationType::BomTable` is an annotation with no
target that draws the table where it is placed; `AnnotationType::Balloon` is an
annotation whose target is the OCCURRENCE, and whose displayed number is
resolved through the BOM at draw time.

```text
+ a quantity cannot be stale, because there is no quantity stored anywhere
+ no new identity scheme at all: annotations already have IDs, placement,
  text height, a view, a target vocabulary and persistence
+ a balloon points at the OCCURRENCE, which is what it labels on the paper,
  and the item number falls out of that. It cannot be pointing at the right
  bolt and showing the wrong number, because the number is a function of the
  bolt
+ suppression needs no special case: a suppressed occurrence is not in the
  active set, so it is in no row, and a balloon on it fails to resolve rather
  than silently labelling a different one
- an item number cannot be frozen or hand-edited. A drawing office that
  reissues a drawing with renumbered items has no answer here
- the BOM is recomputed on every call, including once per balloon drawn
```

## Decision

**Option 3.**

The deciding argument is the one ADR-011 already made and this milestone makes
concrete: **a quantity is not intent.** Nobody *decides* that there are four
brackets; there are four brackets because four occurrences of the bracket are
active. Storing that number creates a second answer to a question the assembly
has already answered, and the two can differ — silently, on an issued drawing.

The second argument is about what a balloon is for. A balloon labels a
component on a drawing. Under option 1 it names a table row, so the chain runs
balloon → row → part, and the occurrence — the actual thing with the leader
pointing at it — is not in the chain at all. Under option 3 it runs

```text
balloon -> occurrence -> part definition -> row -> item number
```

which is the order the information actually flows in, and every link is a
qualified identity that already exists.

### What follows: grouping, quantity, numbering

```text
grouping    by the part definition's ObjectReference -- the identity
            ComponentDefinition::part already carries. NEVER by display name,
            by geometry, by a shape hash or by traversal order. Two
            separately defined parts that happen to be identical boxes are
            two rows, and there is a test that builds exactly that
quantity    the number of ACTIVE occurrences grouped into the row. It is
            `occurrences.size()`, so a row cannot exist with quantity zero
            and a quantity cannot disagree with the list it counts
ordering    rows in ascending part ObjectId. Stable, independent of the order
            components were created in, and independent of names and geometry
numbering   1..N over that order, contiguous, recomputed every time
```

**Numbering is therefore compact, not retained.** If the last occurrence of
item 2 is suppressed, its row goes and the old item 3 becomes item 2. That is
stated here because ADR-017's successor milestone was told not to leave it
accidental: with nothing persisted, a retained number would have nowhere to be
retained, and inventing a place for it would be option 1 through the back
door. A milestone that genuinely needs frozen item numbers will have to add
stored intent and say so.

## Consequences

```text
+ BomRow carries its occurrences, not just a count, so grouping never loses
  provenance and a balloon, a selection or a later traceability feature can
  go from a row back to the individual instances
+ No file format change beyond two new annotation kinds and their fields. A
  BOM table stores where it sits; a balloon stores which occurrence it points
  at and where it sits. Neither stores a number
+ A stale BOM is unrepresentable. The regression that proves it saves a
  document at quantity 4, changes the model to 3, reloads, and requires 3
- billOfMaterials() is recomputed per call, and drawing N balloons computes
  it N times. Nothing caches it, for the reason ADR-011 and ADR-014 give
- No frozen or hand-edited item numbers, and no manual row ordering
- The BOM lists what its VIEW draws, so it requires an assembly view. A BOM
  of a view that draws one object is refused rather than given a single row
```
