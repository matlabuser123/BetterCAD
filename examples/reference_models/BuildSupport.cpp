#include "BuildSupport.hpp"

#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/parameters/Parameter.hpp>

#include <format>
#include <variant>

namespace bettercad::reference::detail {

using namespace bettercad::literals;

void ModelBuilder::need(const Result<void>& result, std::string_view step) {
    if (!result) {
        fail(result.error(), step);
    }
}

void ModelBuilder::fail(const Error& error, std::string_view step) {
    if (!error_) {
        error_ = Error{error.code, std::format("{}: {}: {}", document_.name(), step, error.message)};
    }
}

ParameterId ModelBuilder::length(std::string name, double millimetres) {
    const std::string step = name;
    return need(document_.createParameter(std::move(name), millimetres * units::mm, units::mm), step);
}

ParameterId ModelBuilder::count(std::string name, double value) {
    const std::string step = name;
    return need(document_.createParameter(std::move(name), value, kUnitless), step);
}

ObjectId ModelBuilder::add(std::unique_ptr<DocumentObject> object, std::string_view name) {
    return need(document_.addObject(std::move(object)), name);
}

double ModelBuilder::millimetres(ParameterId parameter) const {
    const Parameter* found = document_.parameters().find(parameter);
    return found == nullptr ? 0.0 : Length::fromSi(found->siValue()).in(units::mm);
}

Result<void> ModelBuilder::status() const {
    if (error_) {
        return std::unexpected(*error_);
    }
    return {};
}

SketchBuilder::SketchBuilder(ModelBuilder& model, std::string name, const Frame3D& plane)
    : model_(model), name_(name), sketch_(std::make_unique<sketch::Sketch>(std::move(name), plane)) {}

EntityId SketchBuilder::point(double u, double v) {
    return model_.need(sketch_->addPoint(Point2D{u * units::mm, v * units::mm}), name_);
}

EntityId SketchBuilder::line(EntityId start, EntityId end) {
    return model_.need(sketch_->addLine(start, end), name_);
}

EntityId SketchBuilder::circle(EntityId centre, double radius) {
    return model_.need(sketch_->addCircle(centre, radius * units::mm), name_);
}

EntityId SketchBuilder::arc(EntityId centre, EntityId start, EntityId end) {
    return model_.need(sketch_->addArc(centre, start, end), name_);
}

void SketchBuilder::construction(EntityId entity) {
    model_.need(sketch_->setConstruction(entity, true), name_);
}

void SketchBuilder::fixed(EntityId point) {
    model_.need(sketch_->addFixed(point), name_);
}

void SketchBuilder::horizontal(EntityId line) {
    model_.need(sketch_->addHorizontal(line), name_);
}

void SketchBuilder::vertical(EntityId line) {
    model_.need(sketch_->addVertical(line), name_);
}

void SketchBuilder::horizontal(EntityId pointA, EntityId pointB) {
    model_.need(sketch_->addHorizontal(pointA, pointB), name_);
}

void SketchBuilder::vertical(EntityId pointA, EntityId pointB) {
    model_.need(sketch_->addVertical(pointA, pointB), name_);
}

void SketchBuilder::coincident(EntityId pointA, EntityId pointB) {
    model_.need(sketch_->addCoincident(pointA, pointB), name_);
}

void SketchBuilder::equal(EntityId a, EntityId b) {
    model_.need(sketch_->addEqual(a, b), name_);
}

void SketchBuilder::drive(Result<ConstraintId> constraint, ParameterId parameter, std::string_view what) {
    const ConstraintId id = model_.need(std::move(constraint), std::format("{}: {}", name_, what));
    model_.need(sketch_->setConstraintParameter(id, parameter), std::format("{}: {}", name_, what));
}

void SketchBuilder::length(EntityId line, ParameterId parameter) {
    drive(sketch_->addDistance(line, model_.millimetres(parameter) * units::mm), parameter, "length");
}

void SketchBuilder::distance(EntityId a, EntityId b, ParameterId parameter) {
    drive(sketch_->addDistance(a, b, model_.millimetres(parameter) * units::mm), parameter, "distance");
}

void SketchBuilder::distance(EntityId a, EntityId b, double value) {
    model_.need(sketch_->addDistance(a, b, value * units::mm), name_);
}

void SketchBuilder::radius(EntityId circleOrArc, ParameterId parameter) {
    drive(sketch_->addRadius(circleOrArc, model_.millimetres(parameter) * units::mm), parameter, "radius");
}

void SketchBuilder::radius(EntityId circleOrArc, double value) {
    model_.need(sketch_->addRadius(circleOrArc, value * units::mm), name_);
}

std::pair<EntityId, EntityId> SketchBuilder::ends(EntityId lineOrArc) const {
    const sketch::Entity* entity = sketch_->findEntity(lineOrArc);
    if (entity == nullptr) {
        return {};
    }
    if (const auto* line = std::get_if<sketch::LineEntity>(&entity->geometry)) {
        return {line->start, line->end};
    }
    if (const auto* arc = std::get_if<sketch::ArcEntity>(&entity->geometry)) {
        return {arc->start, arc->end};
    }
    return {};
}

void SketchBuilder::attach(const PlaneReference& reference) {
    model_.need(sketch_->setAttachment(reference), name_);
}

ObjectId SketchBuilder::finish() {
    return model_.add(std::move(sketch_), name_);
}

DocumentId fixedDocumentId(std::string_view uuid) {
    const std::optional<Uuid> parsed = Uuid::parse(uuid);
    return parsed ? DocumentId::fromValue(*parsed) : DocumentId{};
}

Point3D pointMm(double x, double y, double z) {
    return Point3D{x * units::mm, y * units::mm, z * units::mm};
}

Frame3D levelPlane(double zMm) {
    return Frame3D::create(pointMm(0.0, 0.0, zMm), Direction3D::unitZ(), Direction3D::unitX()).value();
}

} // namespace bettercad::reference::detail
