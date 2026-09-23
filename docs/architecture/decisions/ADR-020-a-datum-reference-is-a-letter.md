# ADR-020 — A datum reference is a letter, and undefined datums are reported

```text
Status:    Accepted
Date:      2026-09-23
Milestone: P14-TOL-001
Builds on: ADR-011 (drawing intent is canonical, presentation is derived)
           ADR-012 (a drawing references only what ADR-004 permits)
           ADR-016 (the drawing scene is the export boundary)
           ADR-017 (sheets, views, dimensions and annotations are DocumentObjects)
```

## Context

A feature-control frame says what a feature must be held to and **against
which datums, in order**: `⌖ | Ø0.2 | A | B | C`. `A|B|C` is a different
requirement from `B|A|C`, so the order is the meaning.

The question this milestone has to settle is what the `A` in that frame
actually *is*. `P14-ANNO-001` already ships a datum feature symbol — an
annotation of type `Datum` carrying a single letter, pointed at a plane, an
axis or a named face by ADR-012's vocabulary. So there are two objects in
play, the symbol that *defines* a datum and the frame that *cites* one, and
the decision is how the second reaches the first.

It matters beyond this milestone. `P14-STREF-001` will harden drawing-to-model
references, and `P14-ASM-001` will put frames on assembly views where the same
part appears more than once. Whatever a datum reference is made of has to
survive both.

Three things have to come out of it:

```text
fidelity      a drawing must mean what ISO 1101 says it means, and that
              standard is written in letters
robustness    an engineer works in the order they work in; placing a frame
              before its datum symbol is normal practice, not an error
honesty       a drawing that cites a datum nobody defined must not be able
              to pass for a finished one
```

## Options

### 1. A letter, and nothing else

`DatumReference{char letter}`, held in an ordered `std::vector`. The frame
knows the letter; the datum feature symbol elsewhere in the document knows the
letter and the geometry. Nothing links the two objects.

```text
+ exactly what ISO 1101 defines; the model says what the standard says
+ authoring order cannot matter, because there is nothing to resolve
+ a frame is complete on its own, so persistence and the scene are trivial
+ nothing to rebind, so nothing can silently rebind to the wrong face
- a frame can cite a datum that does not exist, and nothing says so
- renaming a datum symbol from B to C silently changes what every frame
  citing B or C requires
```

### 2. An `ObjectId` naming the datum annotation

`DatumReference{AnnotationId datum}`. The frame points at the symbol object,
and the letter is read from it when the frame is drawn.

```text
+ a dangling reference is impossible: the ID either resolves or it does not
+ renaming a datum updates every frame citing it, with no further work
- a frame cannot be placed before its datum symbol, which is how engineers
  work; the model would dictate the order of authoring
- the letter becomes derived, so a frame is no longer readable on its own
  and drawing one needs a document lookup that can fail
- a datum is a property of the PART, and this makes it a property of one
  drawing's annotation: a second sheet lettering its own datum A would be a
  different datum A
- ADR-012 permits ObjectId, so this is legal -- but it binds a standard's
  vocabulary to this repository's identity model for no gain the standard
  asks for
```

### 3. A reference to the geometry itself

`DatumReference{PlaneReference | AxisReference | FaceName}` — the frame cites
the face or axis directly, and the letter is only a label drawn from whichever
datum symbol happens to sit on that geometry.

```text
+ the strongest possible link: the requirement points at the surface
+ survives renaming a datum symbol, because the letter was never the identity
- it is not GD&T. ISO 1101 frames cite datums, and a datum is established by
  a datum FEATURE plus a rule for deriving it; a face is not a datum
- two frames citing "A" would hold two independent copies of the reference
  and could drift apart
- it puts a stable-reference problem inside every frame, which is exactly
  what P14-STREF-001 exists to solve once, elsewhere
- a frame would stop being persistable as text a reader can check
```

## Decision

**Option 1: a datum reference is a letter, held in an ordered vector, and
nothing binds it to the symbol that defines it.**

The letter is what the standard defines, what an inspector reads, and what a
drawing prints. Modelling it as anything else would be modelling something
other than GD&T and calling it GD&T.

The cost of option 1 is its last two lines, and only the first of them is
serious. A frame citing an undefined datum is answered NOT by refusing the
frame — which would make the order an engineer works in part of what is legal
— but by **reporting it**:

```cpp
Result<std::vector<char>> undefinedDatums(const Document&, AnnotationId frame);
```

It is computed on every call from the datums that exist now, in the order the
frame cites them, so it cannot go stale: define `C` and it disappears, delete
the symbol and it comes back. A half-finished drawing is not a wrong drawing,
and this is what tells the two apart.

The renaming hazard is recorded rather than solved. Renaming a datum symbol
from `B` to `C` does change what every frame citing either letter requires,
and this model cannot detect it. That is true of the paper drawing too — the
letters are the only link there as well — but it is a real limitation of the
foundation and belongs with `P14-STREF-001`, which will have to decide whether
a rename is an operation the document offers at all.

## Consequences

```text
+ FeatureControlFrame is a value: a characteristic, a zone, a magnitude and
  an ordered vector of letters. It persists as itself and needs no lookup.
+ The order is the meaning and is held in a vector. Never a set, never a map:
  A|B|C and B|A|C compare unequal, serialize differently and draw differently,
  and there is a test for each.
+ ONE datum-letter rule, `validateDatumLetter`, is used by the datum feature
  symbol and by every datum a frame cites, so the two cannot come to disagree
  about what a datum may be called. ISO 5459: a capital A to Z, never I, O
  or Q.
+ A frame cannot silently rebind to another feature, because it binds to no
  feature.
- A frame citing a datum nobody defined draws normally; only undefinedDatums()
  says otherwise, and a caller has to ask. A drawing checker belongs to a
  later milestone and this is the query it will be built on.
- Renaming a datum symbol is undetectable here, and is left to P14-STREF-001.
```
