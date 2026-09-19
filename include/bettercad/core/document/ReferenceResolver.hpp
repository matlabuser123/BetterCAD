#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/document/ObjectReference.hpp>

#include <string_view>

// Turning a reference into the object it names (P13-REF-001, ADR-003).
//
// Two rules shape this interface, and both come from ADR-003.
//
// Resolution is explicit and injectable. Nothing here reads the filesystem.
// A resolver is supplied by the caller, so the CLI, a future GUI and the
// tests each provide their own, and a document can always be loaded with its
// external references left unresolved.
//
// Unresolved is a STATE, not an error. A reference that cannot be resolved
// right now is still a perfectly good reference -- the target may be on
// another machine, or simply not open yet -- so resolution reports why it
// did not resolve rather than failing. That is the same shape the dependency
// graph already uses for a missing local ObjectId.
namespace bettercad {

class Document;
class DocumentObject;

/// How a resolution ended.
enum class ReferenceState {
    /// The document was found, its identity matched, and it holds the object.
    Resolved,
    /// No resolver, or the resolver had nothing to offer for this document.
    DocumentUnavailable,
    /// A candidate document was offered, but its identity is not the one the
    /// reference names. The candidate is rejected: a locator that leads to
    /// the wrong document is a failed reference, never a match (ADR-003).
    DocumentMismatch,
    /// The right document, but it has no object of that ID.
    ObjectMissing,
};

/// "resolved", "document unavailable", "document mismatch", "object missing".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(ReferenceState state) noexcept;

/// What a resolution produced. `object` and `document` are set only when the
/// state is Resolved, and point into the document the resolver supplied --
/// which owns them, and must outlive the use of this result.
struct ResolvedReference {
    ReferenceState state = ReferenceState::DocumentUnavailable;
    const Document* document = nullptr;
    const DocumentObject* object = nullptr;

    [[nodiscard]] bool resolved() const noexcept { return state == ReferenceState::Resolved; }
};

/// Supplies the documents an external reference might name.
///
/// A resolver only ever *proposes*: it returns a candidate, and resolution
/// checks the candidate's own identity against the reference. A resolver
/// cannot cause a reference to bind to the wrong document, however it is
/// implemented, because it is never asked whether the document is right.
///
/// Implementations may look in a workspace, a library, a cache or a test
/// fixture. None of that belongs in the document model, which is why this is
/// an interface passed in rather than a global.
class BETTERCAD_CORE_EXPORT ReferenceResolver {
public:
    virtual ~ReferenceResolver() = default;
    ReferenceResolver(const ReferenceResolver&) = delete;
    ReferenceResolver& operator=(const ReferenceResolver&) = delete;
    ReferenceResolver(ReferenceResolver&&) = delete;
    ReferenceResolver& operator=(ReferenceResolver&&) = delete;

    /// A document that might be the one @p reference names, or nullptr if
    /// this resolver has nothing to offer. The returned document must
    /// outlive the resolution result.
    ///
    /// Returning the wrong document is not an error the implementation needs
    /// to avoid: the caller checks its identity.
    [[nodiscard]] virtual const Document* candidate(const ObjectReference& reference) const = 0;

protected:
    ReferenceResolver() = default;
};

/// The object @p reference names.
///
/// An internal reference is looked up in @p local and never consults the
/// resolver. An external one asks @p resolver for a candidate, checks that
/// the candidate's identity is the one the reference names, and only then
/// looks for the object in it.
///
/// Never fails: an unresolvable reference comes back with the state that
/// says why. With no resolver, an external reference is DocumentUnavailable,
/// which is what lets a document load and report its unresolved references
/// rather than refusing to open.
[[nodiscard]] BETTERCAD_CORE_EXPORT ResolvedReference resolve(const ObjectReference& reference, const Document& local,
                                                              const ReferenceResolver* resolver = nullptr);

} // namespace bettercad
