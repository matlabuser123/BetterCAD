#include <bettercad/features/ChamferFeature.hpp>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace bettercad::features {

std::vector<geometry::EdgeSignature> chamferCurves(const ChamferDefinition& definition) {
    std::vector<geometry::EdgeSignature> curves;
    curves.reserve(definition.edges.size());
    for (const ChamferEdge& edge : definition.edges) {
        curves.push_back(edge.curve);
    }
    return curves;
}

std::vector<ChamferEdgeId> chamferEdgeIds(const ChamferDefinition& definition) {
    std::vector<ChamferEdgeId> ids;
    ids.reserve(definition.edges.size());
    for (const ChamferEdge& edge : definition.edges) {
        ids.push_back(edge.id);
    }
    return ids;
}

Result<void> validate(const ChamferDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a chamfer needs a target feature");
    }
    if (definition.distanceParameter && !definition.distanceParameter->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "the distance parameter ID must be valid");
    }
    using geometry::ChamferMode;
    if (definition.mode != ChamferMode::TwoDistance && definition.distance2 != Length{}) {
        return makeError(ErrorCode::InvalidArgument, "only a chamfer by two distances takes a second distance");
    }
    if (definition.mode != ChamferMode::DistanceAngle && definition.angle != Angle{}) {
        return makeError(ErrorCode::InvalidArgument, "only a chamfer by distance and angle takes an angle");
    }
    // No two selections may share an identity: a reference names one face, so
    // an id that matched two of them would name two.
    for (std::size_t i = 0; i < definition.edges.size(); ++i) {
        const ChamferEdgeId id = definition.edges[i].id;
        if (!id.isValid()) {
            continue; // not yet identified; ChamferFeature allocates one
        }
        for (std::size_t j = i + 1; j < definition.edges.size(); ++j) {
            if (definition.edges[j].id == id) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("two of this chamfer's edges are both {}", id));
            }
        }
    }
    // The rest is the geometry request's contract. A driven distance is
    // checked when the parameter's value is known, at regeneration.
    const geometry::ChamferRequest request{
        .edges = chamferCurves(definition),
        .mode = definition.mode,
        .distance = definition.distanceParameter ? Length::fromSi(1.0) : definition.distance,
        .distance2 = definition.distance2,
        .angle = definition.angle,
        .referenceSide = definition.referenceSide,
    };
    return geometry::validate(request);
}

ChamferFeature::ChamferFeature(std::string name, const ChamferDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<ChamferDefinition> ChamferFeature::identify(const ChamferDefinition& definition,
                                                   const ChamferDefinition* existing) {
    // TWO PASSES, so a rejected definition leaves nothing behind. The first
    // pass can fail and allocates nothing; the second cannot fail. Otherwise a
    // definition rejected on its last edge would already have consumed ids.
    if (existing != nullptr) {
        for (const ChamferEdge& edge : definition.edges) {
            if (!edge.id.isValid()) {
                continue;
            }
            // An edit may name any id THIS chamfer has allocated, including one
            // whose selection has since been removed. That is what undo and
            // target recovery are: restoring a selection under the identity it
            // had is supposed to bring its references back, and a rule that
            // allowed only ids currently present would make undoing a deletion
            // impossible.
            //
            // What it may NOT do is invent an identity above the high-water
            // mark. Those have never been handed out, so nothing can be
            // referring to them yet, and allowing one would let a caller
            // reserve an id that a later selection would then be given too.
            //
            // Silent rebinding is prevented earlier than this: a NEW selection
            // carries no id -- `ChamferEdge` will not convert from a bare curve
            // implicitly -- so ordinary editing can only ever allocate a fresh
            // identity. Deleting a selection and adding one with the same curve
            // gives it a new id, and references to the old one stay unresolved.
            if (edge.id.value() > edgeIds_.lastValue()) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("this chamfer has never had an edge {}, so an edge "
                                             "cannot be given that identity; leave it unset for a "
                                             "new one",
                                             edge.id));
            }
        }
    }
    ChamferDefinition identified = definition;
    // EVERY id this definition already carries is reserved BEFORE anything is
    // allocated. Reserving as we went would let {unset, id 1} hand the first
    // edge id 1 and collide with the second.
    //
    // create() and restore() reserve here because there is no earlier
    // definition to check against; a fresh feature also carries a fresh
    // ObjectId, so no reference stored against an older feature can name it.
    // An edit reserves nothing: every id it carries was allocated by this same
    // allocator already, so the floor is at or above all of them.
    if (existing == nullptr) {
        for (const ChamferEdge& edge : identified.edges) {
            if (edge.id.isValid()) {
                edgeIds_.reserveThrough(edge.id.value());
            }
        }
    }
    for (ChamferEdge& edge : identified.edges) {
        if (!edge.id.isValid()) {
            edge.id = edgeIds_.allocate<ChamferEdgeId>();
        }
    }
    return identified;
}

Result<std::unique_ptr<ChamferFeature>> ChamferFeature::create(std::string name,
                                                               const ChamferDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto feature = std::unique_ptr<ChamferFeature>(new ChamferFeature(std::move(name), definition));
    auto identified = feature->identify(definition, nullptr);
    if (!identified) {
        return std::unexpected(identified.error());
    }
    feature->definition_ = std::move(*identified);
    return feature;
}

Result<std::unique_ptr<ChamferFeature>> ChamferFeature::restore(std::string name,
                                                                const ChamferDefinition& definition,
                                                                std::uint64_t lastEdgeId) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto feature = std::unique_ptr<ChamferFeature>(new ChamferFeature(std::move(name), definition));
    // The floor goes in FIRST, so a file that carries ids keeps them and a
    // file written before chamfer edges had ids gets them allocated above
    // whatever the file already used -- never a value it had retired.
    feature->edgeIds_.reserveThrough(lastEdgeId);
    auto identified = feature->identify(definition, nullptr);
    if (!identified) {
        return std::unexpected(identified.error());
    }
    feature->definition_ = std::move(*identified);
    return feature;
}

std::unique_ptr<DocumentObject> ChamferFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new ChamferFeature(*this));
}

bool ChamferFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const ChamferFeature&>(other).definition_;
}

std::vector<ObjectId> ChamferFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    if (definition_.distanceParameter) {
        result.push_back(ObjectId{*definition_.distanceParameter});
    }
    return result;
}

Result<bool> ChamferFeature::setDefinition(const ChamferDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto identified = identify(definition, &definition_);
    if (!identified) {
        return std::unexpected(identified.error());
    }
    if (*identified == definition_) {
        return false;
    }
    definition_ = std::move(*identified);
    return true;
}

} // namespace bettercad::features
