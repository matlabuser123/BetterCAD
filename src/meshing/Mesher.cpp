// The derived meshes, held by a service and keyed by their control
// (P16-CMD-001, ADR-030).

#include <bettercad/meshing/Mesher.hpp>

#include <bettercad/meshing/MeshingCommands.hpp>

#include <format>
#include <utility>

namespace bettercad::meshing {

std::string_view toString(MeshCurrency currency) noexcept {
    switch (currency) {
    case MeshCurrency::NoMesh:
        return "no mesh";
    case MeshCurrency::Current:
        return "current";
    case MeshCurrency::StaleIntent:
        return "stale: the meshing intent changed";
    case MeshCurrency::StaleGeometry:
        return "stale: the geometry changed";
    case MeshCurrency::GenerationFailed:
        return "generation failed";
    }
    return "unknown";
}

Result<const VolumeMesh*> Mesher::generate(const Document& document,
                                           const features::Regenerator& regenerator,
                                           MeshControlId control) {
    const MeshControl* intent = findMeshControl(document, control);
    if (intent == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("there is no meshing control {}", control.value()));
    }
    const MeshControlDefinition& definition = intent->definition();

    // FROM THE INTENT THE DOCUMENT HOLDS NOW, which is what makes "undo then
    // generate" produce a mesh of the restored settings rather than of the
    // settings that were current when some earlier mesh was built. Nothing
    // here reads a cached control.
    Result<VolumeMesh> mesh =
        volumeMeshFor(document, regenerator, definition.body, definition.mesh);
    if (!mesh.has_value()) {
        // THE OLD MESH IS KEPT. A failed regeneration must not destroy what
        // the user could still inspect, and it must not touch the canonical
        // intent or the command history either.
        failures_.insert_or_assign(control.value(), mesh.error());
        return std::unexpected(mesh.error());
    }

    Result<GeometryMeshMap> map =
        geometryMeshMapFor(document, regenerator, definition.body, *mesh);
    if (!map.has_value()) {
        failures_.insert_or_assign(control.value(), map.error());
        return std::unexpected(map.error());
    }

    Held held{.mesh = std::move(*mesh),
              .map = std::move(*map),
              .quality = MeshQualityReport{},
              .body = definition.body,
              .controls = definition.mesh,
              .thresholds = definition.quality,
              .revision = geometryRevision(document, definition.body)};
    held.quality = evaluateMeshQuality(held.mesh.mesh(), definition.quality);

    failures_.erase(control.value());
    const auto [entry, inserted] = held_.insert_or_assign(control.value(), std::move(held));
    (void)inserted;
    return &entry->second.mesh;
}

MeshCurrency Mesher::currency(const Document& document, MeshControlId control) const {
    const auto failure = failures_.find(control.value());
    if (failure != failures_.end()) {
        // A FAILED ATTEMPT IS ITS OWN STATE, whether or not an older mesh
        // survives it. Reporting such a mesh as merely Stale would hide that
        // the attempt to replace it failed; reporting it Current would be a
        // lie.
        return MeshCurrency::GenerationFailed;
    }
    const auto entry = held_.find(control.value());
    if (entry == held_.end()) {
        return MeshCurrency::NoMesh;
    }

    const MeshControl* intent = findMeshControl(document, control);
    if (intent == nullptr) {
        // The control is gone, so nothing in the document asks for this mesh.
        // Its intent cannot be compared with anything.
        return MeshCurrency::StaleIntent;
    }
    const MeshControlDefinition& definition = intent->definition();

    if (!(definition.body == entry->second.body)) {
        return MeshCurrency::StaleIntent;
    }
    // BY VALUE, AND ONLY THE PART THAT CAN CHANGE A TETRAHEDRON. The control's
    // own revision would also have moved for a boundary-set rename or a
    // quality-threshold edit, neither of which can change the mesh -- and
    // remeshing for those is precisely what the brief forbids.
    if (!(definition.mesh == entry->second.controls)) {
        return MeshCurrency::StaleIntent;
    }
    if (!(geometryRevision(document, definition.body) == entry->second.revision)) {
        return MeshCurrency::StaleGeometry;
    }
    return MeshCurrency::Current;
}

const VolumeMesh* Mesher::mesh(MeshControlId control) const noexcept {
    const auto entry = held_.find(control.value());
    return entry == held_.end() ? nullptr : &entry->second.mesh;
}

const GeometryMeshMap* Mesher::map(MeshControlId control) const noexcept {
    const auto entry = held_.find(control.value());
    return entry == held_.end() ? nullptr : &entry->second.map;
}

const MeshQualityReport* Mesher::quality(MeshControlId control) const noexcept {
    const auto entry = held_.find(control.value());
    return entry == held_.end() ? nullptr : &entry->second.quality;
}

bool Mesher::qualityDescribesCurrentPolicy(const Document& document,
                                           MeshControlId control) const {
    const auto entry = held_.find(control.value());
    if (entry == held_.end()) {
        return false;
    }
    const MeshControl* intent = findMeshControl(document, control);
    // A SEPARATE QUESTION FROM THE MESH'S CURRENCY. Editing a threshold
    // changes how the same elements are classified without making any of them
    // wrong, so the mesh stays Current and only the report goes out of date.
    return intent != nullptr && intent->definition().quality == entry->second.thresholds;
}

const Error* Mesher::lastFailure(MeshControlId control) const noexcept {
    const auto failure = failures_.find(control.value());
    return failure == failures_.end() ? nullptr : &failure->second;
}

void Mesher::forget(MeshControlId control) noexcept {
    held_.erase(control.value());
    failures_.erase(control.value());
}

void Mesher::clear() noexcept {
    held_.clear();
    failures_.clear();
}

} // namespace bettercad::meshing
