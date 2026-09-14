#include <bettercad/features/Feature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <format>
#include <set>

namespace bettercad::features {

std::vector<ObjectId> resultFeatures(const Document& document) {
    std::set<ObjectId> consumed;
    std::vector<ObjectId> producers;
    for (const DocumentObject& object : document.objects()) {
        if (const auto* feature = dynamic_cast<const SolidFeature*>(&object)) {
            producers.push_back(object.id());
            if (const auto target = feature->target()) {
                consumed.insert(ObjectId{*target});
            }
        }
    }
    std::erase_if(producers, [&](ObjectId id) { return consumed.contains(id); });
    return producers;
}

Result<std::vector<ResultBody>> regenerateResultBodies(const Document& document) {
    Document copy = document.clone();
    Regenerator regenerator;
    auto report = regenerator.regenerate(copy);
    if (!report) {
        return std::unexpected(report.error());
    }
    if (!report->succeeded()) {
        std::string problems;
        for (const auto& [id, error] : report->errors) {
            const auto name = copy.nameOf(id);
            problems += std::format("\n  {}: {}", name ? std::string{*name} : std::format("{}", id), error.message);
        }
        for (const ObjectId id : report->blocked) {
            const auto name = copy.nameOf(id);
            problems += std::format("\n  {}: not regenerated (depends on a failed item)",
                                    name ? std::string{*name} : std::format("{}", id));
        }
        return makeError(ErrorCode::FailedPrecondition, std::format("the model does not regenerate:{}", problems));
    }

    std::vector<ResultBody> bodies;
    for (const ObjectId id : resultFeatures(copy)) {
        const geometry::Body* body = regenerator.body(id);
        if (body == nullptr) {
            return makeError(ErrorCode::Internal, std::format("{} produced no body", id));
        }
        bodies.push_back({id, std::string{*copy.nameOf(id)}, *body});
    }
    return bodies;
}

} // namespace bettercad::features
