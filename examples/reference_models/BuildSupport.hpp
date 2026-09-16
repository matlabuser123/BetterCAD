#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Helpers shared by the reference-model builders. They only call the public
// document, parameter and sketch APIs; their one job is to keep the builders
// readable by recording the first error instead of returning after every
// call. A builder that records an error still runs to its end (every call on
// an invalid ID fails cleanly) and then reports that first error.
namespace bettercad::reference::detail {

class ModelBuilder {
public:
    explicit ModelBuilder(Document& document) noexcept : document_(document) {}

    /// The value of @p result; on failure a default value, after recording
    /// the error (prefixed with @p step) if it is the first.
    template <typename T>
    T need(Result<T> result, std::string_view step) {
        if (!result) {
            fail(result.error(), step);
            return T{};
        }
        return std::move(*result);
    }
    void need(const Result<void>& result, std::string_view step);

    ParameterId length(std::string name, double millimetres);
    ParameterId count(std::string name, double value);

    /// Adds a new feature of kind F, e.g. `feature<ExtrudeFeature>("Pad", {...})`.
    template <typename F>
    ObjectId feature(const std::string& name, const typename F::Definition& definition) {
        auto created = F::create(name, definition);
        if (!created) {
            fail(created.error(), name);
            return {};
        }
        return add(std::move(*created), name);
    }
    ObjectId add(std::unique_ptr<DocumentObject> object, std::string_view name);

    /// The value of a length parameter in millimetres (0 for a bad ID).
    [[nodiscard]] double millimetres(ParameterId parameter) const;

    [[nodiscard]] Document& document() noexcept { return document_; }
    [[nodiscard]] Result<void> status() const;

private:
    void fail(const Error& error, std::string_view step);

    Document& document_;
    std::optional<Error> error_;
};

/// A sketch under construction. Positions and literal values are in
/// millimetres. Driven dimensions start at their parameter's current value,
/// so a fresh model is already solved.
class SketchBuilder {
public:
    SketchBuilder(ModelBuilder& model, std::string name, const Frame3D& plane);

    EntityId point(double u, double v);
    /// A line between existing points.
    EntityId line(EntityId start, EntityId end);
    EntityId circle(EntityId centre, double radius);
    /// A counter-clockwise arc between existing points about @p centre.
    EntityId arc(EntityId centre, EntityId start, EntityId end);
    void construction(EntityId entity);

    void fixed(EntityId point);
    void horizontal(EntityId line);
    void vertical(EntityId line);
    void horizontal(EntityId pointA, EntityId pointB);
    void vertical(EntityId pointA, EntityId pointB);
    void coincident(EntityId pointA, EntityId pointB);
    void equal(EntityId a, EntityId b);

    /// A line's length, driven by @p parameter.
    void length(EntityId line, ParameterId parameter);
    /// A point-point or point-line distance, driven by @p parameter.
    void distance(EntityId a, EntityId b, ParameterId parameter);
    /// A point-point or point-line distance with a literal value.
    void distance(EntityId a, EntityId b, double value);
    /// A circle's or arc's radius, driven by @p parameter.
    void radius(EntityId circleOrArc, ParameterId parameter);
    /// A circle's or arc's radius with a literal value.
    void radius(EntityId circleOrArc, double value);

    /// The start and end points of a line or arc.
    [[nodiscard]] std::pair<EntityId, EntityId> ends(EntityId lineOrArc) const;

    /// Adds the sketch to the document and returns its ID.
    ObjectId finish();

private:
    void drive(Result<ConstraintId> constraint, ParameterId parameter, std::string_view what);

    ModelBuilder& model_;
    std::string name_;
    std::unique_ptr<sketch::Sketch> sketch_;
};

[[nodiscard]] constexpr SketchId sketchId(ObjectId id) noexcept { return SketchId::fromValue(id.value()); }
[[nodiscard]] constexpr FeatureId featureId(ObjectId id) noexcept { return FeatureId::fromValue(id.value()); }

/// A document ID from its canonical text, for the models' fixed identities
/// (invalid if the text is not a UUID).
[[nodiscard]] DocumentId fixedDocumentId(std::string_view uuid);

/// A point given in millimetres.
[[nodiscard]] Point3D pointMm(double x, double y, double z);

/// A horizontal sketch plane at height @p zMm (local axes along X and Y).
[[nodiscard]] Frame3D levelPlane(double zMm);

} // namespace bettercad::reference::detail
