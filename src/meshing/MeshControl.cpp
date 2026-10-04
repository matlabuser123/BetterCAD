// The canonical meshing intent, as a document object (P16-CMD-001, ADR-030).

#include <bettercad/meshing/MeshControl.hpp>

#include <algorithm>
#include <format>
#include <set>
#include <utility>

namespace bettercad::meshing {
namespace {

/// The DocumentObject constructor takes a name; everything else is ours.
[[nodiscard]] Result<void> requireBody(ObjectId body) {
    if (!body.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "mesh control: the invalid handle cannot name the body to mesh");
    }
    return Result<void>{};
}

} // namespace

Result<void> validate(const MeshControlDefinition& definition) {
    if (const Result<void> body = requireBody(definition.body); !body.has_value()) {
        return body;
    }

    // DELEGATED, NOT RESTATED. P16-SIZE-001 owns what a valid sizing control
    // is -- non-positive, non-finite, malformed selector, duplicate face --
    // and P16-QUALITY-001 owns what a valid threshold policy is. A copy of
    // either here would be a second policy that eventually disagrees.
    // P16-SIZE-001's validator returns a REPORT, not a yes or no, and it is
    // carried through rather than flattened: the first issue's own message
    // names the kind, the offending control's position in the request and the
    // face, which a bare "invalid sizing" would throw away.
    if (const SizingValidationReport sizing = validate(definition.mesh.sizing); !sizing.valid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("mesh control: {}", sizing.issues.front().message));
    }
    if (const Result<void> thresholds = validate(definition.quality); !thresholds.has_value()) {
        return thresholds;
    }

    std::set<BoundarySetId::ValueType> seen;
    for (const NamedBoundarySet& set : definition.boundarySets) {
        if (const Result<void> checked = validate(set); !checked.has_value()) {
            return checked;
        }
        if (!seen.insert(set.id.value()).second) {
            // REFUSED, for the same reason P16-SIZE refuses two controls on one
            // face: two sets with one identity is a modelling mistake, and
            // resolving it by picking one would hide it.
            return makeError(ErrorCode::AlreadyExists,
                             std::format("mesh control: two boundary sets share the identity {}",
                                         set.id.value()));
        }
    }
    return Result<void>{};
}

Result<std::unique_ptr<MeshControl>> MeshControl::create(std::string name,
                                                         MeshControlDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    return std::unique_ptr<MeshControl>{new MeshControl{std::move(name), std::move(definition)}};
}

MeshControl::MeshControl(std::string name, MeshControlDefinition definition)
    : DocumentObject(std::move(name)), definition_(std::move(definition)) {}

std::unique_ptr<DocumentObject> MeshControl::clone() const {
    // THE COPY CONSTRUCTOR, AND IT HAS TO BE: a clone must carry the ID.
    //
    // Found by probe, and it is not written down anywhere obvious.
    // AddObjectCommand::undo removes the object and keeps it; redo then calls
    // `document.insertObject(removed_->clone())`, and insertObject REQUIRES a
    // valid ID -- "object '{}' has no ID; use addObject()". A clone built from
    // (name, definition) is a fresh object with no ID, so redo failed with
    // that message and an added control could never come back.
    //
    // DocumentObject's copy constructor is defaulted and copies id_, which is
    // why Material::clone does `new Material(*this)`. The base's clone()
    // declaration says nothing about the ID, so the requirement is only
    // discoverable by reading the command that depends on it.
    return std::unique_ptr<DocumentObject>(new MeshControl(*this));
}

bool MeshControl::contentEquals(const DocumentObject& other) const {
    const auto* control = dynamic_cast<const MeshControl*>(&other);
    // SEMANTIC EQUALITY OF THE INTENT, which is what "undo restored exactly
    // the previous state" has to mean. Everything in the definition is a value
    // type with a defaulted operator==, so this compares the body, the
    // discretisation, the sizing controls, the quality policy and the boundary
    // sets -- and nothing derived, because there is nothing derived in here.
    return control != nullptr && definition_ == control->definition_;
}

std::vector<ObjectId> MeshControl::dependencies() const {
    if (!definition_.body.isValid()) {
        return {};
    }
    return {definition_.body};
}

Result<bool> MeshControl::setDefinition(MeshControlDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (definition == definition_) {
        // NO EFFECTIVE CHANGE, so the document does not bump a revision and
        // nothing downstream is invalidated. Setting 5 mm to 5 mm is not an
        // edit, and treating it as one would stale a mesh for nothing.
        return false;
    }
    definition_ = std::move(definition);
    return true;
}

const LocalMeshSizing* MeshControl::localSizing(const FaceName& face) const noexcept {
    const auto found = std::ranges::find_if(
        definition_.mesh.sizing.local,
        [&face](const LocalMeshSizing& local) { return local.face == face; });
    return found == definition_.mesh.sizing.local.end() ? nullptr : &*found;
}

std::vector<LocalMeshSizing> MeshControl::orderedLocalSizing() const {
    std::vector<LocalMeshSizing> ordered(definition_.mesh.sizing.local.begin(),
                                         definition_.mesh.sizing.local.end());
    // ASCENDING FaceName, which FaceName's own operator<=> defines. The stored
    // order carries no meaning (P16-SIZE-001), so an enumeration that exposed
    // it would make a panel's or a file's output depend on the order of past
    // edits.
    std::ranges::sort(ordered, [](const LocalMeshSizing& a, const LocalMeshSizing& b) {
        return a.face < b.face;
    });
    return ordered;
}

const NamedBoundarySet* MeshControl::boundarySet(BoundarySetId id) const noexcept {
    const auto found = std::ranges::find_if(
        definition_.boundarySets,
        [id](const NamedBoundarySet& set) { return set.id == id; });
    return found == definition_.boundarySets.end() ? nullptr : &*found;
}

std::vector<NamedBoundarySet> MeshControl::orderedBoundarySets() const {
    std::vector<NamedBoundarySet> ordered(definition_.boundarySets.begin(),
                                          definition_.boundarySets.end());
    std::ranges::sort(ordered, [](const NamedBoundarySet& a, const NamedBoundarySet& b) {
        return a.id.value() < b.id.value();
    });
    return ordered;
}

} // namespace bettercad::meshing
