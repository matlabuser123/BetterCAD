#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchRegeneration.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <set>
#include <string_view>
#include <utility>
#include <variant>

namespace bettercad::features {

namespace {

/// "Name (object:7)", or just "object:7" for an ID that names nothing.
std::string label(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::format("{} ({})", *name, id) : std::format("{}", id);
}

/// "a sketch", "an extrude", ...
std::string withArticle(std::string_view noun) {
    const bool vowel = !noun.empty() && std::string_view{"aeiou"}.find(noun.front()) != std::string_view::npos;
    return std::format("{} {}", vowel ? "an" : "a", noun);
}

/// What kind of item @p id is, for messages: "a parameter", "a sketch", ...
std::string kindOf(const Document& document, ObjectId id) {
    if (document.asParameter(id)) {
        return "a parameter";
    }
    const DocumentObject* object = document.findObject(id);
    return object == nullptr ? "a missing item" : withArticle(object->typeName());
}

/// "an angle", "dimensionless", ...
std::string describeQuantity(const Dimension& dimension) {
    return dimension == dimensions::dimensionless ? std::string{"dimensionless"}
                                                  : withArticle(describeDimension(dimension));
}

class Checker {
public:
    explicit Checker(const Document& document)
        : document_(document), evaluated_(document.clone()),
          evaluation_(evaluateParameterExpressions(evaluated_)) {}

    ValidationReport run() {
        const DocumentGraph graph = buildDependencyGraph(document_);
        checkConsistency(graph);
        checkMissingReferences(graph);
        checkCycles(graph);
        checkSketches(graph);
        checkRegenerationAndGeometry();
        return std::move(report_);
    }

private:
    void add(ValidationCheck check, Severity severity, std::optional<ObjectId> item, std::string message) {
        if (severity == Severity::Error && item) {
            failedItems_.insert(*item);
        }
        report_.issues.push_back({check, severity, item, std::move(message)});
    }

    /// "<owner>: <reference> <item>, which is <actual>, not <expected>"
    void wrongKind(ObjectId owner, std::string_view reference, ObjectId item, std::string_view actual,
                   std::string_view expected) {
        add(ValidationCheck::DocumentConsistency, Severity::Error, owner,
            std::format("{}: {} {}, which is {}, not {}", label(document_, owner), reference, label(document_, item),
                        actual, expected));
    }

    /// A reference that exists must name a parameter of dimension @p expected.
    void checkParameter(ObjectId owner, ParameterId parameter, const Dimension& expected,
                        std::string_view reference) {
        const ObjectId id{parameter};
        if (!document_.contains(id)) {
            return; // a missing reference
        }
        const Parameter* found = document_.parameters().find(parameter);
        if (found == nullptr) {
            wrongKind(owner, reference, id, kindOf(document_, id), "a parameter");
        } else if (found->dimension() != expected) {
            wrongKind(owner, reference, id, describeQuantity(found->dimension()), describeQuantity(expected));
        }
    }

    /// A plane reference that names an existing object must name a datum
    /// plane or a coordinate system; an axis reference, a datum axis or a
    /// coordinate system; a coordinate system reference, a coordinate system.
    void checkPlaneReference(ObjectId owner, const PlaneReference& reference, std::string_view role) {
        if (reference.face) {
            if (!reference.object || !document_.contains(*reference.object)) {
                return; // a missing reference
            }
            if (auto valid = checkFaceName(document_, FaceName{*reference.object, *reference.face}); !valid) {
                add(ValidationCheck::DocumentConsistency, Severity::Error, owner,
                    std::format("{}: {} {}: {}", label(document_, owner), role,
                                describe(document_, FaceName{*reference.object, *reference.face}),
                                valid.error().message));
            }
            return;
        }
        if (reference.object && document_.contains(*reference.object) &&
            document_.findObjectAs<DatumPlane>(*reference.object) == nullptr &&
            document_.findObjectAs<CoordinateSystem>(*reference.object) == nullptr) {
            wrongKind(owner, role, *reference.object, kindOf(document_, *reference.object),
                      "a datum plane or a coordinate system");
        }
    }

    void checkAxisReference(ObjectId owner, const AxisReference& reference, std::string_view role) {
        if (reference.object && document_.contains(*reference.object) &&
            document_.findObjectAs<DatumAxis>(*reference.object) == nullptr &&
            document_.findObjectAs<CoordinateSystem>(*reference.object) == nullptr) {
            wrongKind(owner, role, *reference.object, kindOf(document_, *reference.object),
                      "a datum axis or a coordinate system");
        }
    }

    void checkSystemReference(ObjectId owner, const std::optional<ObjectId>& system, std::string_view role) {
        if (system && document_.contains(*system) && document_.findObjectAs<CoordinateSystem>(*system) == nullptr) {
            wrongKind(owner, role, *system, kindOf(document_, *system), "a coordinate system");
        }
    }

    /// A profile reference that exists must name a sketch.
    void checkProfile(ObjectId owner, SketchId profile) {
        const ObjectId id{profile};
        if (document_.contains(id) && document_.findObjectAs<sketch::Sketch>(id) == nullptr) {
            wrongKind(owner, "the profile is", id, kindOf(document_, id), "a sketch");
        }
    }

    /// "Name (parameter:3): expression 'x + 1': <message>"
    void expressionIssue(ValidationCheck check, const UnresolvedExpression& unresolved) {
        const ObjectId id{unresolved.parameter};
        const Parameter* parameter = document_.parameters().find(unresolved.parameter);
        add(check, Severity::Error, id,
            std::format("{}: expression '{}': {}", label(document_, id),
                        parameter != nullptr ? parameter->expression().value_or(std::string{}) : std::string{},
                        unresolved.error.message));
    }

    void checkConsistency(const DocumentGraph& graph) {
        // Expressions that do not parse or use names of objects.
        for (const UnresolvedExpression& unresolved : graph.unresolved) {
            if (unresolved.error.code != ErrorCode::NotFound) {
                expressionIssue(ValidationCheck::DocumentConsistency, unresolved);
            }
        }
        for (const DocumentObject& object : document_.objects()) {
            if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
                if (sketch->attachment()) {
                    checkPlaneReference(object.id(), *sketch->attachment(), "the attachment is");
                }
                for (const sketch::Constraint& constraint : sketch->constraints()) {
                    if (constraint.parameter) {
                        // Angle constraints take angles; the others lengths.
                        const Dimension expected =
                            sketch::hasAngle(constraint.type) ? dimensions::angle : dimensions::length;
                        checkParameter(object.id(), *constraint.parameter, expected,
                                       std::format("{} is driven by", constraint.id));
                    }
                }
            } else if (const auto* datumPlane = dynamic_cast<const DatumPlane*>(&object)) {
                const DatumPlaneDefinition& d = datumPlane->definition();
                checkPlaneReference(object.id(), d.base, "the base plane is");
                checkAxisReference(object.id(), d.axis, "the axis is");
                if (d.offsetParameter) {
                    checkParameter(object.id(), *d.offsetParameter, dimensions::length, "the offset is driven by");
                }
                if (d.angleParameter) {
                    checkParameter(object.id(), *d.angleParameter, dimensions::angle, "the angle is driven by");
                }
            } else if (const auto* datumAxis = dynamic_cast<const DatumAxis*>(&object)) {
                checkPlaneReference(object.id(), datumAxis->definition().first, "the first plane is");
                checkPlaneReference(object.id(), datumAxis->definition().second, "the second plane is");
            } else if (const auto* system = dynamic_cast<const CoordinateSystem*>(&object)) {
                const CoordinateSystemDefinition& d = system->definition();
                checkSystemReference(object.id(), d.base, "the base is");
                static constexpr std::array<std::string_view, 3> kAxes{"X", "Y", "Z"};
                for (std::size_t i = 0; i < 3; ++i) {
                    if (d.translationParameters[i]) {
                        checkParameter(object.id(), *d.translationParameters[i], dimensions::length,
                                       std::format("the {} translation is driven by", kAxes[i]));
                    }
                    if (d.rotationParameters[i]) {
                        checkParameter(object.id(), *d.rotationParameters[i], dimensions::angle,
                                       std::format("the {} rotation is driven by", kAxes[i]));
                    }
                }
            } else if (const auto* extrude = dynamic_cast<const ExtrudeFeature*>(&object)) {
                const ExtrudeDefinition& definition = extrude->definition();
                checkProfile(object.id(), definition.profile);
                if (definition.depthParameter) {
                    checkParameter(object.id(), *definition.depthParameter, dimensions::length,
                                   "the depth is driven by");
                }
            } else if (const auto* revolve = dynamic_cast<const RevolveFeature*>(&object)) {
                const RevolveDefinition& definition = revolve->definition();
                checkProfile(object.id(), definition.profile);
                if (definition.angleParameter) {
                    checkParameter(object.id(), *definition.angleParameter, dimensions::angle,
                                   "the angle is driven by");
                }
                const sketch::Entity* axis = revolveAxisEntity(*revolve);
                if (axis != nullptr && !std::holds_alternative<sketch::LineEntity>(axis->geometry)) {
                    add(ValidationCheck::DocumentConsistency, Severity::Error, object.id(),
                        std::format("{}: the axis is {}, which is {}, not a line", label(document_, object.id()),
                                    definition.axis.line, withArticle(sketch::toString(axis->type()))));
                }
            } else if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(&object)) {
                const ChamferDefinition& definition = chamfer->definition();
                if (definition.distanceParameter) {
                    checkParameter(object.id(), *definition.distanceParameter, dimensions::length,
                                   "the distance is driven by");
                }
            } else if (const auto* fillet = dynamic_cast<const FilletFeature*>(&object)) {
                const FilletDefinition& definition = fillet->definition();
                if (definition.radiusParameter) {
                    checkParameter(object.id(), *definition.radiusParameter, dimensions::length,
                                   "the radius is driven by");
                }
            } else if (const auto* hole = dynamic_cast<const HoleFeature*>(&object)) {
                const HoleDefinition& definition = hole->definition();
                const std::pair<const std::optional<ParameterId>*, std::string_view> drivers[] = {
                    {&definition.diameterParameter, "the diameter is driven by"},
                    {&definition.depthParameter, "the depth is driven by"},
                    {&definition.centerUParameter, "the centre's u coordinate is driven by"},
                    {&definition.centerVParameter, "the centre's v coordinate is driven by"},
                };
                for (const auto& [parameter, reference] : drivers) {
                    if (*parameter) {
                        checkParameter(object.id(), **parameter, dimensions::length, reference);
                    }
                }
            } else if (const auto* pattern = dynamic_cast<const LinearPatternFeature*>(&object)) {
                const LinearPatternDefinition& definition = pattern->definition();
                const auto checkDirection = [&](const PatternDirection& direction, std::string_view label) {
                    if (direction.countParameter) {
                        checkParameter(object.id(), *direction.countParameter, dimensions::dimensionless,
                                       std::format("{}'s count is driven by", label));
                    }
                    if (direction.spacingParameter) {
                        checkParameter(object.id(), *direction.spacingParameter, dimensions::length,
                                       std::format("{}'s spacing is driven by", label));
                    }
                };
                checkDirection(definition.first, "direction 1");
                if (definition.second) {
                    checkDirection(*definition.second, "direction 2");
                }
            } else if (const auto* circular = dynamic_cast<const CircularPatternFeature*>(&object)) {
                const CircularPatternDefinition& definition = circular->definition();
                if (definition.axis.reference) {
                    checkAxisReference(object.id(), *definition.axis.reference, "the axis is");
                }
                if (definition.countParameter) {
                    checkParameter(object.id(), *definition.countParameter, dimensions::dimensionless,
                                   "the count is driven by");
                }
                if (definition.angleParameter) {
                    checkParameter(object.id(), *definition.angleParameter, dimensions::angle,
                                   "the angle is driven by");
                }
            } else if (const auto* mirror = dynamic_cast<const MirrorFeature*>(&object)) {
                if (mirror->definition().plane.reference) {
                    checkPlaneReference(object.id(), *mirror->definition().plane.reference, "the plane is");
                }
                if (const auto& offset = mirror->definition().plane.offsetParameter) {
                    checkParameter(object.id(), *offset, dimensions::length, "the plane's offset is driven by");
                }
            } else if (const auto* sweep = dynamic_cast<const SweepFeature*>(&object)) {
                const SweepDefinition& definition = sweep->definition();
                checkProfile(object.id(), definition.profile);
                const ObjectId pathId{definition.path.sketch};
                if (document_.contains(pathId) && document_.findObjectAs<sketch::Sketch>(pathId) == nullptr) {
                    wrongKind(object.id(), "the path is in", pathId, kindOf(document_, pathId), "a sketch");
                }
                if (const sketch::Sketch* path = sweepPathSketch(*sweep)) {
                    for (const EntityId edge : definition.path.edges) {
                        const sketch::Entity* entity = path->findEntity(edge);
                        const bool pathKind = entity == nullptr || entity->type() == sketch::EntityType::Line ||
                                              entity->type() == sketch::EntityType::Arc ||
                                              entity->type() == sketch::EntityType::Circle;
                        if (!pathKind) {
                            add(ValidationCheck::DocumentConsistency, Severity::Error, object.id(),
                                std::format("{}: the path edge {} is {}, not a line, arc or circle",
                                            label(document_, object.id()), edge,
                                            withArticle(sketch::toString(entity->type()))));
                        }
                    }
                }
            } else if (const auto* loft = dynamic_cast<const LoftFeature*>(&object)) {
                const std::vector<LoftSection>& sections = loft->definition().sections;
                for (std::size_t i = 0; i < sections.size(); ++i) {
                    const ObjectId sketchId{sections[i].sketch};
                    if (document_.contains(sketchId) && document_.findObjectAs<sketch::Sketch>(sketchId) == nullptr) {
                        wrongKind(object.id(), std::format("section {} is", i + 1), sketchId,
                                  kindOf(document_, sketchId), "a sketch");
                    }
                    if (sections[i].offsetParameter) {
                        checkParameter(object.id(), *sections[i].offsetParameter, dimensions::length,
                                       std::format("section {}'s offset is driven by", i + 1));
                    }
                }
            }
            if (const auto* feature = dynamic_cast<const SolidFeature*>(&object)) {
                checkTarget(*feature);
            }
        }
    }

    /// The profile sketch of a revolve about a line, if the sketch exists.
    [[nodiscard]] const sketch::Sketch* revolveAxisSketch(const RevolveFeature& revolve) const {
        if (revolve.definition().axis.kind != RevolveAxisKind::Line) {
            return nullptr;
        }
        return document_.findObjectAs<sketch::Sketch>(ObjectId{revolve.definition().profile});
    }

    /// The axis entity of a revolve about a line, if it exists.
    [[nodiscard]] const sketch::Entity* revolveAxisEntity(const RevolveFeature& revolve) const {
        const sketch::Sketch* sketch = revolveAxisSketch(revolve);
        return sketch == nullptr ? nullptr : sketch->findEntity(revolve.definition().axis.line);
    }

    /// The path sketch of a sweep, if it exists.
    [[nodiscard]] const sketch::Sketch* sweepPathSketch(const SweepFeature& sweep) const {
        return document_.findObjectAs<sketch::Sketch>(ObjectId{sweep.definition().path.sketch});
    }

    /// A solid feature's target must be another feature that has a body.
    void checkTarget(const SolidFeature& feature) {
        const auto target = feature.target();
        if (!target) {
            return;
        }
        const ObjectId id{*target};
        if (id != feature.id() && document_.contains(id) && document_.findObjectAs<SolidFeature>(id) == nullptr) {
            // A pattern's or mirror's consumed feature is its source.
            const bool pattern = dynamic_cast<const LinearPatternFeature*>(&feature) != nullptr ||
                                 dynamic_cast<const CircularPatternFeature*>(&feature) != nullptr ||
                                 dynamic_cast<const MirrorFeature*>(&feature) != nullptr;
            wrongKind(feature.id(), pattern ? "the source is" : "the target is", id, kindOf(document_, id),
                      "a feature with a body");
        }
    }

    void checkMissingReferences(const DocumentGraph& graph) {
        for (const MissingReference& reference : graph.missing) {
            add(ValidationCheck::MissingReferences, Severity::Error, reference.dependent,
                std::format("{} references {}, which does not exist", label(document_, reference.dependent),
                            reference.missing));
        }
        // Unknown names in expressions (every one of them), unless the
        // expression already failed the consistency check.
        const std::set<ObjectId> inconsistent = failedItems_;
        for (const UnresolvedExpression& unresolved : graph.unresolved) {
            if (unresolved.error.code == ErrorCode::NotFound &&
                !inconsistent.contains(ObjectId{unresolved.parameter})) {
                expressionIssue(ValidationCheck::MissingReferences, unresolved);
            }
        }
        // References into sketches are not graph edges: revolve axis lines
        // and sweep path edges.
        for (const DocumentObject& object : document_.objects()) {
            if (failedItems_.contains(object.id())) {
                continue;
            }
            if (const auto* revolve = dynamic_cast<const RevolveFeature*>(&object)) {
                const sketch::Sketch* sketch = revolveAxisSketch(*revolve);
                if (sketch != nullptr && sketch->findEntity(revolve->definition().axis.line) == nullptr) {
                    add(ValidationCheck::MissingReferences, Severity::Error, object.id(),
                        std::format("{}: the axis line {} does not exist in {}", label(document_, object.id()),
                                    revolve->definition().axis.line, label(document_, sketch->id())));
                }
            } else if (const auto* sweep = dynamic_cast<const SweepFeature*>(&object)) {
                const sketch::Sketch* sketch = sweepPathSketch(*sweep);
                if (sketch == nullptr) {
                    continue;
                }
                for (const EntityId edge : sweep->definition().path.edges) {
                    if (sketch->findEntity(edge) == nullptr) {
                        add(ValidationCheck::MissingReferences, Severity::Error, object.id(),
                            std::format("{}: the path edge {} does not exist in {}", label(document_, object.id()),
                                        edge, label(document_, sketch->id())));
                    }
                }
            }
        }
    }

    void checkCycles(const DocumentGraph& graph) {
        for (const auto& cycle : graph.graph.cycles()) {
            std::string members;
            for (const ObjectId id : cycle) {
                members += members.empty() ? "" : ", ";
                members += label(document_, id);
            }
            add(ValidationCheck::DependencyCycles, Severity::Error, cycle.front(),
                std::format("dependency cycle: {}", members));
            failedItems_.insert(cycle.begin(), cycle.end());
        }
    }

    /// True if a parameter @p id depends on was not evaluated or failed, so
    /// its value is not known.
    [[nodiscard]] bool usesUnevaluatedParameter(const DocumentGraph& graph, ObjectId id) const {
        const auto unevaluated = [&](ObjectId input) {
            const auto parameter = document_.asParameter(input);
            if (!parameter) {
                return false;
            }
            const bool inCycle = std::ranges::any_of(evaluation_.cycles, [&](const auto& cycle) {
                return std::ranges::contains(cycle, *parameter);
            });
            return inCycle || evaluation_.failed.contains(*parameter) ||
                   std::ranges::contains(evaluation_.blocked, *parameter);
        };
        return std::ranges::any_of(graph.graph.dependenciesOf(id), unevaluated);
    }

    void checkSketches(const DocumentGraph& graph) {
        for (const DocumentObject& object : document_.objects()) {
            const auto* original = dynamic_cast<const sketch::Sketch*>(&object);
            if (original == nullptr || failedItems_.contains(object.id())) {
                continue;
            }
            // Regeneration reports a sketch whose driving values are unknown
            // as blocked.
            if (usesUnevaluatedParameter(graph, object.id())) {
                continue;
            }
            sketch::Sketch copy = *original;
            // Driving values as regeneration computes them: with the
            // parameter expressions evaluated.
            if (auto applied = sketch::applyDrivingParameters(copy, evaluated_.parameters()); !applied) {
                add(ValidationCheck::SketchConstraints, Severity::Error, object.id(),
                    std::format("{}: {}", label(document_, object.id()), applied.error().message));
                continue;
            }
            const sketch::SolveResult result = sketch::analyze(copy);
            switch (result.status) {
            case sketch::SolveStatus::FullyConstrained:
                break;
            case sketch::SolveStatus::UnderConstrained:
                add(ValidationCheck::SketchConstraints, Severity::Warning, object.id(),
                    std::format("{} is under-constrained: {} degree(s) of freedom", label(document_, object.id()),
                                result.degreesOfFreedom));
                break;
            case sketch::SolveStatus::OverConstrained:
            case sketch::SolveStatus::Inconsistent:
            case sketch::SolveStatus::SolverFailure:
                add(ValidationCheck::SketchConstraints, Severity::Error, object.id(),
                    std::format("{} is {}: {}", label(document_, object.id()), sketch::toString(result.status),
                                result.message));
                break;
            }
        }
    }

    void checkBody(ObjectId feature, const geometry::Body& body, BodySummary* summary) {
        const std::string name = label(document_, feature);
        if (body.isEmpty()) {
            add(ValidationCheck::Geometry, Severity::Error, feature, std::format("{} produced an empty body", name));
            return;
        }
        const geometry::TopologySummary topology = body.topology();
        const bool valid = body.isValid();
        auto properties = body.massProperties();
        auto box = body.boundingBox();
        if (topology.solids == 0) {
            add(ValidationCheck::Geometry, Severity::Error, feature, std::format("{}: the body has no solid", name));
        } else if (!valid) {
            add(ValidationCheck::Geometry, Severity::Error, feature,
                std::format("{}: the body fails the kernel's validity check", name));
        } else if (!properties) {
            add(ValidationCheck::Geometry, Severity::Error, feature,
                std::format("{}: {}", name, properties.error().message));
        } else if (!(properties->volume > Volume{})) {
            add(ValidationCheck::Geometry, Severity::Error, feature,
                std::format("{}: the body's volume is not positive ({})", name,
                            toString(properties->volume, units::mm3)));
        }
        if (summary != nullptr) {
            summary->valid = valid && topology.solids > 0 && properties && properties->volume > Volume{};
            summary->topology = topology;
            if (properties) {
                summary->properties = *properties;
            }
            if (box) {
                summary->boundingBox = *box;
            }
        }
    }

    void checkRegenerationAndGeometry() {
        Document copy = document_.clone();
        Regenerator regenerator;
        auto regeneration = regenerator.regenerate(copy);
        if (!regeneration) {
            add(ValidationCheck::FeatureRegeneration, Severity::Error, std::nullopt, regeneration.error().message);
            return;
        }
        report_.regenerated = regeneration->regenerated.size();
        // Failures already explained by an earlier check are not repeated.
        for (const auto& [id, error] : regeneration->errors) {
            if (!failedItems_.contains(id)) {
                const std::string_view what = document_.asParameter(id) ? "evaluate" : "regenerate";
                add(ValidationCheck::FeatureRegeneration, Severity::Error, id,
                    std::format("{} failed to {}: {}", label(document_, id), what, error.message));
            }
        }
        for (const ObjectId id : regeneration->blocked) {
            if (!failedItems_.contains(id)) {
                const std::string_view what = document_.asParameter(id) ? "evaluated" : "regenerated";
                add(ValidationCheck::FeatureRegeneration, Severity::Error, id,
                    std::format("{} was not {} because an item it depends on failed", label(document_, id), what));
            }
        }

        // Every body the model produced, including intermediate ones.
        const std::vector<ObjectId> results = resultFeatures(copy);
        for (const DocumentObject& object : copy.objects()) {
            const geometry::Body* body = regenerator.body(object.id());
            if (body == nullptr) {
                continue;
            }
            BodySummary* summary = nullptr;
            if (std::ranges::contains(results, object.id())) {
                report_.bodies.push_back({.feature = object.id(), .name = object.name()});
                summary = &report_.bodies.back();
            }
            checkBody(object.id(), *body, summary);
        }
    }

    const Document& document_;
    /// A copy with its parameter expressions evaluated.
    Document evaluated_;
    ParameterEvaluationReport evaluation_;
    ValidationReport report_;
    std::set<ObjectId> failedItems_;
};

} // namespace

std::string_view toString(ValidationCheck check) noexcept {
    switch (check) {
    case ValidationCheck::DocumentConsistency:
        return "document consistency";
    case ValidationCheck::MissingReferences:
        return "missing references";
    case ValidationCheck::DependencyCycles:
        return "dependency cycles";
    case ValidationCheck::SketchConstraints:
        return "sketch constraints";
    case ValidationCheck::FeatureRegeneration:
        return "feature regeneration";
    case ValidationCheck::Geometry:
        return "geometry";
    }
    return "unknown";
}

std::string_view toString(Severity severity) noexcept {
    switch (severity) {
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "unknown";
}

std::size_t ValidationReport::count(Severity severity) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(issues, [&](const ValidationIssue& issue) { return issue.severity == severity; }));
}

std::size_t ValidationReport::count(ValidationCheck check, Severity severity) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(issues, [&](const ValidationIssue& issue) {
        return issue.check == check && issue.severity == severity;
    }));
}

ValidationReport validateDocument(const Document& document) {
    return Checker(document).run();
}

} // namespace bettercad::features
