#pragma once

// P19-DOF-001 -- the CFD field layout: where each unknown lives, how long an
// array of it is, and which subscript addresses one component (ADR-066).
//
// WHAT THIS ADDS TO ADR-064 AND ADR-065. The topology named the cells and the
// faces; the metrics measured them. Neither says where a pressure lives, how long
// a velocity array is, or what a momentum solve's row number means. Both files
// said, in the same words, that it was this milestone's:
//
//     No field storage (P19-DOF-001).
//
// AND THERE IS STILL NO FIELD STORAGE HERE (ADR-066, Decision 1). This file holds
// counts, translations and a binding. It holds no pressure, no velocity and no
// flux, and it is not a container: a field is the caller's own `std::span`, whose
// LENGTH comes from `size(CfdVariable)` and whose SUBSCRIPT has a type. The first
// milestone that computes a field decides how the values are owned, with a use in
// hand.
//
// THE LAYOUT, FROZEN (ADR-058's collocated scheme, which is what decides it):
//
// ```text
// p    Pressure   cell-centred     1 component    [0, cells)           Pa
// U    Velocity   cell-centred     3 components   [0, 3 cells)         m/s
// phi  MassFlux   face-centred     1 component    [0, faces)           kg/s
// ```
//
// A CELL-CENTRED VECTOR IS COMPONENT-BLOCKED, `comp * cells + cell`, WHICH IS THE
// OPPOSITE OF P17's INTERLEAVING, and the reason is the authorized solver rather
// than taste. SIMPLE-family coupling (ADR-058) solves momentum SEGREGATED: three
// scalar systems of `cells` rows, one component at a time, and the pressure
// correction is a fourth of the same shape. Blocked makes all four contiguous
// runs, so one scalar interface serves them. P17 interleaves because a Tet4
// stiffness matrix is a full 12x12 block coupling the components of one element;
// the segregated momentum predictor assembles no row that couples u to v.
// ADR-066, Decision 3 carries the comparison and the hazard that follows.
//
// NO MESH HANDLE CAN SUBSCRIPT ANYTHING HERE, which is the checklist's "no raw
// mesh-ID indexing" made structural rather than promised. There is no `ElementId`
// and no `NodeId` accessor, no overload that takes one, and no handle-keyed
// container: the translation between a CellIndex and the ElementId it is stays in
// `CfdTopology`, where it is a binary search over a sorted vector, and a consumer
// that wants it asks the topology. Every container here is sized by a COUNT.
//
// The one handle this file touches is the `MeshId` inside the `MeshStamp` it
// carries for identity, which `mesh()` returns and a refusal formats. An
// identity is not a subscript, and that is the whole distinction; an earlier
// version of this paragraph said "no mesh handle is offered anywhere in this
// file", which was not true of `mesh()` and is the kind of blanket sentence an
// adversarial review is for.
//
// WHAT THIS FILE DOES NOT DO. No field values, no boundary condition and no
// resolution of one (P19-BC-001); no initial condition applied to cells
// (P19-IC-001); no gradient, no operator, no matrix and no solve. It defines the
// index space those milestones share, and the binding that tells them when it has
// stopped meaning anything.

#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

class CfdModel;

// ---------------------------------------------------------------------------
// Where a field lives
// ---------------------------------------------------------------------------

/// The three places a finite-volume field can be stored.
///
/// ```text
/// Cell           one value per cell            [0, cells)
/// Face           one value per face            [0, faces)
/// BoundaryFace   one value per boundary face   [0, boundaryFaces)
/// ```
///
/// `BoundaryFace` IS A LOCATION OF ITS OWN and not merely the tail of `Face`
/// (ADR-066, Decision 2). A prescribed inlet velocity or a wall shear exists only
/// on the boundary, so an array over every face would be mostly unused; and the
/// boundary range is grouped by patch (ADR-064), so a boundary array is the one a
/// boundary condition can fill patch by patch in contiguous runs. ADR-064 drew the
/// same division for the same reason when it gave `BoundaryFaceIndex` its own type.
enum class CfdFieldLocation : std::uint8_t { Cell, Face, BoundaryFace };

/// The three, in declaration order.
inline constexpr std::array<CfdFieldLocation, 3> kCfdFieldLocations{
    CfdFieldLocation::Cell, CfdFieldLocation::Face, CfdFieldLocation::BoundaryFace};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdFieldLocation location) noexcept;

/// Whether @p location is one of the three this build defines.
///
/// A `std::uint8_t` enum, so `static_cast<CfdFieldLocation>(7)` is representable
/// and names nothing; a switch with no default falls through such a value and
/// reaches a lookup. P17-DOF-001's `isRecognised` exists for the same reason.
[[nodiscard]] constexpr bool isRecognised(CfdFieldLocation location) noexcept {
    for (const CfdFieldLocation candidate : kCfdFieldLocations) {
        if (candidate == location) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// The variables, frozen with their dimensions
// ---------------------------------------------------------------------------

/// The three fields of a laminar incompressible finite-volume solution (ADR-058).
///
/// NOT AN OPEN SET. A fourth variable is a formulation change -- turbulence, a
/// species, an energy equation -- and ADR-058 refuses all of them in v1, so a
/// variable added here would be a visible change to a frozen table rather than an
/// implicit extension.
enum class CfdVariable : std::uint8_t { Pressure, Velocity, MassFlux };

/// The three, in declaration order, which is the order `kCfdFields` is in.
inline constexpr std::array<CfdVariable, 3> kCfdVariables{CfdVariable::Pressure, CfdVariable::Velocity,
                                                          CfdVariable::MassFlux};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdVariable variable) noexcept;

/// Whether @p variable is one of the three this build defines.
[[nodiscard]] constexpr bool isRecognised(CfdVariable variable) noexcept {
    for (const CfdVariable candidate : kCfdVariables) {
        if (candidate == variable) {
            return true;
        }
    }
    return false;
}

/// One row of the frozen variable table: where a variable lives, how many
/// components it has, what its dimension is, and the symbol the literature uses.
///
/// THE DIMENSION IS CARRIED RATHER THAN DESCRIBED, which is what makes "units are
/// part of correctness" a compile-time fact at this boundary instead of a sentence
/// a later milestone can drift from. The static assertions below tie each row to
/// the catalog's own dimension, so a momentum assembly that stored a pressure
/// where the velocity field goes would have to disagree with this table to
/// compile.
struct CfdFieldDescriptor {
    CfdVariable variable = CfdVariable::Pressure;
    CfdFieldLocation location = CfdFieldLocation::Cell;
    /// 1 for a scalar, 3 for a Cartesian vector. Never 0.
    std::size_t components = 1;
    Dimension dimension{};
    /// The symbol the finite-volume literature uses: `p`, `U`, `phi`.
    std::string_view symbol{};

    friend constexpr bool operator==(const CfdFieldDescriptor&, const CfdFieldDescriptor&) = default;
};

/// The table, frozen here (ADR-066, Decision 2).
///
/// `phi` IS A MASS FLOW RATE, kg/s, NOT A MASS FLUX, kg/(m^2 s), despite the name
/// every text uses. It is the face-INTEGRATED quantity `rho (U . Sf)`, which is
/// what a control-volume balance sums over a cell's four faces; the per-area form
/// would have to be multiplied by `faceArea` at every site, and the site that
/// forgot would be a conservation error that still looked dimensionally plausible.
/// The name follows the literature and the dimension follows the arithmetic, and
/// both are written down so they cannot be confused.
inline constexpr std::array<CfdFieldDescriptor, 3> kCfdFields{
    {{CfdVariable::Pressure, CfdFieldLocation::Cell, 1, dimensions::pressure, "p"},
     {CfdVariable::Velocity, CfdFieldLocation::Cell, kVectorComponents.size(), dimensions::velocity, "U"},
     {CfdVariable::MassFlux, CfdFieldLocation::Face, 1, dimensions::mass / dimensions::time, "phi"}}};

/// The row for @p variable, or none if this build does not define it.
///
/// An optional rather than a reference with a precondition: an unrecognised
/// enumerator is representable, and a lookup that trusted its argument would index
/// past the end of the table.
[[nodiscard]] constexpr std::optional<CfdFieldDescriptor> descriptorOf(CfdVariable variable) noexcept {
    for (const CfdFieldDescriptor& descriptor : kCfdFields) {
        if (descriptor.variable == variable) {
            return descriptor;
        }
    }
    return std::nullopt;
}

/// The table is in `kCfdVariables` order, so index i of one is index i of the
/// other. A function over the two arrays rather than a comment, so a reordering of
/// either cannot leave them disagreeing -- P17-DOF-001's device.
static_assert([] {
    for (std::size_t i = 0; i < kCfdVariables.size(); ++i) {
        if (kCfdFields[i].variable != kCfdVariables[i]) {
            return false;
        }
    }
    return true;
}());

/// Each row's dimension is the catalog's, not a transcription of it.
static_assert(kCfdFields[0].dimension == dimensions::pressure);
static_assert(kCfdFields[1].dimension == dimensions::velocity);
static_assert(kCfdFields[2].dimension == dimensions::mass / dimensions::time);

/// A velocity has exactly as many components as there are Cartesian directions,
/// and the two scalars have one. Written as assertions because the component count
/// is what every array length below is multiplied by.
static_assert(kCfdFields[0].components == 1);
static_assert(kCfdFields[1].components == kVectorComponents.size());
static_assert(kCfdFields[2].components == 1);

/// And NO row has zero components, which `CfdFieldDescriptor` states as a rule.
/// A loop over the table rather than three more literals, so the rule holds for a
/// fourth row as well: an array length of zero would make a field with no storage
/// look like a field of an empty mesh.
static_assert([] {
    for (const CfdFieldDescriptor& descriptor : kCfdFields) {
        if (descriptor.components == 0 || descriptor.symbol.empty()) {
            return false;
        }
    }
    return true;
}());

// ---------------------------------------------------------------------------
// The one new subscript
// ---------------------------------------------------------------------------

/// One component of one cell-centred vector field, in `[0, 3 cells)`.
///
/// COMPONENT-BLOCKED (ADR-066, Decision 3):
///
/// ```text
///     CellVectorIndex value  =  component * cells + cell
///
///     component X (0)  ->  [0,        cells)
///     component Y (1)  ->  [cells,  2 cells)
///     component Z (2)  ->  [2 cells, 3 cells)
/// ```
///
/// THE ONLY INDEX TYPE THIS MILESTONE ADDS. A cell-centred scalar is indexed by
/// `CellIndex`, a face field by `FaceIndex` and a boundary field by
/// `BoundaryFaceIndex` -- ADR-064's types, reused rather than renamed, because a
/// second name for the same range over the same meaning is the second addressing
/// space ADR-064 went to some length not to create (ADR-066, Decision 4).
///
/// ZERO IS A VALID SUBSCRIPT and a default-constructed one is `kInvalid`, NOT
/// component X of cell 0. That is ADR-064's inversion, kept: a subscript whose
/// first legal value is 0 must never be assignable to a handle whose first legal
/// value is 1, in either direction.
///
/// AND IT DOES NOT CONVERT TO A `CellIndex`, which matters more here than
/// elsewhere. Component X's block IS the cell-centred scalar array, so
/// `vectorIndex(cell, X).value() == cell.value()`: the two subscripts coincide
/// numerically and the type system is the only thing that separates them. The
/// compile-fail group proves the conversion is refused in both directions.
class CellVectorIndex {
public:
    using ValueType = std::size_t;
    static constexpr ValueType kInvalid = static_cast<ValueType>(-1);

    /// The invalid subscript. NOT component X of cell 0.
    constexpr CellVectorIndex() noexcept = default;

    [[nodiscard]] static constexpr CellVectorIndex fromValue(ValueType value) noexcept {
        CellVectorIndex index;
        index.value_ = value;
        return index;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalid; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const CellVectorIndex&, const CellVectorIndex&) = default;
    friend constexpr auto operator<=>(const CellVectorIndex&, const CellVectorIndex&) = default;

private:
    ValueType value_{kInvalid};
};

/// The bound the blocked arithmetic relies on, proved rather than asserted in
/// prose: a mesh holds at most 2^32 - 1 elements (its handles are 32-bit and
/// strictly increasing), so three components per cell is inside a 64-bit `size_t`
/// by ten orders of magnitude.
///
/// AND IT IS A 64-BIT ASSUMPTION, DELIBERATELY NOT HIDDEN. On a platform with a
/// 32-bit `size_t` this assertion is FALSE and this header does not compile --
/// `3 * (2^32 - 1)` does not fit -- which is the loud failure rather than the
/// silent overflow `vectorIndex` would otherwise perform. BetterCAD builds
/// 64-bit; if a 32-bit preset is ever added, this is the line that says so first.
static_assert(static_cast<std::uint64_t>(kVectorComponents.size()) *
                      static_cast<std::uint64_t>(std::numeric_limits<meshing::ElementId::ValueType>::max()) <
              static_cast<std::uint64_t>(std::numeric_limits<CellVectorIndex::ValueType>::max()));

/// A patch's run inside a boundary field array: where it starts and how long it
/// is. CONTIGUOUS, because ADR-064 ordered the boundary range by patch.
///
/// An EMPTY PATCH is representable -- a CAD face whose mapping carries no facet --
/// and then `count` is 0 and `first` is invalid, rather than pointing at another
/// patch's first value.
///
/// SO ASK `isEmpty()` BEFORE DOING SPAN ARITHMETIC, and that warning lives here
/// because this is where a consumer writes it. `first.value()` of an empty range
/// is `kInvalid`, which is `SIZE_MAX`, so `values.subspan(range.first.value(),
/// range.count)` is UNDEFINED BEHAVIOUR rather than an empty subspan, while the
/// equivalent index loop happens to be harmless. The alternative was to give an
/// empty range the offset it would have had -- and for an INTERIOR empty patch
/// that is the next patch's first slot, which is another patch's value and
/// exactly what this type refuses to hand back. An adversarial review raised the
/// hazard; the choice stands and the warning is the answer to it.
struct CfdPatchFieldRange {
    BoundaryFaceIndex first{};
    std::size_t count = 0;

    [[nodiscard]] constexpr bool isEmpty() const noexcept { return count == 0; }

    friend constexpr bool operator==(const CfdPatchFieldRange&, const CfdPatchFieldRange&) = default;
};

// ---------------------------------------------------------------------------
// The refusals
// ---------------------------------------------------------------------------

/// Why a topology does not yield a field layout.
enum class CfdFieldMapProblem : std::uint8_t {
    /// The topology holds no cells.
    ///
    /// REFUSED HERE THOUGH ADR-065 ACCEPTS AN EMPTY TOPOLOGY, and the asymmetry is
    /// the reason rather than an oversight (ADR-066, Decision 6). The measurement
    /// of nothing is zero and misleads nobody; a NUMBERING of nothing is an absent
    /// system, not a degenerate one. P17-DOF-001 and P18-DOF-001 refuse it in the
    /// same words: `N == 0` would reach an assembler as a matrix with no rows and
    /// would solve trivially, reporting success for a model that was never there.
    ///
    /// Reachable: a mesh with no tetrahedra and no patches yields a BUILDABLE
    /// topology with zero cells, because ADR-064's inspection finds no defect in
    /// nothing. That is the witness ADR-065 used for its empty-geometry case.
    TopologyHasNoCells,
    /// The topology was derived from a different mesh generation than the model
    /// holds. The staged gate's refusal; the plain builder has no model to ask.
    TopologyIsOfAnotherMesh,
};

/// The two, in declaration order.
inline constexpr std::array<CfdFieldMapProblem, 2> kCfdFieldMapProblems{
    CfdFieldMapProblem::TopologyHasNoCells, CfdFieldMapProblem::TopologyIsOfAnotherMesh};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdFieldMapProblem problem) noexcept;

// ---------------------------------------------------------------------------
// What a change invalidates
// ---------------------------------------------------------------------------

/// How much of a field layout a topology still agrees with.
///
/// THREE VALUES AND NOT TWO, because the checklist's claim is about a PARTIAL
/// invalidation and "same or not" cannot express one (ADR-066, Decision 5):
///
/// ```text
/// Current                the stamps, the counts and the patch partition agree
/// BoundaryLayoutChanged  the mesh part agrees and the partition does not: cell
///                        fields and internal-face values keep their meaning,
///                        boundary fields do not
/// MeshChanged            the stamps differ, or cells / faces / internalFaces
///                        differ: every field of every location is invalid
/// ```
///
/// A FLUID-PROPERTY EDIT IS NOT ON THIS LIST, and that is the point of its absence
/// rather than a gap. Density and viscosity are not inputs to a layout: two maps
/// built from one mesh generation with two different fluids are EQUAL, so a
/// material edit leaves `Current` standing. A test asserts the equality rather
/// than this comment claiming it.
enum class CfdFieldCurrency : std::uint8_t { Current, BoundaryLayoutChanged, MeshChanged };

/// The three, in declaration order: increasing severity.
inline constexpr std::array<CfdFieldCurrency, 3> kCfdFieldCurrencies{
    CfdFieldCurrency::Current, CfdFieldCurrency::BoundaryLayoutChanged, CfdFieldCurrency::MeshChanged};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdFieldCurrency currency) noexcept;

// ---------------------------------------------------------------------------
// The layout
// ---------------------------------------------------------------------------

/// The field layout of one topology of one generation of one mesh.
///
/// A VALUE THAT BORROWS NOTHING, like `CfdTopology` and `CfdGeometry`: it copies
/// the counts and the patch partition it needs, so it cannot dangle behind a
/// topology that has been rebuilt. It is DERIVED STATE and is never persisted
/// (ADR-058), and it is keyed to the `MeshStamp` it came from.
///
/// O(patches) IN SIZE, AND INDEPENDENT OF THE MESH. It holds three counts, two
/// vectors over the patches, a stamp and a geometry revision. There is no per-cell
/// and no per-face array, because a layout is arithmetic and not a table.
///
/// WHAT CARRIES THAT CLAIM, named exactly, because an earlier version of this
/// paragraph cited "a test measuring the size against a mesh of four thousand
/// cells" and no such measurement exists -- the largest reference mesh is about a
/// thousand cells, and the test it pointed at compared `sizeof` of one type with
/// itself, which cannot differ. Two instruments carry it instead:
///
/// ```text
/// the member list   a mirror static_assert in the tests fixes sizeof against a
///                   struct naming the seven members that MAY appear, so an
///                   eighth breaks the build. It does NOT catch a member
///                   RESIZED: a per-cell vector swapped in for a per-patch one
///                   keeps sizeof identical
/// the two lengths   both vectors are observable through the API -- patchRange
///                   answers for `[0, patches)` and nothing above, and the
///                   ranges are checked to cover the boundary exactly -- so
///                   their lengths are patches + 1 and patches, measured, and
///                   the tube's patch count is asserted below its cell count
/// ```
///
/// THE ARRAY LENGTHS, which are the whole point:
///
/// ```text
/// size(CfdVariable::Pressure)   cells
/// size(CfdVariable::Velocity)   3 cells
/// size(CfdVariable::MassFlux)   faces
/// ```
class BETTERCAD_CFD_EXPORT CfdFieldMap {
public:
    // --- counts -------------------------------------------------------------

    [[nodiscard]] std::size_t cells() const noexcept { return cells_; }
    [[nodiscard]] std::size_t faces() const noexcept { return faces_; }
    [[nodiscard]] std::size_t internalFaces() const noexcept { return internalFaces_; }
    [[nodiscard]] std::size_t boundaryFaces() const noexcept { return faces_ - internalFaces_; }
    [[nodiscard]] std::size_t patches() const noexcept { return patchFace_.size(); }

    /// How many values an array at @p location holds. 0 for a location this build
    /// does not define, which is the same answer as "no slots", and never a
    /// throw or an assertion: an unrecognised enumerator is representable.
    [[nodiscard]] std::size_t size(CfdFieldLocation location) const noexcept;

    /// How many values an array of @p variable holds: its component count times
    /// the size of its location. 0 for a variable this build does not define.
    ///
    /// THE LENGTH A CONSUMER SIZES ITS ARRAY BY, and the reason it is asked per
    /// VARIABLE rather than per location: the component count comes from the
    /// frozen table, so a velocity array cannot be sized as though it were a
    /// pressure array by arithmetic a caller performs itself.
    [[nodiscard]] std::size_t size(CfdVariable variable) const noexcept;

    // --- cell-centred scalar ------------------------------------------------

    /// Whether @p cell is one of this layout's `[0, cells)`.
    [[nodiscard]] bool contains(CellIndex cell) const noexcept {
        return cell.isValid() && cell.value() < cells_;
    }

    // --- cell-centred vector ------------------------------------------------

    /// The subscript of @p component of @p cell: `component * cells + cell`.
    ///
    /// None if @p cell is not one of this layout's cells or @p component is not
    /// one of the three. An optional and not a sentinel, for the reason ADR-064
    /// made `neighbour` one: a sentinel is the kind of thing a caller forgets to
    /// check.
    [[nodiscard]] std::optional<CellVectorIndex> vectorIndex(CellIndex cell,
                                                             VectorComponent component) const noexcept;

    /// The cell @p index addresses, or none if it is outside `[0, 3 cells)`.
    [[nodiscard]] std::optional<CellIndex> cellOf(CellVectorIndex index) const noexcept;

    /// The component @p index addresses, or none if it is outside `[0, 3 cells)`.
    ///
    /// NOT A MODULO, WHICH IS WHAT BLOCKING MEANS. P17's interleaved numbering
    /// takes the component cyclically from a flat position; here the component is
    /// the QUOTIENT `index / cells` and the cell is the remainder. A reader who
    /// expects P17's arithmetic gets a different answer, which is why both are
    /// written out where they are declared.
    [[nodiscard]] std::optional<VectorComponent> componentOf(CellVectorIndex index) const noexcept;

    /// Whether @p index is one of this layout's `[0, 3 cells)`.
    [[nodiscard]] bool contains(CellVectorIndex index) const noexcept;

    // --- face field ---------------------------------------------------------

    /// Whether @p face is one of this layout's `[0, faces)`.
    [[nodiscard]] bool contains(FaceIndex face) const noexcept {
        return face.isValid() && face.value() < faces_;
    }

    /// Whether @p face's value is in the INTERNAL range, `[0, internalFaces)`.
    ///
    /// A comparison and not a stored flag, exactly as `CfdTopology::isInternal`
    /// is one, and the two give the same answer on every face of the topology a
    /// layout was built from -- which a test asserts on every reference mesh
    /// rather than this comment claiming.
    [[nodiscard]] bool isInternalSlot(FaceIndex face) const noexcept {
        return face.isValid() && face.value() < internalFaces_;
    }

    // --- boundary field -----------------------------------------------------

    /// Whether @p boundaryFace is one of this layout's `[0, boundaryFaces)`.
    [[nodiscard]] bool contains(BoundaryFaceIndex boundaryFace) const noexcept {
        return boundaryFace.isValid() && boundaryFace.value() < boundaryFaces();
    }

    /// The face field slot @p boundaryFace corresponds to:
    /// `internalFaces + boundaryFace`. None if it is not one of this layout's.
    ///
    /// THE SAME ARITHMETIC `CfdTopology::faceOf` PERFORMS, offered here so that a
    /// consumer holding a layout and a boundary array can find the face array's
    /// slot without the topology; a test asserts the two agree on every boundary
    /// face of every reference mesh, because two answers to one question are two
    /// things that can drift.
    [[nodiscard]] std::optional<FaceIndex> faceSlotOf(BoundaryFaceIndex boundaryFace) const noexcept;

    /// The boundary field slot @p face corresponds to, or none if @p face is
    /// internal or is not one of this layout's.
    [[nodiscard]] std::optional<BoundaryFaceIndex> boundarySlotOf(FaceIndex face) const noexcept;

    /// The patch @p boundaryFace belongs to, or none if it is not one of this
    /// layout's.
    ///
    /// A BINARY SEARCH OVER THE PARTITION OFFSETS, not a stored per-face array.
    /// `CfdTopology.hpp`'s own rule is the reason -- "a second copy is a second
    /// thing to keep true" -- and the offsets are already carried for
    /// `patchRange`.
    [[nodiscard]] std::optional<PatchIndex> patchOf(BoundaryFaceIndex boundaryFace) const noexcept;

    /// Where @p patch's values sit in a boundary field array, or none if @p patch
    /// is not one of this layout's. Contiguous, possibly empty.
    [[nodiscard]] std::optional<CfdPatchFieldRange> patchRange(PatchIndex patch) const noexcept;

    /// The CAD face @p patch is: the `MappedFace::index` the mapping gave it,
    /// which is NOT in general equal to the PatchIndex (ADR-064). Copied so that a
    /// consumer can attribute a patch's values without the topology, and compared
    /// by the binding below.
    [[nodiscard]] std::optional<std::size_t> patchFace(PatchIndex patch) const noexcept;

    // --- binding ------------------------------------------------------------

    /// The mesh generation this layout was derived from.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    /// The geometry that mesh was built from.
    [[nodiscard]] const meshing::GeometryRevision& geometry() const noexcept { return geometry_; }

    /// Whether this layout was derived from @p mesh. STAMPS, NEVER COUNTS: two
    /// generations can agree on every count and share no node.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept { return mesh.owns(mesh_); }

    /// How much of this layout @p topology still agrees with.
    ///
    /// THE ONE PLACE THE COMPARISON IS DEFINED, so the two questions below cannot
    /// disagree with it or with each other.
    [[nodiscard]] CfdFieldCurrency currency(const CfdTopology& topology) const noexcept;

    /// Whether a CELL field or an internal-face value indexed by this layout is
    /// still meaningful against @p topology: everything but `MeshChanged`.
    ///
    /// NOT A WHOLE FACE FIELD. A face array spans `[0, faces)`, and under
    /// `BoundaryLayoutChanged` its INTERNAL range keeps its meaning while its
    /// boundary tail -- `[internalFaces, faces)` -- does not, because that is the
    /// range the patch partition orders. A consumer holding a mass flux must
    /// therefore keep the head and recompute the tail, or ask
    /// `describesBoundary`. True here is not permission to keep the whole array.
    [[nodiscard]] bool describesCells(const CfdTopology& topology) const noexcept {
        return currency(topology) != CfdFieldCurrency::MeshChanged;
    }

    /// Whether a BOUNDARY field indexed by this layout is still meaningful against
    /// @p topology: `Current` alone.
    [[nodiscard]] bool describesBoundary(const CfdTopology& topology) const noexcept {
        return currency(topology) == CfdFieldCurrency::Current;
    }

    friend bool operator==(const CfdFieldMap&, const CfdFieldMap&) = default;

private:
    CfdFieldMap() = default;

    /// The two functions permitted to build one. The constructor is private for
    /// ADR-064's and ADR-065's reason: possession of a `CfdFieldMap` is proof that
    /// the layout came from a topology with at least one cell, so a caller cannot
    /// assemble a layout over counts no topology has.
    friend BETTERCAD_CFD_EXPORT Result<CfdFieldMap> buildCfdFieldMap(const CfdTopology& topology);
    friend BETTERCAD_CFD_EXPORT Result<CfdFieldMap> requireCfdFieldMap(const CfdModel& model,
                                                                       const CfdTopology& topology);

    meshing::MeshStamp mesh_{};
    meshing::GeometryRevision geometry_{};

    std::size_t cells_ = 0;
    std::size_t faces_ = 0;
    std::size_t internalFaces_ = 0;

    /// The patch partition of the boundary range: patch p owns the boundary field
    /// slots `[patchStart_[p], patchStart_[p + 1])`. `patches + 1` entries, the
    /// first 0 and the last `boundaryFaces`. ADR-064 ordered the boundary range by
    /// patch, which is what makes a partition enough.
    std::vector<std::size_t> patchStart_{};
    /// Per patch: the CAD face's position in the mapping's `faces()` order.
    std::vector<std::size_t> patchFace_{};
};

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

/// The layout of @p topology.
///
/// A PLAIN TOPOLOGY, for the reason ADR-063, ADR-064 and ADR-065 took plain
/// inputs: the staged gate below adds the model's revision and the generation
/// check, and a builder reachable only through it could not be shown a topology
/// of another mesh.
///
/// Refuses `TopologyHasNoCells` with FailedPrecondition, and nothing else: every
/// other property a layout needs is one possession of a `CfdTopology` already
/// proves. In particular the boundary range's grouping by patch is ADR-064's
/// invariant, not a thing re-derived here, and the partition is read off
/// `patchFaces` rather than recomputed from the faces.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdFieldMap> buildCfdFieldMap(const CfdTopology& topology);

/// The layout for a validated CFD input whose @p topology was derived from the
/// generation @p model now holds.
///
/// THE STAGED GATE (ADR-053's shape, as ADR-063, ADR-064 and ADR-065 used it). It
/// names the meshing control in a refusal, which the plain builder cannot, and
/// carries the model's geometry revision onto the result.
///
/// IT DOES NOT CONSULT THE FLUID, and that is deliberate: density and viscosity
/// are not inputs to a layout, so a material edit cannot change one. The model is
/// here for the mesh generation and the control name.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdFieldMap> requireCfdFieldMap(const CfdModel& model,
                                                                          const CfdTopology& topology);

/// The problem `buildCfdFieldMap` would report for @p topology, or none.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdFieldMapProblem>
cfdFieldMapProblem(const CfdTopology& topology) noexcept;

} // namespace bettercad::cfd
