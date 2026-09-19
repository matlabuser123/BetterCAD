#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>

#include <optional>
#include <string>

// How one object names another (P13-REF-001, implementing the external
// reference contract ADR-003 recorded).
//
// The rule the whole milestone turns on:
//
//     ObjectId               local identity, meaningful only inside one
//                            Document
//     external reference     stable document identity
//                            + stable object identity
//                            + a resolver
//
// An ObjectId is durable inside its own document -- IDs are allocated in
// sequence and never reused, and the high-water mark is persisted -- so it is
// perfectly good *object* identity. What it lacks is any statement of WHICH
// document it belongs to, which is why P13-COMP-001 measured that a number
// copied from another document binds to whatever local object happens to
// share it. Qualifying the ObjectId with the owning document's UUID is what
// closes that, and it needs no new identity scheme: DocumentId is already a
// UUID, already persisted, and already survives the file being moved.
//
// A filesystem path is NEVER identity. `hint` exists so a resolver has
// somewhere to start looking, and ADR-003 is explicit that a hint leading to
// a document with a different UUID is a failed reference, not a match.
namespace bettercad {

/// A reference to a document object: one in the referring document, or one
/// in another document.
///
/// `object` is always the ID the object has **in the document that owns it**.
/// With no `document`, that is the referring document and the reference is
/// internal. With a `document`, the reference is external and `object` means
/// nothing locally -- resolving it must find that document first.
///
/// Canonical identity is `document` and `object`. `hint` is locator metadata
/// and is not part of it: moving a file changes where the target is found,
/// never which target is meant. Use sameTarget() to compare identity;
/// operator== compares everything, so that a save/load test notices a hint
/// that was dropped.
struct ObjectReference {
    /// The object's ID in the document that owns it.
    ObjectId object{};
    /// Empty for a reference inside this document; otherwise the durable
    /// identity of the document that owns `object`.
    std::optional<DocumentId> document{};
    /// Where that document might be found. A locator, never identity, and
    /// never trusted: a candidate found through it is accepted only if its
    /// own identity matches `document`.
    std::string hint{};

    /// An internal reference to @p local in the referring document. Not
    /// explicit: an ObjectId *is* an internal reference, and letting one
    /// convert keeps every caller that already names a local object working
    /// unchanged.
    constexpr ObjectReference(ObjectId local = {}) noexcept : object(local) {}

    /// An external reference to @p target in the document @p owner, with an
    /// optional locator.
    ObjectReference(DocumentId owner, ObjectId target, std::string locator = {})
        : object(target), document(owner), hint(std::move(locator)) {}

    /// Compares everything, the hint included. For "do these mean the same
    /// target", use sameTarget().
    friend bool operator==(const ObjectReference&, const ObjectReference&) = default;
};

/// Whether @p reference names an object of the referring document.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isInternal(const ObjectReference& reference) noexcept;

/// Whether @p a and @p b name the same object: the same document and the
/// same object ID. The hint is deliberately ignored, so a reference whose
/// locator changed is still the same reference.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool sameTarget(const ObjectReference& a, const ObjectReference& b) noexcept;

/// Checks a reference on its own: a valid object ID, a non-nil document
/// identity when it is external, and no locator on an internal one (there is
/// nothing to locate). InvalidArgument otherwise. Whether the document can be
/// found and whether the object exists in it is resolution's business.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const ObjectReference& reference);

/// The local object a reference names, or nothing if it is external. This is
/// what the dependency graph may use: an external reference contributes no
/// edge, because ObjectId cannot name a node in another document (ADR-003).
[[nodiscard]] BETTERCAD_CORE_EXPORT std::optional<ObjectId> localTarget(const ObjectReference& reference) noexcept;

} // namespace bettercad
