// P19-DOF-001: the CFD field layout (ADR-066).
//
// THREE KINDS OF EVIDENCE, because the three claims are different in kind.
//
//   1. THE LAYOUT IS WHAT IT SAYS IT IS. The arithmetic is checked against a
//      formula written out AGAIN here -- `component * cells + cell` -- never
//      against the accessor that produced it, and the vector subscript is proved
//      a BIJECTION onto [0, 3 cells) by marking every slot rather than by
//      spot-checking a few. A round trip alone would pass for a layout that
//      mapped two cells to one slot.
//   2. THE LAYOUT AGREES WITH THE TOPOLOGY IT CAME FROM. Four questions have two
//      answers in the tree now -- is this face internal, which face is this
//      boundary slot, which boundary slot is this face, which patch holds it --
//      and two answers to one question are two things that can drift. Each is
//      checked against `CfdTopology`'s on EVERY face of EVERY reference mesh,
//      which is P19-TOPO-001's lesson about structural claims checked on one
//      model.
//   3. THE BINDING REPORTS THE RIGHT SCOPE. A remesh, a fluid edit, a boundary
//      condition edit and a patch re-attribution are four different events, and
//      the milestone's claim is that they invalidate different things. Each is
//      performed -- through the production path where there is one -- and the
//      verdict is read, including the ONE re-attribution ADR-066 records as
//      undetectable, which is measured here rather than left as prose.
//
// WHAT IS NOT CLAIMED. No field VALUES exist in this milestone, so nothing here
// solves, interpolates or balances anything. The dimensions of the three
// variables are checked against SI exponents written out by hand, which is an
// independent oracle for the table but not for the physics: that the mass flux of
// a face is `rho (U . Sf)` is ADR-058's and P19-CONT-001's to honour.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CellVectorIndex;
using cfd::CfdFieldCurrency;
using cfd::CfdFieldLocation;
using cfd::CfdFieldMap;
using cfd::CfdFieldMapProblem;
using cfd::CfdPatchFieldRange;
using cfd::CfdTopology;
using cfd::CfdVariable;
using cfd::FaceIndex;
using cfd::PatchIndex;
using cfd::VectorComponent;
using meshing::ElementId;
using meshing::NodeId;

namespace {

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// ADR-063's face table, written out again HERE rather than included from the
/// header, so a fixture that agreed with a corrupted table would not agree with
/// this one.
constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

/// Which faces of @p tets are used once, keyed by their sorted node triple, with
/// the winding of the first claimant. The same walk ADR-063 describes, written
/// here so a fixture does not have to trust the builder it is testing.
std::map<std::array<std::size_t, 3>, std::pair<std::size_t, std::array<std::size_t, 3>>>
faceUses(const std::vector<std::array<std::size_t, 4>>& tets) {
    std::map<std::array<std::size_t, 3>, std::pair<std::size_t, std::array<std::size_t, 3>>> uses;
    for (const auto& tet : tets) {
        for (const auto& face : kFaces) {
            const std::array<std::size_t, 3> wound{tet[face[0]], tet[face[1]], tet[face[2]]};
            std::array<std::size_t, 3> key = wound;
            std::ranges::sort(key);
            auto& [count, winding] = uses[key];
            if (count++ == 0) {
                winding = wound;
            }
        }
    }
    return uses;
}

/// Builds a mesh from @p points and @p tets and covers every face exactly one tet
/// uses with a boundary triangle wound outward of that tet. Every tetrahedron
/// first, so the cells' ElementIds are consecutive.
meshing::Mesh buildMesh(const std::vector<Point3D>& points, const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    for (const auto& tet : tets) {
        REQUIRE(builder.addTetrahedron({nodes[tet[0]], nodes[tet[1]], nodes[tet[2]], nodes[tet[3]]}, kRegion));
    }
    for (const auto& [key, use] : faceUses(tets)) {
        const auto& [count, w] = use;
        if (count == 1) {
            REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[1]], nodes[w[2]]}, kRegion));
        }
    }
    return builder.build();
}

/// The same mesh, with each tetrahedron's own boundary triangles emitted
/// IMMEDIATELY AFTER IT, so the two cells' ElementIds are 1 and 5 rather than 1
/// and 2.
///
/// A LEGAL MESH, NOT A SPOILED ONE: every boundary face is covered exactly once
/// and wound outward of its owner, as `buildMesh` does. Only the ORDER of the
/// `addTriangle` calls differs, and because P16's triangles and tetrahedra share
/// one handle space (ADR-064's reason for a compact index), that order is enough
/// to make the cell handles sparse.
meshing::Mesh sparseCellHandleMesh(const std::vector<Point3D>& points,
                                   const std::vector<std::array<std::size_t, 4>>& tets) {
    const auto uses = faceUses(tets);
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    for (const auto& tet : tets) {
        REQUIRE(builder.addTetrahedron({nodes[tet[0]], nodes[tet[1]], nodes[tet[2]], nodes[tet[3]]}, kRegion));
        for (const auto& face : kFaces) {
            const std::array<std::size_t, 3> wound{tet[face[0]], tet[face[1]], tet[face[2]]};
            std::array<std::size_t, 3> key = wound;
            std::ranges::sort(key);
            const auto found = uses.find(key);
            REQUIRE(found != uses.end());
            if (found->second.first == 1) {
                REQUIRE(builder.addTriangle({nodes[wound[0]], nodes[wound[1]], nodes[wound[2]]}, kRegion));
            }
        }
    }
    return builder.build();
}

/// Two cells sharing one face: A = (0,1,2,3), B = (1,2,3,4), both positive.
/// 7 faces: 1 interior and 6 boundary.
///
/// FIVE POINTS, NOT SIX. The sibling fixture in CfdTopologyTests.cpp carries a
/// sixth for its non-manifold case; carried here it would be an orphan node, which
/// an adversarial review noticed and read as the remains of a larger fixture.
std::vector<Point3D> twoCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{10_mm, 10_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

/// ONE cell: four boundary faces and no internal face, which is the smallest
/// layout there is and the only place in this file where `internalFaces == 0`.
std::vector<Point3D> oneCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kOneCell{{0, 1, 2, 3}};

/// One patch holding every boundary triangle of @p mesh.
std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

/// @p groups patches, the triangles dealt out round-robin, so patch membership is
/// exercised with more than one patch and with a known expected partition. A
/// group beyond the triangle count is EMPTY, which is how an empty patch is
/// reached.
std::vector<meshing::MappedFace> patchesOf(const meshing::Mesh& mesh, std::size_t groups) {
    std::vector<meshing::MappedFace> patches(groups);
    for (std::size_t p = 0; p < groups; ++p) {
        patches[p].index = 100 + p; // a CAD face position, deliberately not p
    }
    std::size_t next = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        patches[next++ % groups].facets.push_back(triangle.id);
    }
    return patches;
}

/// TWO patches over every boundary triangle, split at @p first: patch 0 takes the
/// first @p first triangles in mesh order and patch 1 the rest, with CAD faces 100
/// and 101 either way.
///
/// Two calls with different splits agree on the patch COUNT and on both CAD face
/// indices and differ only in the per-patch SIZES -- which is the one thing the
/// binding compares that no other fixture here exercises.
std::vector<meshing::MappedFace> splitPatches(const meshing::Mesh& mesh, std::size_t first) {
    std::vector<meshing::MappedFace> patches(2);
    patches[0].index = 100;
    patches[1].index = 101;
    std::size_t seen = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        patches[seen++ < first ? 0 : 1].facets.push_back(triangle.id);
    }
    return patches;
}

/// An EMPTY patch first, then one holding every boundary triangle, so the
/// partition offsets are {0, 0, boundaryFaces}. The LEADING repeat is the case
/// `patchOf`'s `upper_bound` is written for, and every empty patch reachable
/// through `patchesOf` is a trailing one.
std::vector<meshing::MappedFace> leadingEmptyPatch(const meshing::Mesh& mesh) {
    std::vector<meshing::MappedFace> patches(2);
    patches[0].index = 100; // no facets at all
    patches[1].index = 101;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        patches[1].facets.push_back(triangle.id);
    }
    return patches;
}

/// The same partition with the two patches' CAD FACE INDICES exchanged: identical
/// sizes, identical membership, a different attribution to the model's faces.
std::vector<meshing::MappedFace> cadFacesSwapped(std::vector<meshing::MappedFace> patches) {
    REQUIRE(patches.size() >= 2);
    std::swap(patches[0].index, patches[1].index);
    return patches;
}

/// The same patches with their MEMBERSHIP exchanged and their CAD indices left in
/// place: ADR-066, Decision 5's undetectable re-attribution when the two groups
/// are the same size.
std::vector<meshing::MappedFace> membershipSwapped(std::vector<meshing::MappedFace> patches) {
    REQUIRE(patches.size() >= 2);
    std::swap(patches[0].facets, patches[1].facets);
    return patches;
}

void assignWater(Document& document, MaterialId& id) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> created = features::createMaterial(document, "CfdWater", water);
    REQUIRE(created.has_value());
    REQUIRE(features::assignMaterial(document, *created).has_value());
    id = *created;
}

/// A P16 reference model, meshed, with water assigned and the CFD input prepared
/// all the way to its topology and its field layout.
struct PreparedFluid {
    /// `Result<T>` is not default-constructible for a T that is not, and every
    /// stage below is re-run by `prepare()`, so each starts as an explicit "not
    /// prepared" failure rather than as a value.
    static Error notPrepared() { return Error{ErrorCode::FailedPrecondition, "not prepared yet"}; }

    meshref::MeshedReference model;
    MaterialId material{};
    Result<cfd::CfdModel> input{std::unexpected(notPrepared())};
    Result<cfd::CfdMeshContract> contract{std::unexpected(notPrepared())};
    Result<CfdTopology> topology{std::unexpected(notPrepared())};
    Result<CfdFieldMap> layout{std::unexpected(notPrepared())};

    explicit PreparedFluid(MeshReferenceModelKind kind) : model(document(kind)) {
        assignWater(model.document(), material);
        (void)model.regenerate();
        (void)model.require();
        prepare();
    }

    static Document document(MeshReferenceModelKind kind) {
        Result<Document> built = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    /// Re-runs the whole chain against whatever the document and the mesher now
    /// hold, and requires every stage. The ordinary production path.
    void prepare() {
        input = cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        INFO((input.has_value() ? std::string{} : input.error().message));
        REQUIRE(input.has_value());
        contract = cfd::requireCfdMesh(*input);
        INFO((contract.has_value() ? std::string{} : contract.error().message));
        REQUIRE(contract.has_value());
        topology = cfd::requireCfdTopology(*input, *contract);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        layout = cfd::requireCfdFieldMap(*input, *topology);
        INFO((layout.has_value() ? std::string{} : layout.error().message));
        REQUIRE(layout.has_value());
    }

    [[nodiscard]] const meshing::Mesh& mesh() const { return input->mesh().mesh(); }

    /// Edits the fluid's density. A mesh is a function of geometry and meshing
    /// intent, and the density is in neither.
    void setDensity(Density density) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(model.document(), material)->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(density);
        REQUIRE(features::setMaterialMechanical(model.document(), material, mechanical).has_value());
    }

    /// Edits the fluid's dynamic viscosity: the other property a CfdModel
    /// resolves, and the other half of the header's claim.
    void setViscosity(DynamicViscosity viscosity) {
        materials::ThermalProperties thermal =
            features::findMaterial(model.document(), material)->definition().thermal;
        thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(viscosity);
        REQUIRE(features::setMaterialThermal(model.document(), material, thermal).has_value());
    }

    /// Puts a valid steady analysis in the document, so that a later edit is a
    /// change to CANONICAL STATE and the definition read back afterwards is the
    /// document's rather than a local copy. Both reviews found the local-copy
    /// version of these tests unable to fail.
    CfdAnalysisId addAnalysis() {
        cfd::CfdAnalysisDefinition definition;
        definition.mesh = model.control();
        definition.referencePressure = cfd::CfdReferencePressure{.value = 0_Pa, .location = Point3D{}};
        Result<std::unique_ptr<cfd::CfdAnalysis>> study = cfd::CfdAnalysis::create("Flow", std::move(definition));
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        const ObjectId added = require(model.document().addObject(std::move(*study)));
        (void)model.regenerate();
        return CfdAnalysisId::fromValue(added.value());
    }

    /// The definition the DOCUMENT holds, read afresh every time.
    [[nodiscard]] const cfd::CfdAnalysisDefinition& definitionOf(CfdAnalysisId analysis) {
        const cfd::CfdAnalysis* study = cfd::findCfdAnalysis(model.document(), analysis);
        REQUIRE(study != nullptr);
        return study->definition();
    }

    /// Adds a no-slip wall to the analysis IN THE DOCUMENT, through the tracked
    /// edit path, and requires that the edit was a real change.
    void addWall(CfdAnalysisId analysis) {
        const ObjectId object = ObjectId::fromValue(analysis.value());
        const Result<bool> changed = model.document().modifyObject<cfd::CfdAnalysis>(
            object, [this](cfd::CfdAnalysis& study) -> Result<bool> {
                cfd::CfdAnalysisDefinition definition = study.definition();
                definition.boundaryConditions.emplace_back(CfdBoundaryConditionId::fromValue(1),
                                                           cfd::NoSlipWall{FaceName{model.body(), FaceSelector{}}});
                return study.setDefinition(std::move(definition));
            });
        INFO((changed.has_value() ? std::string{} : changed.error().message));
        REQUIRE(changed.has_value());
        CHECK(*changed);
    }

    /// Changes the meshing intent and remeshes, which is a NEW GENERATION through
    /// the production path rather than a second hand-built mesh.
    void remeshWithTargetSize(Length target) {
        const ObjectId control = model.controlObject();
        REQUIRE(model.document()
                    .modifyObject<meshing::MeshControl>(
                        control,
                        [target](meshing::MeshControl& object) -> Result<bool> {
                            meshing::MeshControlDefinition definition = object.definition();
                            definition.mesh.sizing.globalTargetSize = target;
                            return object.setDefinition(definition).has_value();
                        })
                    .has_value());
        CHECK(model.currency() != meshing::MeshCurrency::Current);
        (void)model.regenerate();
        (void)model.require();
    }
};

/// Every structural claim about a layout, run on every input rather than one
/// model per claim.
///
/// EVERY NUMBER IS CHECKED AGAINST THE TOPOLOGY'S OWN ANSWER or against the
/// blocked formula written out again -- never read back from the map accessor that
/// produced it. That is a cross-check between two implementations, not an
/// independent oracle: the oracle counted off the P16 mesh and mapping is
/// `CfdFields_TheArrayLengthsFollowFromTheMeshAloneByAnIndependentCount`, and an
/// adversarial review was right that an earlier version of this comment claimed
/// the stronger thing. Three assertions that merely restated the implementation
/// (`boundaryFaces == faces - internalFaces`, `isEmpty == (count == 0)`, and
/// `size(variable) == components * size(location)`) were removed rather than
/// reworded: they could not fail.
void checkLayout(const CfdFieldMap& map, const CfdTopology& topology, const meshing::Mesh& mesh) {
    // --- the counts are the topology's ------------------------------------
    CHECK(map.cells() == topology.cells());
    CHECK(map.faces() == topology.faces());
    CHECK(map.internalFaces() == topology.internalFaces());
    CHECK(map.boundaryFaces() == topology.boundaryFaces());
    CHECK(map.patches() == topology.patches());

    // --- the array lengths -------------------------------------------------
    CHECK(map.size(CfdFieldLocation::Cell) == topology.cells());
    CHECK(map.size(CfdFieldLocation::Face) == topology.faces());
    CHECK(map.size(CfdFieldLocation::BoundaryFace) == topology.boundaryFaces());
    CHECK(map.size(CfdVariable::Pressure) == topology.cells());
    CHECK(map.size(CfdVariable::Velocity) == 3 * topology.cells());
    CHECK(map.size(CfdVariable::MassFlux) == topology.faces());

    // --- the cell-centred vector is a bijection onto [0, 3 cells) ---------
    const std::size_t components = cfd::kVectorComponents.size();
    std::vector<unsigned> hits(components * map.cells(), 0U);
    for (std::size_t c = 0; c < map.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        CHECK(map.contains(cell));
        for (std::size_t k = 0; k < components; ++k) {
            const VectorComponent component = cfd::kVectorComponents[k];
            const std::optional<CellVectorIndex> index = map.vectorIndex(cell, component);
            REQUIRE(index.has_value());
            // The formula, written out again rather than read back.
            CHECK(index->value() == k * map.cells() + c);
            REQUIRE(index->value() < hits.size());
            ++hits[index->value()];
            CHECK(map.contains(*index));
            CHECK(map.cellOf(*index) == cell);
            CHECK(map.componentOf(*index) == component);
        }
        // ADR-066, Decision 3's coincidence, verified rather than claimed: the X
        // block IS the scalar layout.
        CHECK(map.vectorIndex(cell, VectorComponent::X)->value() == cell.value());
    }
    CHECK(std::ranges::all_of(hits, [](unsigned n) { return n == 1U; }));
    CHECK_FALSE(map.contains(CellIndex::fromValue(map.cells())));
    CHECK_FALSE(map.contains(CellVectorIndex::fromValue(components * map.cells())));

    // EACH COMPONENT'S BLOCK IS ONE CONTIGUOUS RUN, which is the property the
    // whole choice of layout rests on (ADR-066, Decision 3) and which was checked
    // only on a two-cell fixture before a test-gap review pointed that out.
    for (std::size_t k = 0; k < components; ++k) {
        const CellVectorIndex first = *map.vectorIndex(CellIndex::fromValue(0), cfd::kVectorComponents[k]);
        const CellVectorIndex last =
            *map.vectorIndex(CellIndex::fromValue(map.cells() - 1), cfd::kVectorComponents[k]);
        CHECK(first.value() == k * map.cells());
        CHECK(last.value() - first.value() == map.cells() - 1);
    }

    // --- the face field, and the internal range first ---------------------
    for (std::size_t f = 0; f < map.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        CHECK(map.contains(face));
        // The same question the topology answers; two answers must not drift.
        CHECK(map.isInternalSlot(face) == topology.isInternal(face));
        const std::optional<BoundaryFaceIndex> boundary = map.boundarySlotOf(face);
        CHECK(boundary.has_value() == topology.isBoundary(face));
        CHECK(boundary == topology.boundaryFaceOf(face));
        if (boundary) {
            CHECK(map.faceSlotOf(*boundary) == face);
        }
    }
    CHECK_FALSE(map.contains(FaceIndex::fromValue(map.faces())));

    // --- the boundary field, and the patch partition ----------------------
    std::size_t covered = 0;
    std::vector<unsigned> boundaryHits(map.boundaryFaces(), 0U);
    for (std::size_t p = 0; p < map.patches(); ++p) {
        const PatchIndex patch = PatchIndex::fromValue(p);
        const std::optional<CfdPatchFieldRange> range = map.patchRange(patch);
        REQUIRE(range.has_value());
        CHECK(range->count == topology.patchFaces(patch).size());
        CHECK(range->isEmpty() == topology.patchFaces(patch).empty());
        CHECK(map.patchFace(patch) == topology.patchFace(patch));
        covered += range->count;
        for (std::size_t k = 0; k < range->count; ++k) {
            const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(range->first.value() + k);
            REQUIRE(slot.value() < boundaryHits.size());
            ++boundaryHits[slot.value()];
            CHECK(map.patchOf(slot) == patch);
        }
    }
    // The ranges cover the boundary exactly once: ADR-064's totality, checked as
    // a consequence rather than assumed.
    CHECK(covered == map.boundaryFaces());
    CHECK(std::ranges::all_of(boundaryHits, [](unsigned n) { return n == 1U; }));
    for (std::size_t b = 0; b < map.boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        CHECK(map.contains(slot));
        CHECK(map.patchOf(slot) == topology.patchOf(slot));
        CHECK(map.faceSlotOf(slot) == topology.faceOf(slot));
        CHECK(map.faceSlotOf(slot)->value() == map.internalFaces() + b);
    }
    CHECK_FALSE(map.contains(BoundaryFaceIndex::fromValue(map.boundaryFaces())));

    // --- the binding ------------------------------------------------------
    CHECK(map.describes(mesh));
    CHECK(map.mesh() == topology.mesh());
    CHECK(map.currency(topology) == CfdFieldCurrency::Current);
    CHECK(map.describesCells(topology));
    CHECK(map.describesBoundary(topology));
}

} // namespace

// ---------------------------------------------------------------------------
// The index types
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_TheIndexTypesAreDistinctAndNothingConvertsToAMeshHandle", "[cfd][fields]") {
    // ADR-066, Decision 4: ONE new subscript, and ADR-064's three reused. The
    // conversions that would make the layout indexable by a mesh handle do not
    // exist in either direction, and neither does the one between a cell and its
    // own X component -- which matters more here than elsewhere, because those two
    // subscripts COINCIDE numerically (Decision 3).
    static_assert(!std::is_convertible_v<CellVectorIndex, CellIndex>);
    static_assert(!std::is_convertible_v<CellIndex, CellVectorIndex>);
    static_assert(!std::is_convertible_v<CellVectorIndex, FaceIndex>);
    static_assert(!std::is_convertible_v<CellVectorIndex, BoundaryFaceIndex>);
    static_assert(!std::is_convertible_v<CellVectorIndex, std::size_t>);
    static_assert(!std::is_convertible_v<std::size_t, CellVectorIndex>);
    static_assert(!std::is_convertible_v<CellVectorIndex, ElementId>);
    static_assert(!std::is_convertible_v<ElementId, CellVectorIndex>);
    static_assert(!std::is_convertible_v<CellIndex, ElementId>);
    static_assert(!std::is_convertible_v<ElementId, CellIndex>);
    static_assert(!std::is_convertible_v<NodeId, CellIndex>);

    // Zero is a VALID subscript and a default-constructed one is not component X
    // of cell 0 -- ADR-064's inversion, kept.
    CHECK(CellVectorIndex::fromValue(0).isValid());
    CHECK_FALSE(CellVectorIndex{}.isValid());
    CHECK(CellVectorIndex{} != CellVectorIndex::fromValue(0));
    CHECK(CellVectorIndex::fromValue(0) < CellVectorIndex::fromValue(1));
    CHECK(static_cast<bool>(CellVectorIndex::fromValue(7)));
    CHECK_FALSE(static_cast<bool>(CellVectorIndex{}));

    // AND THE INVALID SUBSCRIPT SORTS LAST, which is the surprising consequence of
    // `kInvalid` being -1 cast to an unsigned type: a consumer sorting subscripts
    // finds it after every real one, not before. Pinned because it follows from
    // the representation rather than from a choice, and because the defaulted
    // three-way comparison is public API that nothing else exercised.
    CHECK(CellVectorIndex::fromValue(0) < CellVectorIndex{});
    CHECK((CellVectorIndex{} <=> CellVectorIndex::fromValue(0)) == std::strong_ordering::greater);
    CHECK((CellVectorIndex::fromValue(4) <=> CellVectorIndex::fromValue(4)) == std::strong_ordering::equal);

    // Possession of a layout requires the builder: there is no public constructor,
    // so a caller cannot assemble a layout over counts no topology has.
    static_assert(!std::is_default_constructible_v<CfdFieldMap>);
}

// ---------------------------------------------------------------------------
// The variable table
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_TheVariableTableFixesTheThreeLocationsAndTheirSiDimensions", "[cfd][fields]") {
    // THE CHECKLIST'S FIRST THREE ITEMS, and the oracle is INDEPENDENT of the
    // catalog: the SI exponents are written out by hand here, so a table that
    // agreed with a corrupted `dimensions::pressure` would not agree with this.
    // The header's own static_asserts tie the table to the catalog; this ties the
    // catalog's answer to the physics.
    struct Expected {
        CfdVariable variable;
        CfdFieldLocation location;
        std::size_t components;
        Dimension dimension;
        std::string_view symbol;
    };
    const std::array<Expected, 3> expected{
        {{CfdVariable::Pressure, CfdFieldLocation::Cell, 1, Dimension{.length = -1, .mass = 1, .time = -2}, "p"},
         {CfdVariable::Velocity, CfdFieldLocation::Cell, 3, Dimension{.length = 1, .time = -1}, "U"},
         {CfdVariable::MassFlux, CfdFieldLocation::Face, 1, Dimension{.mass = 1, .time = -1}, "phi"}}};

    REQUIRE(cfd::kCfdFields.size() == expected.size());
    for (const Expected& row : expected) {
        INFO(cfd::toString(row.variable));
        const std::optional<cfd::CfdFieldDescriptor> descriptor = cfd::descriptorOf(row.variable);
        REQUIRE(descriptor.has_value());
        CHECK(descriptor->location == row.location);
        CHECK(descriptor->components == row.components);
        CHECK(descriptor->dimension == row.dimension);
        CHECK(descriptor->symbol == row.symbol);
    }

    // The mass flux is a mass FLOW RATE, kg/s, and not kg/(m^2 s): the
    // face-integrated quantity a control-volume balance sums (ADR-066, Decision
    // 2). Stated as the difference from the per-area form, which an area divides.
    const Dimension perArea = Dimension{.mass = 1, .time = -1} / Dimension{.length = 2};
    CHECK_FALSE(cfd::descriptorOf(CfdVariable::MassFlux)->dimension == perArea);

    // The table is in enumerator order, and an unrecognised value names nothing.
    for (std::size_t i = 0; i < cfd::kCfdVariables.size(); ++i) {
        CHECK(cfd::kCfdFields[i].variable == cfd::kCfdVariables[i]);
        CHECK(cfd::isRecognised(cfd::kCfdVariables[i]));
    }
    CHECK_FALSE(cfd::isRecognised(static_cast<CfdVariable>(7)));
    CHECK_FALSE(cfd::descriptorOf(static_cast<CfdVariable>(7)).has_value());
    CHECK_FALSE(cfd::isRecognised(static_cast<CfdFieldLocation>(9)));
    for (const CfdFieldLocation location : cfd::kCfdFieldLocations) {
        CHECK(cfd::isRecognised(location));
    }

    // The rows compare as values -- public API, and how a consumer will check it
    // holds the row it thinks it does.
    CHECK(*cfd::descriptorOf(CfdVariable::Pressure) == cfd::kCfdFields[0]);
    CHECK(*cfd::descriptorOf(CfdVariable::MassFlux) == cfd::kCfdFields[2]);
    CHECK_FALSE(*cfd::descriptorOf(CfdVariable::Pressure) == cfd::kCfdFields[1]);
}

// ---------------------------------------------------------------------------
// Every reference mesh yields a layout
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_EveryP16ReferenceMeshYieldsALayout", "[cfd][fields]") {
    // THE MILESTONE'S CENTRAL CLAIM, on every body P16 qualified, with every
    // structural claim run on every one of them. RM-MESH-08, the open profile, has
    // no body and so no mesh.
    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid fluid(info.kind);
        ++meshed;
        checkLayout(*fluid.layout, *fluid.topology, fluid.mesh());
        // A real body has cells and faces, so the lengths are not vacuous.
        CHECK(fluid.layout->cells() > 0);
        CHECK(fluid.layout->faces() > fluid.layout->cells());
        CHECK(fluid.layout->boundaryFaces() > 0);
        CHECK(fluid.layout->patches() > 0);
    }
    CHECK(meshed == 8);
}

// ---------------------------------------------------------------------------
// The blocked arithmetic
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_TheComponentIsTheQuotientWhichIsWhereP17sInterleavingDiffers", "[cfd][fields]") {
    // ADR-066, Decision 3 reverses P17-DOF-001's convention, so the two must be
    // shown to DISAGREE on a concrete subscript rather than described as
    // different. TWO CELLS IS NOT ENOUGH FOR THAT, which both of this milestone's
    // reviews caught this comment claiming otherwise: at two cells, subscript 3 is
    // cell 1 under BOTH conventions and only the component differs, so an
    // assertion about the cell is satisfied either way. The two-cell layout below
    // is kept because it enumerates all six slots explicitly; the DISCRIMINATION
    // is then made on the block's six cells, where subscript 4 is cell 4's X
    // component blocked and cell 1's Y component interleaved -- both halves
    // differ.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE(topology.has_value());
    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*topology);
    REQUIRE(map.has_value());
    REQUIRE(map->cells() == 2);

    // cells = 2: the blocked layout is [x0 x1 | y0 y1 | z0 z1].
    CHECK(map->vectorIndex(CellIndex::fromValue(0), VectorComponent::X)->value() == 0);
    CHECK(map->vectorIndex(CellIndex::fromValue(1), VectorComponent::X)->value() == 1);
    CHECK(map->vectorIndex(CellIndex::fromValue(0), VectorComponent::Y)->value() == 2);
    CHECK(map->vectorIndex(CellIndex::fromValue(1), VectorComponent::Y)->value() == 3);
    CHECK(map->vectorIndex(CellIndex::fromValue(0), VectorComponent::Z)->value() == 4);
    CHECK(map->vectorIndex(CellIndex::fromValue(1), VectorComponent::Z)->value() == 5);

    // Subscript 3 is cell 1's Y under BLOCKING, and cell 1's X under interleaving:
    // the component is the QUOTIENT here and the remainder there. The CELL agrees,
    // which is why this fixture alone does not settle it.
    const CellVectorIndex three = CellVectorIndex::fromValue(3);
    CHECK(map->componentOf(three) == VectorComponent::Y);
    CHECK(map->cellOf(three) == CellIndex::fromValue(1));
    CHECK(cfd::kVectorComponents[three.value() % cfd::kVectorComponents.size()] == VectorComponent::X);
    CHECK(three.value() / cfd::kVectorComponents.size() == 1);

    // THE DISCRIMINATING FIXTURE: six cells, where subscript 4 is cell 4's X
    // component under blocking and cell 1's Y component under interleaving, so the
    // cell AND the component differ. The block is six tetrahedra, measured.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdFieldMap& six = *fluid.layout;
    REQUIRE(six.cells() == 6);
    const CellVectorIndex four = CellVectorIndex::fromValue(4);
    CHECK(six.cellOf(four) == CellIndex::fromValue(4));
    CHECK(six.componentOf(four) == VectorComponent::X);
    const std::size_t interleavedComponent = four.value() % cfd::kVectorComponents.size();
    const std::size_t interleavedCell = four.value() / cfd::kVectorComponents.size();
    CHECK(cfd::kVectorComponents[interleavedComponent] == VectorComponent::Y);
    CHECK(interleavedCell == 1);
    CHECK_FALSE(six.cellOf(four) == CellIndex::fromValue(interleavedCell));
    CHECK_FALSE(six.componentOf(four) == cfd::kVectorComponents[interleavedComponent]);
}

TEST_CASE("CfdFields_EveryVariablesArrayLengthIsItsComponentsTimesItsLocation", "[cfd][fields]") {
    // The length a consumer sizes its array by, asked per VARIABLE so that the
    // component count comes from the frozen table and not from the caller.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdFieldMap& map = *fluid.layout;

    // NOT `size(variable) == components * size(location)`, which is verbatim the
    // body of the accessor and cannot fail. The three lengths are written out
    // instead, each against the location it is a multiple of.
    CHECK(map.size(CfdVariable::Pressure) == map.size(CfdFieldLocation::Cell));
    CHECK(map.size(CfdVariable::Velocity) == 3 * map.size(CfdFieldLocation::Cell));
    CHECK(map.size(CfdVariable::MassFlux) == map.size(CfdFieldLocation::Face));

    // An unrecognised enumerator names no slots, in either overload, and neither
    // throws nor asserts: zero is an answer a caller can act on.
    CHECK(map.size(static_cast<CfdVariable>(7)) == 0);
    CHECK(map.size(static_cast<CfdFieldLocation>(9)) == 0);
}

// ---------------------------------------------------------------------------
// No raw mesh-ID indexing
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_ASparseCellHandleSpaceIsLaidOutByCountAndNotByHandleValue", "[cfd][fields]") {
    // THE DEFECT THE COMPACT INDEX EXISTS TO PREVENT. P16's triangles and
    // tetrahedra share one handle space, so emitting each cell's boundary
    // triangles right after it leaves the CELL handles sparse -- 1 and 5 here. A
    // layout keyed by `ElementId::value()` would be eight slots long with two
    // used; this one is two.
    const meshing::Mesh mesh = sparseCellHandleMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    REQUIRE(topology->cells() == 2);

    // The premise of the test, proved rather than assumed: the handles really are
    // sparse, and the largest exceeds the count.
    const ElementId firstCell = topology->cellOf(CellIndex::fromValue(0));
    const ElementId secondCell = topology->cellOf(CellIndex::fromValue(1));
    CHECK(secondCell.value() > firstCell.value() + 1);
    CHECK(secondCell.value() > topology->cells());

    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*topology);
    REQUIRE(map.has_value());
    CHECK(map->cells() == 2);
    CHECK(map->size(CfdVariable::Pressure) == 2);
    CHECK(map->size(CfdVariable::Velocity) == 6);
    CHECK(map->size(CfdVariable::Pressure) < secondCell.value());

    // The compact index of the LAST cell is 1, not the handle's value, and the
    // subscripts of its three components are inside the array the layout sizes.
    for (const VectorComponent component : cfd::kVectorComponents) {
        const std::optional<CellVectorIndex> index = map->vectorIndex(CellIndex::fromValue(1), component);
        REQUIRE(index.has_value());
        CHECK(index->value() < map->size(CfdVariable::Velocity));
    }
    // And the handle itself cannot be used as a subscript: there is no overload
    // that takes one, which the compile-fail group proves, and the index the
    // topology translates it to is the compact one.
    CHECK(topology->cellIndexOf(secondCell) == CellIndex::fromValue(1));

    checkLayout(*map, *topology, mesh);
}

TEST_CASE("CfdFields_TheLayoutHoldsNoPerCellOrPerFaceArray", "[cfd][fields]") {
    // P17's MIRROR DEVICE, applied to the layout: the members that MAY appear, and
    // a size equality that an added per-cell array breaks. This is what makes
    // "O(patches), independent of the mesh" a checked property rather than a
    // sentence in the header.
    struct PermittedLayout {
        meshing::MeshStamp mesh;
        meshing::GeometryRevision geometry;
        std::size_t cells;
        std::size_t faces;
        std::size_t internalFaces;
        std::vector<std::size_t> patchStart;
        std::vector<std::size_t> patchFace;
    };
    static_assert(sizeof(CfdFieldMap) == sizeof(PermittedLayout),
                  "the layout holds a stamp, a revision, three counts and the patch partition -- nothing else. A "
                  "per-cell or per-face array, a copy of the topology, a mesh handle or a field value added to it "
                  "breaks this");

    // And the same size whatever the mesh: a type whose footprint grew with the
    // cell count could not satisfy the assertion above, and this states the
    // consequence on two meshes of very different size.
    PreparedFluid small(MeshReferenceModelKind::Block);
    PreparedFluid large(MeshReferenceModelKind::Tube);
    // NOT `sizeof(*small.layout) == sizeof(*large.layout)`, which was here and is
    // a tautology: both operands are sizeof(CfdFieldMap), equal by language rule
    // for any implementation whatever. Both reviews caught it. What the mirror
    // above proves is that no EIGHTH member exists; what bounds the two vectors
    // below the cell count is the patch count, asserted here.
    CHECK(large.layout->cells() > 100 * small.layout->cells());
    // The only heap the layout owns is the partition, which is the PATCH count,
    // and on the tube that is far below the cell count. On the BLOCK the two are
    // equal -- six CAD faces, six tetrahedra -- which is why the claim is made
    // where the counts separate and why the mirror above is what carries it.
    CHECK(large.layout->patches() < large.layout->cells());
    CHECK(small.layout->patches() == small.layout->cells());
}

// ---------------------------------------------------------------------------
// Totality and the refusals
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_EveryAccessorIsTotalForAnIndexItDoesNotHold", "[cfd][fields]") {
    // No accessor has a precondition a caller can violate: each answers for an
    // index outside the layout, for the invalid index, and for a sentinel value.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patchesOf(mesh, 2));
    REQUIRE(topology.has_value());
    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*topology);
    REQUIRE(map.has_value());

    CHECK_FALSE(map->contains(CellIndex{}));
    CHECK_FALSE(map->contains(FaceIndex{}));
    CHECK_FALSE(map->contains(BoundaryFaceIndex{}));
    CHECK_FALSE(map->contains(CellVectorIndex{}));
    CHECK_FALSE(map->isInternalSlot(FaceIndex{}));

    CHECK_FALSE(map->vectorIndex(CellIndex{}, VectorComponent::X).has_value());
    CHECK_FALSE(map->vectorIndex(CellIndex::fromValue(map->cells()), VectorComponent::X).has_value());
    CHECK_FALSE(map->vectorIndex(CellIndex::fromValue(0), static_cast<VectorComponent>(5)).has_value());
    CHECK_FALSE(map->cellOf(CellVectorIndex{}).has_value());
    CHECK_FALSE(map->cellOf(CellVectorIndex::fromValue(3 * map->cells())).has_value());
    CHECK_FALSE(map->componentOf(CellVectorIndex{}).has_value());
    CHECK_FALSE(map->componentOf(CellVectorIndex::fromValue(3 * map->cells())).has_value());

    CHECK_FALSE(map->faceSlotOf(BoundaryFaceIndex{}).has_value());
    CHECK_FALSE(map->faceSlotOf(BoundaryFaceIndex::fromValue(map->boundaryFaces())).has_value());
    CHECK_FALSE(map->boundarySlotOf(FaceIndex{}).has_value());
    CHECK_FALSE(map->boundarySlotOf(FaceIndex::fromValue(map->faces())).has_value());
    // An INTERNAL face has no boundary slot, which is not the same as being out of
    // range: both answer none, and the first is the ordinary case.
    CHECK_FALSE(map->boundarySlotOf(FaceIndex::fromValue(0)).has_value());
    CHECK(map->isInternalSlot(FaceIndex::fromValue(0)));

    CHECK_FALSE(map->patchOf(BoundaryFaceIndex{}).has_value());
    CHECK_FALSE(map->patchOf(BoundaryFaceIndex::fromValue(map->boundaryFaces())).has_value());
    CHECK_FALSE(map->patchRange(PatchIndex{}).has_value());
    CHECK_FALSE(map->patchRange(PatchIndex::fromValue(map->patches())).has_value());
    CHECK_FALSE(map->patchFace(PatchIndex{}).has_value());
    CHECK_FALSE(map->patchFace(PatchIndex::fromValue(map->patches())).has_value());
}

TEST_CASE("CfdFields_AnEmptyTopologyIsRefusedBecauseANumberingOfNothingIsAbsent", "[cfd][fields]") {
    // ADR-066, Decision 6: ADR-065 measures an empty topology and gets zeroes,
    // because measuring nothing is zero; a NUMBERING of nothing is an absent
    // system that would reach an assembler as a matrix with no rows and solve
    // trivially. P17-DOF-001's and P18-DOF-001's refusal, in their words.
    //
    // REACHABLE, and this is the witness: ADR-064's inspection finds no defect in
    // nothing, so an empty mesh with no patches yields a BUILDABLE topology.
    const meshing::Mesh empty = meshing::MeshBuilder{}.build();
    const Result<CfdTopology> topology = cfd::buildCfdTopology(empty, {});
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    CHECK(topology->cells() == 0);
    CHECK(topology->faces() == 0);

    CHECK(cfd::cfdFieldMapProblem(*topology) == CfdFieldMapProblem::TopologyHasNoCells);
    const Result<CfdFieldMap> refused = cfd::buildCfdFieldMap(*topology);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("no cells"));
    CHECK_THAT(refused.error().message, ContainsSubstring("nothing to number"));

    // A topology that DOES have a cell reports no problem, so the probe
    // discriminates.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> real = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE(real.has_value());
    CHECK_FALSE(cfd::cfdFieldMapProblem(*real).has_value());
    CHECK(cfd::buildCfdFieldMap(*real).has_value());
}

TEST_CASE("CfdFields_AnEmptyPatchHasNoFirstSlotAndACountOfZero", "[cfd][fields]") {
    // A CAD face whose mapping carries no facet is representable, and its range
    // must not point at another patch's first value.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::size_t triangles = mesh.triangles().size();
    REQUIRE(triangles == 6);
    // Two groups beyond the triangle count: patches 6 and 7 get nothing.
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patchesOf(mesh, triangles + 2));
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    REQUIRE(topology->patches() == triangles + 2);

    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*topology);
    REQUIRE(map.has_value());
    for (std::size_t p = 0; p < triangles; ++p) {
        const std::optional<CfdPatchFieldRange> range = map->patchRange(PatchIndex::fromValue(p));
        REQUIRE(range.has_value());
        CHECK(range->count == 1);
        CHECK_FALSE(range->isEmpty());
        CHECK(range->first.isValid());
    }
    for (std::size_t p = triangles; p < triangles + 2; ++p) {
        INFO("empty patch " << p);
        const std::optional<CfdPatchFieldRange> range = map->patchRange(PatchIndex::fromValue(p));
        REQUIRE(range.has_value());
        CHECK(range->count == 0);
        CHECK(range->isEmpty());
        // NOT slot 0, which is a real slot of another patch.
        CHECK_FALSE(range->first.isValid());
        // The CAD face is still named, because an empty patch is still a patch.
        CHECK(map->patchFace(PatchIndex::fromValue(p)) == topology->patchFace(PatchIndex::fromValue(p)));
    }
    // And no boundary slot is attributed to an empty patch.
    for (std::size_t b = 0; b < map->boundaryFaces(); ++b) {
        const std::optional<PatchIndex> patch = map->patchOf(BoundaryFaceIndex::fromValue(b));
        REQUIRE(patch.has_value());
        CHECK(patch->value() < triangles);
    }
    checkLayout(*map, *topology, mesh);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_TwoBuildsOfOneTopologyAreEqual", "[cfd][fields]") {
    // Determinism means nothing unless equality discriminates, so both halves are
    // here: two builds agree field for field, and a layout of another mesh does
    // not compare equal to either.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patchesOf(mesh, 3));
    REQUIRE(topology.has_value());

    const Result<CfdFieldMap> first = cfd::buildCfdFieldMap(*topology);
    const Result<CfdFieldMap> second = cfd::buildCfdFieldMap(*topology);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);

    // Two topologies of ONE mesh generation are equal field for field (ADR-064 is
    // deterministic), so their layouts are one layout.
    const Result<CfdTopology> again = cfd::buildCfdTopology(mesh, patchesOf(mesh, 3));
    REQUIRE(again.has_value());
    CHECK(*again == *topology);
    const Result<CfdFieldMap> third = cfd::buildCfdFieldMap(*again);
    REQUIRE(third.has_value());
    CHECK(*third == *first);
}

TEST_CASE("CfdFields_EqualityDiscriminatesSoDeterminismMeansSomething", "[cfd][fields]") {
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const meshing::Mesh other = buildMesh(twoCellPoints(), kTwoCells);
    // Identical in every count and every coordinate, and a different generation.
    CHECK_FALSE(mesh.stamp() == other.stamp());

    const Result<CfdTopology> one = cfd::buildCfdTopology(mesh, patchesOf(mesh, 2));
    const Result<CfdTopology> two = cfd::buildCfdTopology(other, patchesOf(other, 2));
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    const Result<CfdFieldMap> first = cfd::buildCfdFieldMap(*one);
    const Result<CfdFieldMap> second = cfd::buildCfdFieldMap(*two);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    // Every count agrees; the stamp does not, so the layouts are not equal.
    CHECK(first->cells() == second->cells());
    CHECK(first->faces() == second->faces());
    CHECK(first->patches() == second->patches());
    CHECK_FALSE(*first == *second);

    // And a different patch partition on one mesh is a different layout.
    const Result<CfdTopology> split = cfd::buildCfdTopology(mesh, patchesOf(mesh, 3));
    REQUIRE(split.has_value());
    const Result<CfdFieldMap> third = cfd::buildCfdFieldMap(*split);
    REQUIRE(third.has_value());
    CHECK_FALSE(*third == *first);
}

// ---------------------------------------------------------------------------
// The binding: the mesh source
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_DescribesIsFalseForAnotherMeshGeneration", "[cfd][fields]") {
    // STAMPS, NEVER COUNTS. Two meshes identical in every count and coordinate are
    // still two generations, and a layout of one describes neither the other mesh
    // nor its topology.
    const meshing::Mesh first = buildMesh(twoCellPoints(), kTwoCells);
    const meshing::Mesh second = buildMesh(twoCellPoints(), kTwoCells);
    CHECK(first.nodeCount() == second.nodeCount());
    CHECK(first.elementCount() == second.elementCount());

    const Result<CfdTopology> one = cfd::buildCfdTopology(first, onePatch(first));
    const Result<CfdTopology> two = cfd::buildCfdTopology(second, onePatch(second));
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*one);
    REQUIRE(map.has_value());

    CHECK(map->describes(first));
    CHECK_FALSE(map->describes(second));
    CHECK(map->currency(*one) == CfdFieldCurrency::Current);
    CHECK(map->currency(*two) == CfdFieldCurrency::MeshChanged);
    CHECK_FALSE(map->describesCells(*two));
    CHECK_FALSE(map->describesBoundary(*two));
}

TEST_CASE("CfdFields_ARemeshInvalidatesEveryLocationOfTheLayout", "[cfd][fields]") {
    // THROUGH THE PRODUCTION PATH, not a second hand-built mesh: the meshing
    // intent is edited, the mesher is asked again, and the new generation is what
    // the old layout is held against.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdFieldMap before = *fluid.layout;
    const CfdTopology topologyBefore = *fluid.topology;
    const meshing::MeshStamp stampBefore = fluid.mesh().stamp();
    const std::size_t cellsBefore = before.cells();

    fluid.remeshWithTargetSize(4_mm);
    fluid.prepare();
    CHECK_FALSE(fluid.mesh().stamp() == stampBefore);

    // Every location is invalid, not just the boundary: the mesh changed.
    CHECK(before.currency(*fluid.topology) == CfdFieldCurrency::MeshChanged);
    CHECK_FALSE(before.describesCells(*fluid.topology));
    CHECK_FALSE(before.describesBoundary(*fluid.topology));
    CHECK_FALSE(before.describes(fluid.mesh()));

    // The refusal is reached through the staged gate as well, which is what a
    // consumer holding a stale topology actually hits.
    const Result<CfdFieldMap> refused = cfd::requireCfdFieldMap(*fluid.input, topologyBefore);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);

    // THE STRONGEST FORM OF "STAMPS, NEVER COUNTS", and this body is what
    // measures it. The block is 120 x 70 x 35 mm with eight corner nodes, so its
    // surface triangulation admits only six tetrahedra and P16's global target
    // does not refine it: measured at 20, 12 and 4 mm, with and without a finer
    // linear deflection, every one gives 6 tets and 8 nodes. So the remesh above
    // produced a mesh IDENTICAL IN EVERY COUNT and a new generation, and every
    // verdict here rests on the stamp alone.
    checkLayout(*fluid.layout, *fluid.topology, fluid.mesh());
    CHECK(fluid.layout->cells() == cellsBefore);
    CHECK(fluid.layout->faces() == before.faces());
}

TEST_CASE("CfdFields_TheStagedGateNamesTheControlAndRefusesAnotherGeneration", "[cfd][fields]") {
    // The staged gate's one refusal, with the diagnostic a reader of the failure
    // needs: which control, which generation it was derived from, which the model
    // now holds.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const meshing::Mesh hand = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> alien = cfd::buildCfdTopology(hand, onePatch(hand));
    REQUIRE(alien.has_value());

    const Result<CfdFieldMap> refused = cfd::requireCfdFieldMap(*fluid.input, *alien);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("meshing control"));
    CHECK_THAT(refused.error().message, ContainsSubstring("another mesh generation"));
    CHECK_THAT(refused.error().message, ContainsSubstring(std::to_string(fluid.model.control().value())));
}

TEST_CASE("CfdFields_TheStagedGateCarriesTheModelsGeometryRevisionAndIgnoresTheFluid", "[cfd][fields]") {
    // The gate's two jobs: the model's revision rather than the topology's copy of
    // it, and nothing from the fluid.
    //
    // THIS FIXTURE CANNOT SEE THE FIRST OF THEM, and says so rather than implying
    // otherwise: its topology came from `requireCfdTopology`, which already
    // stamped the model's revision onto it, so the gate's assignment is a no-op
    // here and deleting it would pass this case. Both reviews found that, and the
    // fixture that does see it -- a PLAIN topology, whose revision is invalid --
    // is `CfdFields_TheStagedGateReplacesThePlainTopologysGeometryRevision`.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    CHECK(fluid.layout->geometry() == fluid.input->geometryRevision());
    CHECK(fluid.layout->geometry().isValid());

    const Result<CfdFieldMap> plain = cfd::buildCfdFieldMap(*fluid.topology);
    REQUIRE(plain.has_value());
    CHECK(plain->geometry() == fluid.topology->geometry());
    CHECK(plain->cells() == fluid.layout->cells());
    CHECK(plain->faces() == fluid.layout->faces());
    CHECK(plain->currency(*fluid.topology) == CfdFieldCurrency::Current);
}

// ---------------------------------------------------------------------------
// The binding: what an edit invalidates
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_AFluidEditLeavesTheLayoutCurrentAndEqual", "[cfd][fields]") {
    // A LAYOUT IS NOT A FUNCTION OF THE FLUID. Density and viscosity are inputs to
    // the equations, not to the index space, so an edit to them leaves the layout
    // not merely current but EQUAL -- which is stronger, and is what makes the
    // absence of a fluid revision from the binding safe.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdFieldMap before = *fluid.layout;
    const meshing::MeshStamp stamp = fluid.mesh().stamp();

    fluid.setDensity(1000_kg_per_m3);
    CHECK(fluid.model.currency() == meshing::MeshCurrency::Current);
    fluid.prepare();

    // The premise: the fluid really did change.
    CHECK(fluid.input->density() == 1000_kg_per_m3);
    CHECK(fluid.mesh().stamp() == stamp);

    CHECK(before.currency(*fluid.topology) == CfdFieldCurrency::Current);
    CHECK(before.describesCells(*fluid.topology));
    CHECK(before.describesBoundary(*fluid.topology));
    CHECK(before.describes(fluid.mesh()));
    CHECK(before == *fluid.layout);

    // AND THE VISCOSITY, which the header claims alongside the density and which
    // a test-gap review found untested. It is the OTHER fluid property a CfdModel
    // resolves (P19-FLUID-001), and it reaches the momentum equation rather than
    // the index space.
    fluid.setViscosity(2.0_mPa_s);
    CHECK(fluid.model.currency() == meshing::MeshCurrency::Current);
    fluid.prepare();
    CHECK(fluid.input->fluid().viscosity() == 2.0_mPa_s);
    CHECK(fluid.mesh().stamp() == stamp);
    CHECK(before.currency(*fluid.topology) == CfdFieldCurrency::Current);
    CHECK(before == *fluid.layout);
}

TEST_CASE("CfdFields_ABoundaryConditionEditChangesNoLayout", "[cfd][fields]") {
    // A BOUNDARY CONDITION IS NOT A BOUNDARY LAYOUT. A condition names a CAD face
    // (ADR-032) and is canonical intent; the boundary field layout is derived from
    // the mesh's patch partition. So editing, adding or removing conditions cannot
    // move a boundary slot, and the layout is equal before and after.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    fluid.prepare();
    const CfdFieldMap before = *fluid.layout;
    const meshing::MeshStamp stamp = fluid.mesh().stamp();
    const cfd::CfdDefinitionFingerprint empty = cfd::fingerprint(fluid.definitionOf(analysis));
    REQUIRE(fluid.definitionOf(analysis).boundaryConditions.empty());

    // THE EDIT IS MADE IN THE DOCUMENT, through the tracked path, and the
    // fingerprint is read back from the document both times. An earlier version of
    // this test edited a local definition the document never held, which both
    // reviews found could not fail.
    fluid.addWall(analysis);
    const cfd::CfdDefinitionFingerprint withWall = cfd::fingerprint(fluid.definitionOf(analysis));
    REQUIRE(fluid.definitionOf(analysis).boundaryConditions.size() == 1);
    CHECK_FALSE(empty == withWall);

    // And it reaches no part of the layout, which is rebuilt from the same mesh
    // generation: the mesh is a function of geometry and meshing intent, and a
    // boundary condition is in neither.
    fluid.prepare();
    CHECK(fluid.mesh().stamp() == stamp);
    CHECK(before == *fluid.layout);
    CHECK(before.currency(*fluid.topology) == CfdFieldCurrency::Current);
    CHECK(before.describesCells(*fluid.topology));
    CHECK(before.describesBoundary(*fluid.topology));
}

TEST_CASE("CfdFields_APatchReattributionInvalidatesTheBoundaryLayoutAndNotTheCells", "[cfd][fields]") {
    // THE CHECKLIST'S "BOUNDARY EDITS INVALIDATE BOUNDARY-DERIVED STATE ONLY",
    // measured. Two topologies of ONE mesh generation whose patch attribution
    // differs agree on the stamp and on every cell, and differ in the boundary
    // range -- so the verdict must be partial, which is why it has three values
    // (ADR-066, Decision 5).
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> twoGroups = patchesOf(mesh, 2);

    const Result<CfdTopology> a = cfd::buildCfdTopology(mesh, twoGroups);
    const Result<CfdTopology> sizes = cfd::buildCfdTopology(mesh, patchesOf(mesh, 3));
    const Result<CfdTopology> cadFaces = cfd::buildCfdTopology(mesh, cadFacesSwapped(twoGroups));
    REQUIRE(a.has_value());
    REQUIRE(sizes.has_value());
    REQUIRE(cadFaces.has_value());

    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*a);
    REQUIRE(map.has_value());

    // THE CELLS AND THE INTERNAL FACES REALLY ARE UNCHANGED, which is what makes
    // "keep their meaning" a measurement and not a hope. The patch attribution
    // reaches neither.
    for (const Result<CfdTopology>* other : {&sizes, &cadFaces}) {
        const CfdTopology& t = **other;
        REQUIRE(t.cells() == a->cells());
        REQUIRE(t.internalFaces() == a->internalFaces());
        for (std::size_t c = 0; c < a->cells(); ++c) {
            CHECK(t.cellOf(CellIndex::fromValue(c)) == a->cellOf(CellIndex::fromValue(c)));
        }
        for (std::size_t f = 0; f < a->internalFaces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            CHECK(t.owner(face) == a->owner(face));
            CHECK(t.neighbour(face) == a->neighbour(face));
            CHECK(t.faceNodes(face) == a->faceNodes(face));
        }
    }

    // A partition with different SIZES is detected.
    CHECK(map->currency(*sizes) == CfdFieldCurrency::BoundaryLayoutChanged);
    CHECK(map->describesCells(*sizes));
    CHECK_FALSE(map->describesBoundary(*sizes));

    // So is one with the same sizes and the CAD faces attributed differently: a
    // boundary value would be charged to the wrong model face.
    CHECK(map->currency(*cadFaces) == CfdFieldCurrency::BoundaryLayoutChanged);
    CHECK(map->describesCells(*cadFaces));
    CHECK_FALSE(map->describesBoundary(*cadFaces));
    CHECK(cadFaces->patchFace(PatchIndex::fromValue(0)) != a->patchFace(PatchIndex::fromValue(0)));

    // And the layout of the same attribution is current.
    CHECK(map->currency(*a) == CfdFieldCurrency::Current);
}

TEST_CASE("CfdFields_TheOneReattributionTheBindingCannotSeeIsTheOneAdr066Records", "[cfd][fields]") {
    // ADR-066, Decision 5's KNOWN LIMITATION, measured rather than left as prose,
    // so that a later reader finds the witness instead of discovering it.
    //
    // Exchanging the MEMBERSHIP of two patches of equal size, with their CAD face
    // indices left in place, changes which boundary face sits in which slot and
    // changes neither the per-patch count nor the attribution the binding
    // compares. The verdict is therefore Current, and it is wrong about the
    // boundary.
    //
    // NOT REACHABLE FROM A CfdModel: `requireCfdTopology` reads one mapping from
    // one model, so both topologies here need hand-built MappedFace spans, which
    // exist so that the refusals can be shown inputs the gate refuses.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> twoGroups = patchesOf(mesh, 2);
    const Result<CfdTopology> a = cfd::buildCfdTopology(mesh, twoGroups);
    const Result<CfdTopology> swapped = cfd::buildCfdTopology(mesh, membershipSwapped(twoGroups));
    REQUIRE(a.has_value());
    REQUIRE(swapped.has_value());

    // The premise: equal sizes, equal CAD attribution.
    REQUIRE(a->patches() == swapped->patches());
    for (std::size_t p = 0; p < a->patches(); ++p) {
        const PatchIndex patch = PatchIndex::fromValue(p);
        REQUIRE(a->patchFaces(patch).size() == swapped->patchFaces(patch).size());
        REQUIRE(a->patchFace(patch) == swapped->patchFace(patch));
    }

    // And the boundary order DID move: at least one slot holds another facet.
    std::size_t moved = 0;
    for (std::size_t b = 0; b < a->boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        if (!(a->triangleOf(slot) == swapped->triangleOf(slot))) {
            ++moved;
        }
    }
    CHECK(moved > 0);

    // The binding cannot see it. This is the limitation, stated as a measurement.
    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*a);
    REQUIRE(map.has_value());
    CHECK(map->currency(*swapped) == CfdFieldCurrency::Current);
    CHECK(map->describesBoundary(*swapped));
}

// ---------------------------------------------------------------------------
// No field arrays in canonical state
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_NoCanonicalStateHoldsAFieldOrALayout", "[cfd][fields]") {
    // THE CHECKLIST'S LAST STRUCTURAL ITEM (ADR-066, Decision 7). Three things,
    // none of them prose; the first was already proved and is CITED rather than
    // re-counted.
    //
    //   1. the cfdids compile-fail cases of P19-DATA-001 prove the definition has
    //      no pressure field, no velocity field, no mass flux and no cells, and
    //      `CfdData_TheDefinitionHoldsOnlyIntent` pins its twelve members
    //   2. the cfdfield compile-fail cases prove the definition has no field map,
    //      and that a layout cannot be conjured without its builder
    //   3. a remesh that changes the cell count leaves the canonical definition,
    //      its canonical text and its fingerprint IDENTICAL -- which is what "no
    //      field arrays in canonical state" means operationally, and is checked
    //      here
    // A CURVED body, because a planar one cannot be refined: the block is six
    // tetrahedra at every target (measured), while the cylinder's reference
    // intent is 12 mm and 8 mm gives it several times as many cells. The claim
    // needs the mesh to change SIZE, not merely generation.
    PreparedFluid fluid(MeshReferenceModelKind::Cylinder);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    fluid.prepare();

    // READ BACK FROM THE DOCUMENT, BEFORE AND AFTER. An earlier version of this
    // test held a local definition the document never saw, so `canonicalText` was
    // being asked twice about the same unmodified struct -- a check of its
    // determinism, not of the absence of mesh-sized state, and one that a
    // `std::vector<double> velocities` added to the definition would have survived
    // because it would have been empty both times. Both reviews found it.
    const std::string textBefore = cfd::canonicalText(fluid.definitionOf(analysis));
    const cfd::CfdDefinitionFingerprint printBefore = cfd::fingerprint(fluid.definitionOf(analysis));
    const CfdFieldMap before = *fluid.layout;
    const std::size_t cellsBefore = before.cells();
    const meshing::MeshStamp stampBefore = fluid.mesh().stamp();

    fluid.remeshWithTargetSize(8_mm);
    fluid.prepare();
    REQUIRE(fluid.layout->cells() != cellsBefore);
    CHECK_FALSE(fluid.mesh().stamp() == stampBefore);

    // The one place in the suite where a COUNT CHANGE meets a verdict. It is still
    // the stamp that decides -- a test-gap review established that no public API
    // can produce a same-stamp, different-count pair -- but the mesh here really
    // did resize.
    CHECK(before.currency(*fluid.topology) == CfdFieldCurrency::MeshChanged);

    // The mesh changed size; canonical state did not notice, because nothing in it
    // is mesh-sized. A field array inside the definition would have had to change,
    // and so would the canonical text digested into the fingerprint.
    CHECK(cfd::canonicalText(fluid.definitionOf(analysis)) == textBefore);
    CHECK(cfd::fingerprint(fluid.definitionOf(analysis)) == printBefore);
    CHECK_FALSE(textBefore.empty());

    // And the layout itself is not canonical state: it cannot be default
    // constructed, so possession requires a topology, and it is never persisted
    // (ADR-058). The compile-fail group proves the definition has no member for it.
    static_assert(!std::is_default_constructible_v<CfdFieldMap>);
    static_assert(!std::is_assignable_v<cfd::CfdAnalysisDefinition&, CfdFieldMap>);
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_ToStringNamesEveryEnumeratorAndFallsBackForNone", "[cfd][fields]") {
    // Four enumerations, every enumerator named distinctly, and an unrecognised
    // value answered rather than falling off the end of a switch.
    std::set<std::string_view> seen;
    for (const CfdFieldLocation location : cfd::kCfdFieldLocations) {
        CHECK(seen.insert(cfd::toString(location)).second);
        CHECK_FALSE(cfd::toString(location).empty());
    }
    seen.clear();
    for (const CfdVariable variable : cfd::kCfdVariables) {
        CHECK(seen.insert(cfd::toString(variable)).second);
    }
    seen.clear();
    for (const CfdFieldMapProblem problem : cfd::kCfdFieldMapProblems) {
        CHECK(seen.insert(cfd::toString(problem)).second);
    }
    seen.clear();
    for (const CfdFieldCurrency currency : cfd::kCfdFieldCurrencies) {
        CHECK(seen.insert(cfd::toString(currency)).second);
    }

    CHECK(cfd::toString(static_cast<CfdFieldLocation>(9)) == "unknown field location");
    CHECK(cfd::toString(static_cast<CfdVariable>(9)) == "unknown variable");
    CHECK(cfd::toString(static_cast<CfdFieldMapProblem>(9)) == "unknown field layout problem");
    CHECK(cfd::toString(static_cast<CfdFieldCurrency>(9)) == "unknown field currency");

    // The three currencies are declared in increasing severity, which is what lets
    // the two questions be defined as comparisons against one of them.
    CHECK(cfd::kCfdFieldCurrencies.front() == CfdFieldCurrency::Current);
    CHECK(cfd::kCfdFieldCurrencies.back() == CfdFieldCurrency::MeshChanged);
}


// ---------------------------------------------------------------------------
// Independent validation
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_TheArrayLengthsFollowFromTheMeshAloneByAnIndependentCount", "[cfd][fields]") {
    // THE INDEPENDENT ORACLE, and it is independent in the sense CLAUDE.md means:
    // every number here is counted off the P16 MESH and the P16 MAPPING, never
    // read back from the topology the layout was built from. A layout that agreed
    // with a corrupted topology would not agree with these.
    //
    //   cells        the mesh's tetrahedra, counted
    //   boundary     the mesh's triangles, counted. ADR-063 covers every
    //                one-cell face with exactly one triangle, so the two are
    //                the same number
    //   faces        (4 cells + boundary) / 2, from 4 cells == 2 internal +
    //                boundary -- Euler bookkeeping over the mesh, not a reading
    //   per patch    the facets of that MappedFace, counted off the mapping
    //
    // The last is the one that matters most: the partition is what the boundary
    // field's layout IS, and the mapping is its only upstream source.
    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid fluid(info.kind);
        const CfdFieldMap& map = *fluid.layout;
        ++meshed;

        const std::size_t cells = fluid.mesh().tetrahedra().size();
        const std::size_t boundary = fluid.mesh().triangles().size();
        REQUIRE(cells > 0);
        REQUIRE(boundary > 0);
        // 4 faces per tetrahedron, each internal face shared by two cells.
        REQUIRE((4 * cells + boundary) % 2 == 0);
        const std::size_t faces = (4 * cells + boundary) / 2;

        CHECK(map.size(CfdVariable::Pressure) == cells);
        CHECK(map.size(CfdVariable::Velocity) == 3 * cells);
        CHECK(map.size(CfdVariable::MassFlux) == faces);
        CHECK(map.size(CfdFieldLocation::Cell) == cells);
        CHECK(map.size(CfdFieldLocation::Face) == faces);
        CHECK(map.size(CfdFieldLocation::BoundaryFace) == boundary);
        CHECK(map.internalFaces() == faces - boundary);

        // The patch partition against the mapping's own facet lists.
        const std::span<const meshing::MappedFace> mapped = fluid.model.map().faces();
        REQUIRE(map.patches() == mapped.size());
        std::size_t facets = 0;
        for (std::size_t p = 0; p < mapped.size(); ++p) {
            INFO("patch " << p << " of CAD face " << mapped[p].index);
            const std::optional<CfdPatchFieldRange> range = map.patchRange(PatchIndex::fromValue(p));
            REQUIRE(range.has_value());
            CHECK(range->count == mapped[p].facets.size());
            CHECK(map.patchFace(PatchIndex::fromValue(p)) == mapped[p].index);
            facets += mapped[p].facets.size();
        }
        // Every boundary triangle is in exactly one patch, so the facets of the
        // mapping and the slots of the boundary field are the same count.
        CHECK(facets == boundary);
        CHECK(facets == map.size(CfdFieldLocation::BoundaryFace));
    }
    CHECK(meshed == 8);
}

// ---------------------------------------------------------------------------
// The cases this milestone's two reviews asked for
// ---------------------------------------------------------------------------

TEST_CASE("CfdFields_TheStagedGateReplacesThePlainTopologysGeometryRevision", "[cfd][fields]") {
    // THE FIXTURE THAT CAN SEE THE GATE'S ONE DISTINGUISHING LINE. Both reviews
    // found that `map->geometry_ = model.geometryRevision()` was a no-op on every
    // path the suite took: `requireCfdTopology` already stamps the model's
    // revision onto the topology, so the gate was assigning a value that was
    // already there, and deleting the line passed everything.
    //
    // A PLAIN topology carries no revision -- only the staged gate has a CfdModel
    // to ask -- so over the very same topology the plain builder's layout must
    // carry the INVALID revision and the gate's must carry the model's.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const Result<CfdTopology> plain = cfd::buildCfdTopology(fluid.mesh(), fluid.model.map().faces());
    INFO((plain.has_value() ? std::string{} : plain.error().message));
    REQUIRE(plain.has_value());

    // The premise, proved rather than assumed: same mesh generation, no revision.
    CHECK(plain->describes(fluid.mesh()));
    CHECK_FALSE(plain->geometry().isValid());
    CHECK(fluid.topology->geometry().isValid());

    const Result<CfdFieldMap> plainLayout = cfd::buildCfdFieldMap(*plain);
    const Result<CfdFieldMap> gated = cfd::requireCfdFieldMap(*fluid.input, *plain);
    REQUIRE(plainLayout.has_value());
    INFO((gated.has_value() ? std::string{} : gated.error().message));
    REQUIRE(gated.has_value());

    CHECK_FALSE(plainLayout->geometry().isValid());
    CHECK(gated->geometry() == fluid.input->geometryRevision());
    CHECK(gated->geometry().isValid());

    // EVERY OTHER FIELD AGREES, so the revision is the only difference -- and
    // equality discriminates on it, which nothing else in the suite shows.
    CHECK(gated->cells() == plainLayout->cells());
    CHECK(gated->faces() == plainLayout->faces());
    CHECK(gated->internalFaces() == plainLayout->internalFaces());
    CHECK(gated->patches() == plainLayout->patches());
    CHECK(gated->mesh() == plainLayout->mesh());
    CHECK_FALSE(*gated == *plainLayout);

    // AND THE REVISION IS NOT PART OF THE BINDING, which is why `currency` does
    // not compare it: both layouts describe this topology though only one carries
    // its revision. A comparison added to `currency` would fail here.
    CHECK(gated->currency(*plain) == CfdFieldCurrency::Current);
    CHECK(plainLayout->currency(*plain) == CfdFieldCurrency::Current);
    checkLayout(*gated, *plain, fluid.mesh());
    checkLayout(*plainLayout, *plain, fluid.mesh());
}

TEST_CASE("CfdFields_APatchSplitThatKeepsTheCountAndTheCadFacesIsStillDetected", "[cfd][fields]") {
    // THE DETECTOR BRANCH NOTHING ELSE REACHED. `currency` compares the patch
    // COUNT, then each patch's SIZE, then each patch's CAD face. The first and the
    // third decided every verdict in the suite; a test-gap review found the middle
    // one -- the per-patch size, and the most realistic re-attribution of the
    // three -- never deciding anything.
    //
    // Two patches either way, CAD faces 100 and 101 either way, six boundary faces
    // either way: only the split moves.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    REQUIRE(mesh.triangles().size() == 6);
    const Result<CfdTopology> split24 = cfd::buildCfdTopology(mesh, splitPatches(mesh, 2));
    const Result<CfdTopology> split33 = cfd::buildCfdTopology(mesh, splitPatches(mesh, 3));
    INFO((split24.has_value() ? std::string{} : split24.error().message));
    REQUIRE(split24.has_value());
    REQUIRE(split33.has_value());

    // The premise: everything the binding compares agrees except the sizes.
    REQUIRE(split24->patches() == split33->patches());
    REQUIRE(split24->boundaryFaces() == split33->boundaryFaces());
    for (std::size_t p = 0; p < split24->patches(); ++p) {
        REQUIRE(split24->patchFace(PatchIndex::fromValue(p)) == split33->patchFace(PatchIndex::fromValue(p)));
    }
    CHECK(split24->patchFaces(PatchIndex::fromValue(0)).size() == 2);
    CHECK(split33->patchFaces(PatchIndex::fromValue(0)).size() == 3);

    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*split24);
    REQUIRE(map.has_value());
    CHECK(map->currency(*split24) == CfdFieldCurrency::Current);
    CHECK(map->currency(*split33) == CfdFieldCurrency::BoundaryLayoutChanged);
    CHECK(map->describesCells(*split33));
    CHECK_FALSE(map->describesBoundary(*split33));

    // And the cells really are untouched by the split, which is what makes a
    // partial verdict the right one.
    REQUIRE(split24->cells() == split33->cells());
    for (std::size_t c = 0; c < split24->cells(); ++c) {
        CHECK(split24->cellOf(CellIndex::fromValue(c)) == split33->cellOf(CellIndex::fromValue(c)));
    }
    checkLayout(*map, *split24, mesh);
}

TEST_CASE("CfdFields_ALeadingEmptyPatchStillAttributesEverySlotToItsOwnPatch", "[cfd][fields]") {
    // WHY `patchOf` IS AN upper_bound AND NOT A lower_bound. With an empty patch
    // the partition offsets REPEAT, and the attribution must take the LAST patch
    // whose start is at or below the slot. Every empty patch in the suite was
    // TRAILING, where the repeat sits at `boundaryFaces` and `contains` has
    // already refused the slot -- so the choice never changed an answer and the
    // justification was unverified, which an adversarial review pointed out.
    //
    // Here the empty patch is FIRST: the offsets are {0, 0, 6}, and a lower_bound
    // would attribute every slot to patch 0, which holds nothing.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, leadingEmptyPatch(mesh));
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    REQUIRE(topology->patches() == 2);
    REQUIRE(topology->patchFaces(PatchIndex::fromValue(0)).empty());
    REQUIRE(topology->patchFaces(PatchIndex::fromValue(1)).size() == 6);

    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*topology);
    REQUIRE(map.has_value());

    const std::optional<CfdPatchFieldRange> first = map->patchRange(PatchIndex::fromValue(0));
    const std::optional<CfdPatchFieldRange> second = map->patchRange(PatchIndex::fromValue(1));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->isEmpty());
    CHECK_FALSE(first->first.isValid());
    CHECK(second->count == 6);
    CHECK(second->first == BoundaryFaceIndex::fromValue(0));

    // Two ranges compared as values, which nothing else in the suite does.
    CHECK_FALSE(*first == *second);
    CHECK(*first == CfdPatchFieldRange{});
    CHECK(*second == *map->patchRange(PatchIndex::fromValue(1)));

    // EVERY slot belongs to patch 1, including slot 0 where the offsets repeat.
    for (std::size_t b = 0; b < map->boundaryFaces(); ++b) {
        INFO("slot " << b);
        CHECK(map->patchOf(BoundaryFaceIndex::fromValue(b)) == PatchIndex::fromValue(1));
    }
    checkLayout(*map, *topology, mesh);
}

TEST_CASE("CfdFields_ASingleCellTopologyHasNoInternalFaceAndStillLaysOutThreeBlocks", "[cfd][fields]") {
    // THE SMALLEST LAYOUT THERE IS: one cell, four boundary faces, NO internal
    // face. Nothing else in the suite has `internalFaces == 0`, so this is where
    // the face field is entirely boundary, where `% 1` and `/ 1` are the vector
    // translation's whole arithmetic, and where each component block is one slot.
    const meshing::Mesh mesh = buildMesh(oneCellPoints(), kOneCell);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());

    const Result<CfdFieldMap> map = cfd::buildCfdFieldMap(*topology);
    REQUIRE(map.has_value());
    CHECK(map->cells() == 1);
    CHECK(map->internalFaces() == 0);
    CHECK(map->faces() == 4);
    CHECK(map->boundaryFaces() == 4);
    CHECK(map->size(CfdVariable::Pressure) == 1);
    CHECK(map->size(CfdVariable::Velocity) == 3);
    CHECK(map->size(CfdVariable::MassFlux) == 4);

    // No face is internal, and every face is a boundary slot.
    for (std::size_t f = 0; f < map->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        CHECK_FALSE(map->isInternalSlot(face));
        CHECK(map->boundarySlotOf(face) == BoundaryFaceIndex::fromValue(f));
    }

    // Three blocks of one slot each: the subscript IS the component's ordinal.
    const CellIndex cell = CellIndex::fromValue(0);
    for (std::size_t k = 0; k < cfd::kVectorComponents.size(); ++k) {
        const std::optional<CellVectorIndex> index = map->vectorIndex(cell, cfd::kVectorComponents[k]);
        REQUIRE(index.has_value());
        CHECK(index->value() == k);
        CHECK(map->cellOf(*index) == cell);
        CHECK(map->componentOf(*index) == cfd::kVectorComponents[k]);
    }
    CHECK_FALSE(map->contains(CellVectorIndex::fromValue(3)));
    checkLayout(*map, *topology, mesh);
}
