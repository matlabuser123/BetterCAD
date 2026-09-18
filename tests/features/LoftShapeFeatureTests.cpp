#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/LoftModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::describe;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::ShapeLoftModel;
using bettercad::test::SmoothLoftModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-LOFT-001 at the feature level: lofts between sections of different
// shapes, and smooth interpolation. Every expected volume is a closed form
// computed here, not read back from the loft.

namespace {

constexpr double pi = std::numbers::pi;
// Ruled faces between a line and an arc are B-spline surfaces, like those
// between turned circles: P11-FEAT-009 measured those within 6.3e-10.
constexpr double kRelSpline = bettercad::test::kRelApproximatedIntersection;

const geometry::Body& requireBody(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    return *body;
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

// Body::boundingBox() is exact for planes, cylinders and cones, but the sides
// of a loft between different shapes are B-spline surfaces, which the kernel
// bounds numerically (BRepBndLib::AddOptimal) and pads outwards by its
// confusion tolerance, 1e-7 mm. Measured here: -10.0000001 for an exact -10,
// and 30.0000001 for an exact 30. Such a bound must contain the exact one and
// exceed it by at most that -- it must never cut inside it.
constexpr double kSplineBoundsPaddingMm = 1e-7;

void checkPaddedBound(double actualMm, double exactMm, bool isLowerBound) {
    CAPTURE(actualMm, exactMm);
    if (isLowerBound) {
        CHECK(actualMm <= exactMm + kPositionToleranceMm);
        CHECK(actualMm >= exactMm - kSplineBoundsPaddingMm - kPositionToleranceMm);
    } else {
        CHECK(actualMm >= exactMm - kPositionToleranceMm);
        CHECK(actualMm <= exactMm + kSplineBoundsPaddingMm + kPositionToleranceMm);
    }
}

Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO(describe(report));
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

} // namespace

// ---------------------------------------------------------------------------
// Differing section shapes
// ---------------------------------------------------------------------------

TEST_CASE("LoftShapes_ASquareLoftsToACircle", "[loft][features][p12][acceptance]") {
    ShapeLoftModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());

    const double expected = ShapeLoftModel::expectedVolume(10.0, 5.0, 30.0);
    INFO("expected " << expected << " mm^3, actual " << volumeMm3(regenerator, m.taper) << " mm^3");
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(expected, kRelSpline));
    const geometry::Body& body = requireBody(regenerator, m.taper);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    // A 20 mm square at the bottom and a 10 mm circle at the top.
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    checkPaddedBound(box->min.x.in(units::mm), -10.0, /*isLowerBound=*/true);
    checkPaddedBound(box->max.z.in(units::mm), 30.0, /*isLowerBound=*/false);
}

TEST_CASE("LoftShapes_FollowTheirParameters", "[loft][features][p12][acceptance]") {
    ShapeLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(ShapeLoftModel::expectedVolume(10, 5, 30), kRelSpline));

    SECTION("a bigger circle") {
        REQUIRE(m.doc.setParameterValue(m.radius, 8_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(ShapeLoftModel::expectedVolume(10, 8, 30), kRelSpline));
    }
    SECTION("a taller loft") {
        REQUIRE(m.doc.setParameterValue(m.height, 45_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(ShapeLoftModel::expectedVolume(10, 5, 45), kRelSpline));
    }
}

TEST_CASE("LoftShapes_CapsStayNamedAcrossShapes", "[loft][features][references][p12]") {
    // A loft names its end caps and not its sides (P11-FEAT-009). Matching
    // sections of different shapes does not change that: the caps are still
    // found, and the sides are still refused as unnamed.
    ShapeLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.taper);

    for (const FaceRole role : {FaceRole::StartCap, FaceRole::EndCap}) {
        const FaceName name{m.taper, {.role = role}};
        const auto found = geometry::findNamedFaces(body, name);
        REQUIRE(found.has_value());
        INFO("role " << toString(role));
        CHECK(found->size() == 1);
        CHECK(checkFaceName(m.doc, name).has_value());
    }
    // The start cap is the square, the end cap the circle.
    const auto start = geometry::findNamedFaces(body, FaceName{m.taper, {.role = FaceRole::StartCap}});
    REQUIRE(start.has_value());
    CHECK_THAT(start->front().area.in(units::mm2), WithinRel(400.0, bettercad::test::kRelTight));
    const auto end = geometry::findNamedFaces(body, FaceName{m.taper, {.role = FaceRole::EndCap}});
    REQUIRE(end.has_value());
    CHECK_THAT(end->front().area.in(units::mm2), WithinRel(pi * 25.0, bettercad::test::kRelTight));

    // A side is still not a named face of a loft.
    const auto side = checkFaceName(m.doc, FaceName{m.taper, {.role = FaceRole::Side,
                                                              .entity = EntityId::fromValue(1)}});
    REQUIRE_FALSE(side.has_value());
    CHECK_THAT(side.error().message, ContainsSubstring("is a loft, whose sides are not planes and are not named"));
}

TEST_CASE("LoftShapes_AreDeterministic", "[loft][features][p12][determinism]") {
    const auto fingerprint = [](Document& doc, ObjectId feature, Regenerator& regenerator) {
        REQUIRE(requireReport(regenerator, doc).succeeded());
        const geometry::Body* body = regenerator.body(feature);
        REQUIRE(body != nullptr);
        const auto properties = body->massProperties().value();
        return std::vector<double>{properties.volume.si(), properties.surfaceArea.si(),
                                   properties.centerOfMass.x.si(), properties.centerOfMass.y.si(),
                                   properties.centerOfMass.z.si()};
    };
    SECTION("differing shapes") {
        auto first = std::make_unique<ShapeLoftModel>();
        Regenerator a;
        const std::vector<double> once = fingerprint(first->doc, first->taper, a);
        Regenerator again;
        CHECK(fingerprint(first->doc, first->taper, again) == once);
        auto second = std::make_unique<ShapeLoftModel>();
        Regenerator b;
        CHECK(fingerprint(second->doc, second->taper, b) == once);
    }
    SECTION("smooth interpolation") {
        auto first = std::make_unique<SmoothLoftModel>();
        Regenerator a;
        const std::vector<double> once = fingerprint(first->doc, first->spool, a);
        Regenerator again;
        CHECK(fingerprint(first->doc, first->spool, again) == once);
        auto second = std::make_unique<SmoothLoftModel>();
        Regenerator b;
        CHECK(fingerprint(second->doc, second->spool, b) == once);
    }
}

// ---------------------------------------------------------------------------
// Smooth interpolation
// ---------------------------------------------------------------------------

TEST_CASE("LoftSmooth_FollowsTheQuadraticThroughItsSections", "[loft][features][p12][acceptance]") {
    SmoothLoftModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    INFO("smooth " << volumeMm3(regenerator, m.spool) << " mm^3, law " << SmoothLoftModel::smoothVolume());
    CHECK_THAT(volumeMm3(regenerator, m.spool), WithinRel(SmoothLoftModel::smoothVolume(), kRelSpline));
    // Its sides run across the waist instead of being split there.
    CHECK(requireBody(regenerator, m.spool).topology().faces == 3);
}

TEST_CASE("LoftSmooth_IsADifferentSolidFromTheRuledOne", "[loft][features][p12][acceptance]") {
    SmoothLoftModel smooth;
    SmoothLoftModel ruled{LoftInterpolation::Ruled};
    Regenerator a;
    Regenerator b;
    REQUIRE(requireReport(a, smooth.doc).succeeded());
    REQUIRE(requireReport(b, ruled.doc).succeeded());
    CHECK_THAT(volumeMm3(a, smooth.spool), WithinRel(SmoothLoftModel::smoothVolume(), kRelSpline));
    CHECK_THAT(volumeMm3(b, ruled.spool), WithinRel(SmoothLoftModel::ruledVolume(), bettercad::test::kRelTight));
    // Four fifths, and a side that is not split at the waist.
    CHECK_THAT(volumeMm3(a, smooth.spool) / volumeMm3(b, ruled.spool), WithinRel(0.8, 1e-8));
    CHECK(requireBody(a, smooth.spool).topology().faces == 3);
    CHECK(requireBody(b, ruled.spool).topology().faces == 4);
}

TEST_CASE("LoftSmooth_TwoSectionFeatureMatchesTheRuledFeature", "[loft][features][p12]") {
    // The feature-level case of LoftSmooth_WithTwoSectionsIsTheRuledLoft in
    // LoftShapeTests.cpp: nothing to run across, so the two interpolations
    // agree, both against the closed form rather than against each other.
    ShapeLoftModel smooth{LoftInterpolation::Smooth};
    ShapeLoftModel ruled{LoftInterpolation::Ruled};
    Regenerator a;
    Regenerator b;
    REQUIRE(requireReport(a, smooth.doc).succeeded());
    REQUIRE(requireReport(b, ruled.doc).succeeded());
    const double expected = ShapeLoftModel::expectedVolume(10.0, 5.0, 30.0);
    CHECK_THAT(volumeMm3(a, smooth.taper), WithinRel(expected, kRelSpline));
    CHECK_THAT(volumeMm3(b, ruled.taper), WithinRel(expected, kRelSpline));
    CHECK_THAT(volumeMm3(a, smooth.taper), WithinRel(volumeMm3(b, ruled.taper), kRelSpline));
}

TEST_CASE("LoftSmooth_EditingTheInterpolationIsUndoable", "[loft][features][undo][p12][acceptance]") {
    SmoothLoftModel m{LoftInterpolation::Ruled};
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double ruled = volumeMm3(regenerator, m.spool);
    CHECK_THAT(ruled, WithinRel(SmoothLoftModel::ruledVolume(), bettercad::test::kRelTight));

    LoftDefinition d = m.definitionOf(m.spool);
    d.interpolation = LoftInterpolation::Smooth;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLoftCommand>(
                                       FeatureId::fromValue(m.spool.value()), d))
                .has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.spool), WithinRel(SmoothLoftModel::smoothVolume(), kRelSpline));

    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definitionOf(m.spool).interpolation == LoftInterpolation::Ruled);
    CHECK(bits(volumeMm3(regenerator, m.spool)) == bits(ruled));

    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definitionOf(m.spool).interpolation == LoftInterpolation::Smooth);
}

TEST_CASE("LoftSmooth_FailsAtomicallyLikeARuledLoft", "[loft][features][p12][acceptance]") {
    SmoothLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double before = volumeMm3(regenerator, m.spool);

    // The waist moved onto the bottom section's plane: no loft to make.
    REQUIRE(m.doc.setParameterValue(m.mid, 0_mm).has_value());
    const Error error = requireFailure(regenerator, m.doc, m.spool);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(error.message, ContainsSubstring("lie on the same plane"));
    CHECK(regenerator.body(m.spool) == nullptr);

    REQUIRE(m.doc.setParameterValue(m.mid, 25_mm).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(bits(volumeMm3(regenerator, m.spool)) == bits(before));
}
