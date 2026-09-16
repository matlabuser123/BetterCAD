#include "ReferenceModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace bettercad::reference {

namespace {

std::array<double, 3> millimetres(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

double relative(double a, double b) {
    const double scale = std::max(std::abs(a), std::abs(b));
    return scale == 0.0 ? 0.0 : std::abs(a - b) / scale;
}

double largest(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    double worst = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

} // namespace

Result<BodyFingerprint> bodyFingerprint(const geometry::Body& body, ObjectId feature, std::string name) {
    const auto properties = body.massProperties();
    if (!properties) {
        return std::unexpected(properties.error());
    }
    const auto box = body.boundingBox();
    if (!box) {
        return std::unexpected(box.error());
    }
    return BodyFingerprint{.feature = feature,
                           .name = std::move(name),
                           .valid = body.isValid(),
                           .topology = body.topology(),
                           .volumeMm3 = properties->volume.in(units::mm3),
                           .areaMm2 = properties->surfaceArea.in(units::mm2),
                           .centroidMm = millimetres(properties->centerOfMass),
                           .minMm = millimetres(box->min),
                           .maxMm = millimetres(box->max)};
}

Result<ModelFingerprint> fingerprint(const Document& document, const features::Regenerator& regenerator) {
    ModelFingerprint result;
    for (const Parameter& parameter : document.parameters().all()) {
        result.items.push_back({ObjectId{parameter.id()}, "parameter", parameter.name()});
    }
    for (const DocumentObject& object : document.objects()) {
        result.items.push_back({object.id(), std::string{object.typeName()}, object.name()});
        if (dynamic_cast<const features::SolidFeature*>(&object) != nullptr) {
            ++result.featureCount;
        }
    }
    std::ranges::sort(result.items, {}, &ItemFingerprint::id);

    for (const ObjectId feature : features::resultFeatures(document)) {
        const geometry::Body* body = regenerator.body(feature);
        const auto name = document.nameOf(feature);
        if (body == nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{}: result feature {} has no body", document.name(), feature));
        }
        auto summary = bodyFingerprint(*body, feature, name ? std::string{*name} : std::string{});
        if (!summary) {
            return std::unexpected(summary.error());
        }
        result.bodies.push_back(std::move(*summary));
    }
    return result;
}

FingerprintDifference compare(const ModelFingerprint& a, const ModelFingerprint& b) {
    FingerprintDifference difference;
    difference.sameStructure =
        a.items == b.items && a.featureCount == b.featureCount && a.bodies.size() == b.bodies.size();
    for (std::size_t i = 0; difference.sameStructure && i < a.bodies.size(); ++i) {
        const BodyFingerprint& x = a.bodies[i];
        const BodyFingerprint& y = b.bodies[i];
        difference.sameStructure = x.feature == y.feature && x.name == y.name && x.valid == y.valid &&
                                   x.topology == y.topology;
        difference.volumeAreaRelative =
            std::max({difference.volumeAreaRelative, relative(x.volumeMm3, y.volumeMm3), relative(x.areaMm2, y.areaMm2)});
        difference.positionMm = std::max({difference.positionMm, largest(x.centroidMm, y.centroidMm),
                                          largest(x.minMm, y.minMm), largest(x.maxMm, y.maxMm)});
    }
    return difference;
}

std::string toText(const ModelFingerprint& fingerprint) {
    std::string text;
    for (const ItemFingerprint& item : fingerprint.items) {
        text += std::format("item {} {} {}\n", item.id.value(), item.kind, item.name);
    }
    text += std::format("features {}\nbodies {}\n", fingerprint.featureCount, fingerprint.bodies.size());
    for (const BodyFingerprint& body : fingerprint.bodies) {
        const auto& t = body.topology;
        text += std::format("body {} {} valid={} solids={} shells={} faces={} edges={} vertices={}\n",
                            body.feature.value(), body.name, body.valid ? 1 : 0, t.solids, t.shells, t.faces, t.edges,
                            t.vertices);
        text += std::format("  volume_mm3 {:.17g}\n  area_mm2 {:.17g}\n", body.volumeMm3, body.areaMm2);
        text += std::format("  centroid_mm {:.17g} {:.17g} {:.17g}\n", body.centroidMm[0], body.centroidMm[1],
                            body.centroidMm[2]);
        text += std::format("  min_mm {:.17g} {:.17g} {:.17g}\n", body.minMm[0], body.minMm[1], body.minMm[2]);
        text += std::format("  max_mm {:.17g} {:.17g} {:.17g}\n", body.maxMm[0], body.maxMm[1], body.maxMm[2]);
    }
    return text;
}

} // namespace bettercad::reference
