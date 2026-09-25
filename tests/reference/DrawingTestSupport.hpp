#pragma once

#include "io/DrawingExportSupport.hpp"
#include "reference/ReferenceTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <DrawingReferenceModels.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/Resolution.hpp>
#include <bettercad/drawing/SheetScene.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DrawingExport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Support for the production DRAWING reference models (P14-REFMOD-001).
//
// Everything here drives the PRODUCTION path: the public builders, a real
// `features::Regenerator` with the assembly and drawing handlers registered,
// and `drawing::sheetScene` for what a sheet actually draws. There is no back
// door that regenerates a drawing some other way, and nothing here derives an
// expected value from the model's own result -- the expectations live in the
// tests beside the arithmetic that produced them.
namespace bettercad::test::drawref {

/// The message of a failed Result, or nothing.
///
/// INFO builds its text with operator<<, which binds tighter than `?:`, so a
/// conditional handed straight to it is parsed as `(builder << cond) ? a : b`
/// and does not compile. The text is built first, here.
template <typename T>
[[nodiscard]] inline std::string why(const Result<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

/// PAPER-space tolerance, in millimetres.
///
/// The writers print four decimals of a millimetre (P14-EXPORT-001), so this
/// is the FORMAT's own resolution rather than a chosen slack: a coordinate
/// read back from a file cannot be closer than this to the scene's, and
/// nothing that matters on a drawing is smaller.
inline constexpr double kPaperMm = 1e-4;

/// A reference drawing, regenerated the way production regenerates one.
class Drawn {
public:
    explicit Drawn(Document document) : document_(std::move(document)) {
        assembly::registerHandlers(regenerator_, nullptr, nullptr);
        drawing::registerHandlers(regenerator_);
        report_ = regenerator_.regenerateAll(document_);
    }

    [[nodiscard]] Document& document() noexcept { return document_; }
    [[nodiscard]] const Document& document() const noexcept { return document_; }
    [[nodiscard]] features::Regenerator& regenerator() noexcept { return regenerator_; }
    [[nodiscard]] const features::Regenerator& regenerator() const noexcept { return regenerator_; }

    /// Regenerates again, after a change.
    void regenerate() { report_ = regenerator_.regenerateAll(document_); }

    /// Every object regenerated, and whether every one of them succeeded.
    [[nodiscard]] bool succeeded() const { return report_.has_value() && report_->succeeded(); }

    /// Whether the last regeneration reported @p object as failed.
    ///
    /// A drawing whose reference stopped resolving fails LOUDLY (ADR-014):
    /// the object is reported, and nothing downstream is published. Asking
    /// which object failed is how a test says it failed for the right reason
    /// rather than merely that something did.
    [[nodiscard]] bool failedOn(ObjectId object) const {
        return report_.has_value() &&
               std::ranges::find(report_->failed, object) != report_->failed.end();
    }
    [[nodiscard]] std::string errorOn(ObjectId object) const {
        if (!report_) {
            return report_.error().message;
        }
        const auto found = report_->errors.find(object);
        return found == report_->errors.end() ? std::string{} : found->second.message;
    }
    [[nodiscard]] std::string why() const {
        if (!report_) {
            return report_.error().message;
        }
        std::string text;
        for (const ObjectId object : report_->failed) {
            const auto found = report_->errors.find(object);
            text += std::format("{} failed: {}; ", object.value(),
                                found == report_->errors.end() ? "no error recorded"
                                                               : found->second.message);
        }
        for (const ObjectId object : report_->blocked) {
            text += std::format("{} blocked; ", object.value());
        }
        return text;
    }

    [[nodiscard]] drawing::BodyLookup bodies() const {
        return [this](ObjectId object) { return regenerator_.body(object); };
    }
    [[nodiscard]] drawing::TransformLookup transforms() const {
        return [this](ComponentId component) { return regenerator_.transform(component); };
    }

    /// What a sheet draws, through the production assembly path.
    [[nodiscard]] Result<drawing::DrawingScene> scene(SheetId sheet) const {
        return drawing::sheetScene(document_, sheet, bodies(), transforms());
    }

private:
    Document document_;
    features::Regenerator regenerator_;
    Result<features::RegenerationReport> report_{};
};

/// Builds a drawing reference model and regenerates it, requiring success.
[[nodiscard]] inline Drawn drawn(reference::DrawingReferenceModelKind kind) {
    auto document = reference::buildDrawingReferenceModel(kind);
    INFO(why(document));
    REQUIRE(document.has_value());
    Drawn result{std::move(*document)};
    INFO(result.why());
    REQUIRE(result.succeeded());
    return result;
}

/// The sheet's scene, required to build and to validate.
[[nodiscard]] inline drawing::DrawingScene sceneOf(const Drawn& model, SheetId sheet) {
    auto scene = model.scene(sheet);
    INFO(why(scene));
    REQUIRE(scene.has_value());
    const auto valid = drawing::validate(*scene);
    INFO(why(valid));
    REQUIRE(valid.has_value());
    return *scene;
}

/// A measured LINEAR dimension, in millimetres.
[[nodiscard]] inline double measuredMm(const Drawn& model, DimensionId dimension) {
    auto measured = drawing::measure(model.document(), dimension, model.bodies(),
                                     model.transforms());
    INFO(why(measured));
    REQUIRE(measured.has_value());
    REQUIRE(measured->length.has_value());
    return measured->length->in(units::mm);
}

/// A measured ANGULAR dimension, in degrees.
[[nodiscard]] inline double measuredDegrees(const Drawn& model, DimensionId dimension) {
    auto measured = drawing::measure(model.document(), dimension, model.bodies(),
                                     model.transforms());
    INFO(why(measured));
    REQUIRE(measured.has_value());
    REQUIRE(measured->angle.has_value());
    return measured->angle->in(units::deg);
}

/// A body's volume in cubic millimetres.
[[nodiscard]] inline double volumeMm3(const geometry::Body& body) {
    const auto properties = body.massProperties();
    INFO(why(properties));
    REQUIRE(properties.has_value());
    return properties->volume.in(units::mm3);
}

/// A measured dimension's text, as the drawing writes it -- which is where a
/// tolerance, a fit and a unit become visible.
[[nodiscard]] inline std::string dimensionText(const Drawn& model, DimensionId dimension) {
    auto measured = drawing::measure(model.document(), dimension, model.bodies(),
                                     model.transforms());
    INFO(why(measured));
    REQUIRE(measured.has_value());
    return measured->text;
}

/// An annotation's resolved text: a hole callout's words, a datum's letter, a
/// balloon's item number.
[[nodiscard]] inline std::string annotationTextOf(const Drawn& model, AnnotationId annotation) {
    auto text = drawing::annotationText(model.document(), annotation, model.bodies(),
                                        model.transforms());
    INFO(why(text));
    REQUIRE(text.has_value());
    return *text;
}

/// How an annotation's reference to the model stands now.
[[nodiscard]] inline drawing::Resolution resolutionOf(const Drawn& model,
                                                       AnnotationId annotation) {
    return drawing::annotationResolution(model.document(), annotation, model.bodies(),
                                         model.transforms());
}

/// The bill of materials of a view, computed from the assembly as it is.
[[nodiscard]] inline drawing::BillOfMaterials bomOf(const Drawn& model, ViewId view) {
    auto bom = drawing::billOfMaterials(model.document(), view);
    INFO(why(bom));
    REQUIRE(bom.has_value());
    return *bom;
}

/// The ID of the parameter named @p name, required to exist.
[[nodiscard]] inline ParameterId parameterNamed(const Document& document, std::string_view name) {
    const ObjectId object = document.findByName(name).value_or(ObjectId{});
    INFO("no parameter named " << name);
    REQUIRE(object.isValid());
    const ParameterId parameter = document.asParameter(object).value_or(ParameterId{});
    REQUIRE(parameter.isValid());
    return parameter;
}

/// Sets the length parameter named @p name to @p millimetres and regenerates,
/// requiring both to succeed. The PRODUCTION path: an edit, then the ordinary
/// regeneration, with nothing told which objects to rebuild.
inline void driveMm(Drawn& model, std::string_view name, double millimetres) {
    const ParameterId parameter = parameterNamed(model.document(), name);
    auto changed = model.document().setParameterValue(parameter, millimetres * units::mm);
    INFO(why(changed));
    REQUIRE(changed.has_value());
    model.regenerate();
    INFO(model.why());
    REQUIRE(model.succeeded());
}

/// Whether two sheets draw the same thing.
///
/// DrawingScene has no equality of its own -- SceneItems deliberately has
/// none, because "the same scene" means the same primitives in the same
/// order, and comparing the three vectors says so explicitly.
[[nodiscard]] inline bool sameScene(const drawing::DrawingScene& a,
                                    const drawing::DrawingScene& b) {
    return a.width == b.width && a.height == b.height && a.items.lines == b.items.lines &&
           a.items.arcs == b.items.arcs && a.items.texts == b.items.texts;
}

/// Suppresses or unsuppresses a component at the BASE level -- the supported
/// path, `setComponentDefinition`, rather than reaching past it -- and
/// regenerates.
inline void suppressBase(Drawn& model, ComponentId component, bool suppressed) {
    const auto* existing = model.document().findObjectAs<assembly::Component>(component);
    INFO("no component " << component.value());
    REQUIRE(existing != nullptr);
    assembly::ComponentDefinition definition = existing->definition();
    definition.suppressed = suppressed;
    auto changed = assembly::setComponentDefinition(model.document(), component, definition);
    INFO(why(changed));
    REQUIRE(changed.has_value());
    model.regenerate();
}

/// Every circular arc a sheet draws whose radius is @p radiusMm, as its
/// centre in SHEET millimetres, sorted so two runs compare.
///
/// A full circle arrives as one SceneArc of a whole turn, so a hole is one
/// item and not a thousand -- which is what makes counting them meaningful.
[[nodiscard]] inline std::vector<std::pair<double, double>> circleCentresMm(
    const drawing::DrawingScene& scene, double radiusMm, double tolerance = 1e-6) {
    std::vector<std::pair<double, double>> centres;
    for (const drawing::SceneArc& arc : scene.items.arcs) {
        if (std::abs(arc.radius.in(units::mm) - radiusMm) <= tolerance) {
            centres.emplace_back(arc.centre.x.in(units::mm), arc.centre.y.in(units::mm));
        }
    }
    std::ranges::sort(centres);
    return centres;
}

/// Makes @p configuration active and regenerates, WITHOUT requiring the
/// regeneration to succeed.
///
/// A configuration that takes a component out from under a balloon leaves
/// that balloon with nothing to label, and BetterCAD reports it rather than
/// drawing something else -- so a test of that behaviour cannot require
/// success and then assert the failure.
inline void activate(Drawn& model, std::optional<ConfigurationId> configuration) {
    auto changed = model.document().setActiveConfiguration(configuration);
    INFO(why(changed));
    REQUIRE(changed.has_value());
    model.regenerate();
}

/// The largest distance between corresponding points of two scenes'
/// polylines, in sheet millimetres; infinity if they do not have the same
/// lines with the same numbers of points.
///
/// A drawing restored by putting a parameter back is arithmetically the one
/// it started as, but it is recomputed rather than remembered: the kernel
/// runs again and the last bits of a double need not repeat. This says HOW
/// FAR apart two such drawings are, so a test can assert that the difference
/// is rounding rather than assume it.
[[nodiscard]] inline double maxLineDeviationMm(const drawing::DrawingScene& a,
                                               const drawing::DrawingScene& b) {
    if (a.items.lines.size() != b.items.lines.size()) {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < a.items.lines.size(); ++i) {
        const drawing::SceneLine& left = a.items.lines[i];
        const drawing::SceneLine& right = b.items.lines[i];
        if (left.points.size() != right.points.size() || left.style != right.style ||
            left.width != right.width) {
            return std::numeric_limits<double>::infinity();
        }
        for (std::size_t j = 0; j < left.points.size(); ++j) {
            worst = std::max(worst, std::hypot((left.points[j].x - right.points[j].x).in(units::mm),
                                               (left.points[j].y - right.points[j].y).in(units::mm)));
        }
    }
    return worst;
}

/// The same, for the arcs: the largest of the centre displacement and the
/// radius difference, in sheet millimetres.
[[nodiscard]] inline double maxArcDeviationMm(const drawing::DrawingScene& a,
                                              const drawing::DrawingScene& b) {
    if (a.items.arcs.size() != b.items.arcs.size()) {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < a.items.arcs.size(); ++i) {
        const drawing::SceneArc& left = a.items.arcs[i];
        const drawing::SceneArc& right = b.items.arcs[i];
        if (left.style != right.style || left.width != right.width) {
            return std::numeric_limits<double>::infinity();
        }
        worst = std::max({worst,
                          std::hypot((left.centre.x - right.centre.x).in(units::mm),
                                     (left.centre.y - right.centre.y).in(units::mm)),
                          std::abs((left.radius - right.radius).in(units::mm))});
    }
    return worst;
}

/// Whether two lists of circle centres agree to @p tolerance, pairwise.
///
/// Exact equality on projected coordinates would be asserting that two
/// arithmetically equal doubles came out of the kernel bit for bit, which is
/// a claim about rounding rather than about the drawing.
[[nodiscard]] inline bool sameCentres(const std::vector<std::pair<double, double>>& a,
                                      const std::vector<std::pair<double, double>>& b,
                                      double tolerance = kPaperMm) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].first - b[i].first) > tolerance ||
            std::abs(a[i].second - b[i].second) > tolerance) {
            return false;
        }
    }
    return true;
}

/// The scene's text runs, as plain strings, in the order they are drawn.
[[nodiscard]] inline std::vector<std::string> sceneTexts(const drawing::DrawingScene& scene) {
    std::vector<std::string> texts;
    texts.reserve(scene.items.texts.size());
    for (const drawing::SceneText& text : scene.items.texts) {
        texts.push_back(text.text);
    }
    return texts;
}

/// Whether the scene draws anything in the ISO 128 hidden-detail style, which
/// is the only evidence from outside that something was hidden by something.
[[nodiscard]] inline std::size_t hiddenLineCount(const drawing::DrawingScene& scene) {
    std::size_t hidden = 0;
    for (const drawing::SceneLine& line : scene.items.lines) {
        hidden += line.style == drawing::LineStyle::Dashed ? 1 : 0;
    }
    for (const drawing::SceneArc& arc : scene.items.arcs) {
        hidden += arc.style == drawing::LineStyle::Dashed ? 1 : 0;
    }
    return hidden;
}

} // namespace bettercad::test::drawref
