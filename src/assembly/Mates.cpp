#include <bettercad/assembly/Mates.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace bettercad::assembly {
namespace {

/// How deep the search for "does this part include that feature" may go.
/// The same limit datum resolution uses for the same reason: a bound is
/// cheaper than trusting the graph to be acyclic here.
constexpr int kMaxDepth = 64;

/// Whether @p feature is @p part, or something @p part is built from.
///
/// A part is usually several features -- extrude, then fillet -- and the
/// body's faces are named by whichever generated them, so a face of the part
/// may be named by an ancestor rather than by the part feature itself. This
/// walks the dependencies of the part looking for that ancestor.
[[nodiscard]] bool partIncludes(const Document& document, ObjectId part, ObjectId feature) {
    if (part == feature) {
        return true;
    }
    std::set<ObjectId> seen{part};
    std::vector<std::pair<ObjectId, int>> pending{{part, 0}};
    while (!pending.empty()) {
        const auto [id, depth] = pending.back();
        pending.pop_back();
        if (depth >= kMaxDepth) {
            continue;
        }
        const DocumentObject* object = document.findObject(id);
        if (object == nullptr) {
            continue;
        }
        for (const ObjectId next : object->dependencies()) {
            if (next == feature) {
                return true;
            }
            if (seen.insert(next).second) {
                pending.emplace_back(next, depth + 1);
            }
        }
    }
    return false;
}

/// The feature a target names a face of, if it names one at all. A plane
/// reference carries a face selector for exactly the same reason a face
/// target does, so both are checked.
[[nodiscard]] std::optional<ObjectId> namedFaceFeature(const MateTarget& target) {
    if (target.kind == MateTargetKind::Face && target.face) {
        return target.face->feature;
    }
    if (target.kind == MateTargetKind::Plane && target.plane && target.plane->face && target.plane->object) {
        return *target.plane->object;
    }
    return std::nullopt;
}

/// Checks one target against the document.
[[nodiscard]] Result<void> checkTarget(const Document& document, const MateTarget& target) {
    const Component* component = findComponent(document, target.component);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a mate cannot use {}, which is not a component of this document",
                                     target.component));
    }
    for (const ObjectId id : referencedObjects(target)) {
        if (document.findObject(id) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("a mate target names {}, which is not an object of this document", id));
        }
    }
    // A face belongs to the part that generates it. Using a face of another
    // component's part is a modelling mistake, and is refused here rather
    // than left to resolve into the wrong geometry.
    if (const auto feature = namedFaceFeature(target)) {
        const ObjectReference& part = component->definition().part;
        if (!isInternal(part)) {
            // The part is in another document, so its features are not this
            // document's to walk. Resolving that needs the external body,
            // which is P13-SOLVE-001's problem, not validation's.
            return {};
        }
        if (!partIncludes(document, part.object, *feature)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a mate target on {} names a face of {}, which is not part of {}",
                                         target.component, *feature, part.object));
        }
    }
    return {};
}

} // namespace

Result<void> checkMate(const Document& document, const MateDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return valid;
    }
    if (definition.component.isValid()) {
        if (findComponent(document, definition.component) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("a fixed mate cannot hold {}, which is not a component of this document",
                                         definition.component));
        }
        return {};
    }
    if (auto valid = checkTarget(document, *definition.a); !valid) {
        return valid;
    }
    return checkTarget(document, *definition.b);
}

Result<MateId> createMate(Document& document, std::string name, const MateDefinition& definition) {
    // Checked before anything is built, so a rejected mate consumes no ID and
    // leaves the document untouched.
    if (auto valid = checkMate(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto mate = Mate::create(std::move(name), definition);
    if (!mate) {
        return std::unexpected(mate.error());
    }
    auto id = document.addObject(std::move(*mate));
    if (!id) {
        return std::unexpected(id.error());
    }
    return MateId::fromValue(id->value());
}

Result<bool> setMateDefinition(Document& document, MateId id, const MateDefinition& definition) {
    if (findMate(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no mate {}", id));
    }
    if (auto valid = checkMate(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto changed = document.modifyObject<Mate>(
        id, [&](Mate& mate) { return mate.setDefinition(definition).value_or(false); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return *changed;
}

const Mate* findMate(const Document& document, MateId id) noexcept {
    return document.findObjectAs<Mate>(id);
}

std::vector<MateId> mates(const Document& document) {
    std::vector<MateId> found;
    // objects() is ascending by ID, so the result is ordered without sorting.
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Mate*>(&object) != nullptr) {
            found.push_back(MateId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::vector<MateId> matesOf(const Document& document, ComponentId component) {
    std::vector<MateId> found;
    for (const MateId id : mates(document)) {
        const Mate* mate = findMate(document, id);
        if (mate == nullptr) {
            continue;
        }
        const MateDefinition& d = mate->definition();
        const bool names = d.component == component || (d.a && d.a->component == component) ||
                           (d.b && d.b->component == component);
        if (names) {
            found.push_back(id);
        }
    }
    return found;
}

Result<void> removeMate(Document& document, MateId id) {
    if (findMate(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no mate {}", id));
    }
    auto removed = document.removeObject(id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

std::vector<UnresolvedMate> unresolvedMates(const Document& document) {
    std::vector<UnresolvedMate> found;
    for (const MateId id : mates(document)) {
        const Mate* mate = findMate(document, id);
        if (mate == nullptr) {
            continue;
        }
        std::vector<ObjectId> missing;
        for (const ObjectId referenced : mate->dependencies()) {
            if (document.findObject(referenced) == nullptr) {
                missing.push_back(referenced);
            }
        }
        if (!missing.empty()) {
            found.push_back({.mate = id, .missing = std::move(missing)});
        }
    }
    return found;
}

} // namespace bettercad::assembly
