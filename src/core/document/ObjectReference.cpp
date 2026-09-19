#include <bettercad/core/document/ObjectReference.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ReferenceResolver.hpp>

namespace bettercad {

bool isInternal(const ObjectReference& reference) noexcept {
    return !reference.document.has_value();
}

bool sameTarget(const ObjectReference& a, const ObjectReference& b) noexcept {
    // The hint is not identity: a reference whose locator changed still means
    // the same object.
    return a.document == b.document && a.object == b.object;
}

Result<void> validate(const ObjectReference& reference) {
    if (!reference.object.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a reference must name an object");
    }
    if (reference.document) {
        if (reference.document->value().isNil()) {
            return makeError(ErrorCode::InvalidArgument,
                             "an external reference must name the document that owns the object");
        }
    } else if (!reference.hint.empty()) {
        // A locator with nothing to locate: the reference is internal, so the
        // object is in this document and there is no file to find.
        return makeError(ErrorCode::InvalidArgument, "an internal reference has nothing to locate");
    }
    return {};
}

std::optional<ObjectId> localTarget(const ObjectReference& reference) noexcept {
    if (!isInternal(reference)) {
        return std::nullopt;
    }
    return reference.object;
}

std::string_view toString(ReferenceState state) noexcept {
    switch (state) {
    case ReferenceState::Resolved:
        return "resolved";
    case ReferenceState::DocumentUnavailable:
        return "document unavailable";
    case ReferenceState::DocumentMismatch:
        return "document mismatch";
    case ReferenceState::ObjectMissing:
        return "object missing";
    }
    return "unknown";
}

ResolvedReference resolve(const ObjectReference& reference, const Document& local,
                          const ReferenceResolver* resolver) {
    const Document* owner = &local;
    if (!isInternal(reference)) {
        if (resolver == nullptr) {
            return {.state = ReferenceState::DocumentUnavailable};
        }
        const Document* candidate = resolver->candidate(reference);
        if (candidate == nullptr) {
            return {.state = ReferenceState::DocumentUnavailable};
        }
        // The resolver proposed; we decide. A candidate whose identity is not
        // the one named is rejected outright -- it is never treated as a
        // near-enough match, however it was found (ADR-003).
        if (candidate->id() != *reference.document) {
            return {.state = ReferenceState::DocumentMismatch, .document = candidate};
        }
        owner = candidate;
    }
    const DocumentObject* object = owner->findObject(reference.object);
    if (object == nullptr) {
        return {.state = ReferenceState::ObjectMissing, .document = owner};
    }
    return {.state = ReferenceState::Resolved, .document = owner, .object = object};
}

} // namespace bettercad
