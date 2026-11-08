# My own finding, before the reviews report: the reinterpret_cast is avoidable

## What is there now

`checkedArrays` flattens the two typed vector spans to `const double*`:

```cpp
out[n++] = Checked{"the face gradient", input.faceGradient.size(), faces, 3,
                   reinterpret_cast<const double*>(input.faceGradient.data())};
```

and phase 11 walks `entries * stride` doubles through that pointer.

## Why it is wrong, and not merely ugly

1. **It is undefined behaviour under the standard's object model.** Reading a
   `double` through a pointer obtained by `reinterpret_cast` from a
   `CartesianVector<Q>*` is not a valid access path. It works on every compiler
   anyone will use, and "works everywhere" is not the standard this project
   holds itself to — the layering test exists because "nobody would do that" is
   not a guarantee either.
2. **It assumes a layout nothing asserts.** `CartesianVector<Q>` is three
   `Quantity<D>`, each holding one `double`. Nothing in the file requires
   `sizeof(CartesianVector<Q>) == 3 * sizeof(double)` or that there is no
   padding. If a `Quantity` ever gained a member — a debug tag, a unit
   provenance field — the walk would read the wrong doubles and report
   finiteness of something else, silently.
3. **The typed accessor already exists.** `CfdData.hpp` declares
   `template <typename Q> bool isFinite(const CartesianVector<Q>&)`, which is
   exactly this check, written once, in the module that owns the type. Reaching
   past it with a cast is the opposite of ARCHITECTURE.md's "units are part of
   correctness": it strips the type in order to inspect it.
4. **`stride` exists only to serve the cast.** Removing the cast removes the
   field, and with it the class of bug the field already caused once — the first
   draft derived the stride by inspecting two characters of the array's NAME,
   and the test matched `"the face flux phi"` as well as `"the face gradient"`,
   so the scalar `phi` array would have been walked three doubles per entry and
   read three times past its end.

## The fix

Split the list in two, because there are genuinely two kinds of array and
pretending otherwise is what forced the cast:

```cpp
/// A scalar array: one double per entry.
struct CheckedScalars {
    std::string_view name;
    std::span<const double> values;
    std::size_t expected;
};

/// A vector array: one CartesianVector per entry, checked through the typed
/// `isFinite` that CfdData.hpp already owns.
template <typename Q>
struct CheckedVectors {
    std::string_view name;
    std::span<const CartesianVector<Q>> values;
    std::size_t expected;
};
```

Phase 10 checks the lengths of both lists; phase 11 walks the scalar list with
`std::isfinite` and the two vector arrays with `cfd::isFinite`, naming the
offending entry index rather than a double offset — which is also a better
diagnostic, because "entry 7 of the pressure gradient" locates a cell and
"double offset 21" does not.

The two vector arrays are of DIFFERENT quantity types (`VelocityGradient3` and
`PressureGradient3`), so they cannot share one list without erasing the type
again. They are checked by two explicit calls to one small template, which is
two lines and keeps the type.

## What this does not change

The phase ORDER is unchanged: every length before every value, and the same
arrays in the same order in both phases, which is the property the list
structure exists to guarantee. The guarantee is now carried by two lists walked
in a fixed sequence rather than one, so the comment stating it has to name both.
