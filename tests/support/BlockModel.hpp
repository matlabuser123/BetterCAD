#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

namespace bettercad::test {

// A parametric block, the base of the edge-operation fixtures:
//
//   width, length -> Base (XY plane: rectangle with a corner fixed at the origin)
//                 -> Pad (extrude along +Z) <- height
//
// plus one more length parameter for the operation built on it, created
// right after height. So every derived model has the same IDs: width 1,
// length 2, height 3, the extra parameter 4, Base 5 and Pad 6.
//
// The block spans [0, width] x [0, length] x [0, height] = 100 x 50 x 20 mm.
struct BlockModel {
    Document doc{"Block"};
    ParameterId width, length, height, extra;
    ObjectId base, pad;

    /// The line along X through (0, y, z) mm, e.g. alongX(0, 20) is the top front edge.
    static geometry::EdgeSignature alongX(double yMm, double zMm) {
        return geometry::lineSignature(Point3D{Length{}, yMm * units::mm, zMm * units::mm}, Direction3D::unitX());
    }
    /// The line along Y through (x, 0, z) mm.
    static geometry::EdgeSignature alongY(double xMm, double zMm) {
        return geometry::lineSignature(Point3D{xMm * units::mm, Length{}, zMm * units::mm}, Direction3D::unitY());
    }
    /// The line along Z through (x, y, 0) mm.
    static geometry::EdgeSignature alongZ(double xMm, double yMm) {
        return geometry::lineSignature(Point3D{xMm * units::mm, yMm * units::mm, Length{}}, Direction3D::unitZ());
    }

    static SketchId sketchId(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    BlockModel(const std::string& extraName, Length extraValue) {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        width = doc.createParameter("width", 100_mm, units::mm).value();
        length = doc.createParameter("length", 50_mm, units::mm).value();
        height = doc.createParameter("height", 20_mm, units::mm).value();
        extra = doc.createParameter(extraName, extraValue, units::mm).value();

        auto rectangle = std::make_unique<Sketch>("Base");
        const auto lines = addRectangle(*rectangle, 0_mm, 0_mm, 10_mm, 10_mm);
        require(rectangle->addFixed(std::get<LineEntity>(rectangle->findEntity(lines[0])->geometry).start));
        require(rectangle->addHorizontal(lines[0]));
        require(rectangle->addHorizontal(lines[2]));
        require(rectangle->addVertical(lines[1]));
        require(rectangle->addVertical(lines[3]));
        const ConstraintId w = require(rectangle->addDistance(lines[0], 1_mm));
        const ConstraintId l = require(rectangle->addDistance(lines[1], 1_mm));
        REQUIRE(rectangle->setConstraintParameter(w, width).has_value());
        REQUIRE(rectangle->setConstraintParameter(l, length).has_value());
        base = doc.addObject(std::move(rectangle)).value();

        auto extrude = features::ExtrudeFeature::create("Pad", {.profile = sketchId(base), .depthParameter = height});
        REQUIRE(extrude.has_value());
        pad = doc.addObject(std::move(*extrude)).value();
    }

    /// Adds a feature of kind F made from @p definition.
    template <typename F>
    ObjectId add(const std::string& name, const typename F::Definition& definition) {
        auto feature = F::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    template <typename F>
    [[nodiscard]] typename F::Definition definitionOf(ObjectId id) const {
        return doc.findObjectAs<F>(id)->definition();
    }

    template <typename F>
    void setDefinition(ObjectId id, const typename F::Definition& definition) {
        REQUIRE(doc.modifyObject<F>(id, [&](F& f) { return f.setDefinition(definition); }).has_value());
    }
};

} // namespace bettercad::test
