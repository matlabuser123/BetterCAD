#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/sketch/Constraints.hpp>
#include <bettercad/sketch/Entities.hpp>
#include <bettercad/sketch/Export.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::sketch {

/// A 2D sketch: entities in the local coordinates of a plane placed in model
/// space. Sketches are document objects (type name "sketch").
///
/// - Entity IDs are allocated per sketch, start at 1 and are never reused.
/// - Entities that reference points (lines, circles, arcs, ellipses,
///   splines) keep those points alive: a referenced point cannot be removed.
/// - Geometry is validated when entities are created. Point positions can be
///   edited freely afterwards (the solver moves them); keeping arc ends on
///   their circle and ellipse axes perpendicular is then the solver's job.
/// - Inside a Document, mutate a sketch through Document::modifyObject so the
///   change is tracked.
class BETTERCAD_SKETCH_EXPORT Sketch final : public DocumentObject {
public:
    /// Geometric tolerance for validity checks (the kernel's precision).
    static constexpr Length kLengthTolerance = Length::fromSi(1e-10);

    explicit Sketch(std::string name, const Frame3D& placement = Frame3D::xy());

    [[nodiscard]] std::string_view typeName() const noexcept override { return "sketch"; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// Parameters that drive constraint values and the objects the sketch's
    /// attachment refers to: the attached object and, for a copied face, the
    /// features that copy it (ascending, unique).
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// Copies point positions, circle radii and constraint values (lengths
    /// and angles) from @p solved, which must have the same entities and
    /// constraints (e.g. a solved clone of this sketch). Returns whether
    /// anything changed.
    Result<bool> adoptSolution(const Sketch& solved);

    /// Makes the placement, attachment, entities, constraints and ID counters equal to
    /// @p state's; the object's own identity (ID, name, revision) is kept.
    /// Used by undoable sketch edits (ModifySketchCommand). Returns whether
    /// anything changed.
    Result<bool> restoreContent(const Sketch& state);

    /// Typed view of id(); invalid until the sketch is in a document.
    [[nodiscard]] SketchId sketchId() const noexcept { return SketchId::fromValue(id().value()); }

    // --- Placement (sketch coordinate system) ----------------------------------
    [[nodiscard]] const Frame3D& placement() const noexcept { return placement_; }
    Result<bool> setPlacement(const Frame3D& placement);
    /// The plane the placement follows (P12-DATUM-001), if any: a datum
    /// plane, a coordinate system's principal plane or one of the model's.
    /// Regeneration resolves it and makes it the placement once the sketch
    /// has solved; the placement is otherwise kept as set.
    [[nodiscard]] const std::optional<PlaneReference>& attachment() const noexcept { return attachment_; }
    /// Fails with InvalidArgument for an invalid object ID.
    Result<bool> setAttachment(std::optional<PlaneReference> attachment);
    [[nodiscard]] Point3D toGlobal(const Point2D& local) const noexcept {
        return placement_.toGlobal(local);
    }
    [[nodiscard]] Point2D toLocal(const Point3D& global) const noexcept {
        return placement_.toLocal(global);
    }

    // --- Creating entities -----------------------------------------------------
    Result<EntityId> addPoint(const Point2D& position);
    /// New line with two new end points.
    Result<EntityId> addLine(const Point2D& start, const Point2D& end);
    /// New line between existing point entities.
    Result<EntityId> addLine(EntityId startPoint, EntityId endPoint);
    Result<EntityId> addCircle(const Point2D& center, Length radius);
    Result<EntityId> addCircle(EntityId centerPoint, Length radius);
    /// Counter-clockwise arc on the circle (center, radius) from startAngle to
    /// endAngle; creates the centre, start and end points.
    Result<EntityId> addArc(const Point2D& center, Length radius, Angle startAngle, Angle endAngle);
    /// Counter-clockwise arc between existing points, which must be equidistant
    /// from the centre (within kLengthTolerance).
    Result<EntityId> addArc(EntityId centerPoint, EntityId startPoint, EntityId endPoint);
    /// Ellipse around @p center with the semi-axis @p radiusX in the direction
    /// @p rotation and the semi-axis @p radiusY 90° counter-clockwise from it;
    /// creates the centre and the two vertex points.
    Result<EntityId> addEllipse(const Point2D& center, Length radiusX, Length radiusY, Angle rotation = Angle{});
    /// Ellipse on existing points. Both vertices must be further than
    /// kLengthTolerance from the centre, in perpendicular directions: neither
    /// vertex may be further than kLengthTolerance from the perpendicular
    /// through the centre to the other's axis.
    Result<EntityId> addEllipse(EntityId centerPoint, EntityId xVertex, EntityId yVertex);
    /// Spline on new pole points (see SplineEntity and UniformBSpline for the
    /// accepted degrees and pole counts).
    Result<EntityId> addSpline(const std::vector<Point2D>& poles, int degree = 3, bool periodic = false);
    /// Spline on existing, distinct point entities.
    Result<EntityId> addSpline(std::vector<EntityId> poles, int degree = 3, bool periodic = false);

    // --- Editing ---------------------------------------------------------------
    // Mutators return whether anything changed.
    Result<bool> setPointPosition(EntityId point, const Point2D& position);
    Result<bool> setCircleRadius(EntityId circle, Length radius);
    Result<bool> setConstruction(EntityId entity, bool construction);
    /// Removes an entity that no other entity and no constraint references.
    Result<void> removeEntity(EntityId entity);
    /// Inserts an entity that already has an ID (loading). Referenced points
    /// must exist and be distinct, coordinates must be finite, and a spline
    /// needs a supported degree and enough poles. Geometric conditions such as
    /// non-zero length are not re-checked: editing may leave them violated
    /// until the next solve, and any such state must be loadable.
    Result<void> insertEntity(const Entity& entity);
    /// Makes later allocations return IDs above these values (loading).
    void reserveIds(std::uint64_t lastEntity, std::uint64_t lastConstraint) noexcept {
        entityIds_.reserveThrough(lastEntity);
        constraintIds_.reserveThrough(lastConstraint);
    }

    // --- Constraints -----------------------------------------------------------
    /// Adds a constraint after checking its references and its value or
    /// angle (see Constraints.hpp for the accepted signatures and the
    /// canonical entity orders it stores).
    Result<ConstraintId> addConstraint(ConstraintType type, std::vector<EntityId> entities,
                                       std::optional<Length> value = std::nullopt,
                                       std::optional<Angle> angle = std::nullopt);
    Result<ConstraintId> addCoincident(EntityId pointA, EntityId pointB);
    Result<ConstraintId> addHorizontal(EntityId line);
    Result<ConstraintId> addHorizontal(EntityId pointA, EntityId pointB);
    Result<ConstraintId> addVertical(EntityId line);
    Result<ConstraintId> addVertical(EntityId pointA, EntityId pointB);
    Result<ConstraintId> addParallel(EntityId lineA, EntityId lineB);
    Result<ConstraintId> addPerpendicular(EntityId lineA, EntityId lineB);
    /// Length of a line.
    Result<ConstraintId> addDistance(EntityId line, Length value);
    /// Point-to-point or point-to-line distance.
    Result<ConstraintId> addDistance(EntityId a, EntityId b, Length value);
    Result<ConstraintId> addRadius(EntityId circleOrArc, Length value);
    Result<ConstraintId> addEqual(EntityId a, EntityId b);
    /// Holds a point at its current position while solving.
    Result<ConstraintId> addFixed(EntityId point);
    /// The angle from @p lineA counter-clockwise to @p lineB, in (0, 180°).
    Result<ConstraintId> addAngle(EntityId lineA, EntityId lineB, Angle value);
    /// A line and a circle or arc, or two circles or arcs, touching.
    Result<ConstraintId> addTangent(EntityId a, EntityId b);
    Result<ConstraintId> addConcentric(EntityId circleOrArcA, EntityId circleOrArcB);
    /// A point halfway along a line (either order).
    Result<ConstraintId> addMidpoint(EntityId a, EntityId b);
    /// Two points mirrored across @p line.
    Result<ConstraintId> addSymmetric(EntityId pointA, EntityId pointB, EntityId line);
    Result<ConstraintId> addDiameter(EntityId circleOrArc, Length value);
    /// Inserts a constraint that already has an ID (loading, undo); it is
    /// checked like a new one.
    Result<void> insertConstraint(Constraint constraint);
    Result<Constraint> removeConstraint(ConstraintId id);
    Result<bool> setConstraintEnabled(ConstraintId id, bool enabled);
    /// Changes the value of a Distance, Radius or Diameter constraint.
    Result<bool> setConstraintValue(ConstraintId id, Length value);
    /// Changes the angle of an Angle constraint.
    Result<bool> setConstraintAngle(ConstraintId id, Angle value);
    /// Sets or clears the parameter driving a constraint's value or angle
    /// (see isDrivable()).
    Result<bool> setConstraintParameter(ConstraintId id, std::optional<ParameterId> parameter);

    [[nodiscard]] const Constraint* findConstraint(ConstraintId id) const noexcept;
    [[nodiscard]] std::size_t constraintCount() const noexcept { return constraints_.size(); }
    /// All constraints in ascending ID order.
    [[nodiscard]] auto constraints() const { return std::views::values(constraints_); }
    /// Constraints that reference @p entity.
    [[nodiscard]] std::vector<ConstraintId> constraintsReferencing(EntityId entity) const;
    [[nodiscard]] std::uint64_t lastAllocatedConstraintId() const noexcept {
        return constraintIds_.lastValue();
    }

    // --- Queries -----------------------------------------------------------------
    [[nodiscard]] const Entity* findEntity(EntityId id) const noexcept;
    [[nodiscard]] std::optional<EntityType> entityType(EntityId id) const noexcept;
    [[nodiscard]] std::size_t entityCount() const noexcept { return entities_.size(); }
    /// All entities in ascending ID order.
    [[nodiscard]] auto entities() const { return std::views::values(entities_); }
    /// Entities that reference @p point.
    [[nodiscard]] std::vector<EntityId> dependentsOf(EntityId point) const;
    /// Highest entity ID value allocated so far.
    [[nodiscard]] std::uint64_t lastAllocatedEntityId() const noexcept {
        return entityIds_.lastValue();
    }

    [[nodiscard]] Result<Point2D> position(EntityId point) const;
    /// Start and end of a line, an arc or an open spline.
    [[nodiscard]] Result<Endpoints> endpoints(EntityId entity) const;
    /// Length of a line or arc, circumference of a circle or ellipse, length
    /// of a spline. Ellipses and splines are integrated numerically, to about
    /// 1e-12 relative for the curves the sketch accepts (Gauss-Legendre on
    /// 64 pieces per turn or per knot span).
    [[nodiscard]] Result<Length> length(EntityId entity) const;
    [[nodiscard]] Result<Length> radius(EntityId circleOrArc) const;
    /// Centre of a circle, an arc or an ellipse.
    [[nodiscard]] Result<Point2D> center(EntityId entity) const;
    /// Counter-clockwise angle of an arc from its start to its end, in [0, 2 pi).
    [[nodiscard]] Result<Angle> sweep(EntityId arc) const;
    /// Bounds of one entity: exact for points, lines, circles, arcs (with the
    /// extreme points they pass) and ellipses; for a spline, the bounds of its
    /// poles, which contain the curve.
    [[nodiscard]] Result<BoundingBox2D> boundingBox(EntityId entity) const;
    /// Bounds of all entities; std::nullopt for an empty sketch.
    [[nodiscard]] std::optional<BoundingBox2D> boundingBox() const;

private:
    [[nodiscard]] Result<const Entity*> require(EntityId id) const;
    [[nodiscard]] Result<Point2D> requirePoint(EntityId id) const;
    /// Positions of distinct, existing point entities, in order.
    [[nodiscard]] Result<std::vector<Point2D>> requirePoints(std::span<const EntityId> ids) const;
    EntityId insert(EntityGeometry geometry);
    [[nodiscard]] Result<void> checkConstraint(const Constraint& constraint) const;
    [[nodiscard]] Result<void> checkReferences(const Constraint& constraint) const;
    void canonicalize(Constraint& constraint) const;
    [[nodiscard]] Result<Constraint*> requireConstraint(ConstraintId id);

    Frame3D placement_;
    std::optional<PlaneReference> attachment_;
    IdAllocator entityIds_;
    std::map<EntityId, Entity> entities_;
    IdAllocator constraintIds_;
    std::map<ConstraintId, Constraint> constraints_;
};

} // namespace bettercad::sketch
