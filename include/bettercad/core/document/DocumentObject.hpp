#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Naming.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad {

class Document;

/// Object names follow the identifier rule; see validateIdentifier().
[[nodiscard]] inline Result<void> validateObjectName(std::string_view name) {
    return validateIdentifier(name, "object");
}

/// Base class of everything a document contains besides parameters:
/// sketches, features, bodies. Concrete kinds are defined by higher-level
/// modules, so the document model does not depend on them.
///
/// An object gets its ID when it is added to a document. Name and revision
/// are managed by the owning Document; subclasses change their own state
/// only through Document::modifyObject(), so every change is tracked.
class BETTERCAD_CORE_EXPORT DocumentObject {
public:
    virtual ~DocumentObject();
    DocumentObject& operator=(const DocumentObject&) = delete;

    /// Invalid until the object is added to a document.
    [[nodiscard]] ObjectId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    /// Starts at 1 and increments on every effective change.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// Stable kind name used in files and diagnostics, e.g. "sketch".
    [[nodiscard]] virtual std::string_view typeName() const noexcept = 0;
    /// Deep copy, including ID, name and revision.
    [[nodiscard]] virtual std::unique_ptr<DocumentObject> clone() const = 0;
    /// Equality of the kind-specific content. Called only with an object of
    /// the same typeName(); ID and name are compared by the caller.
    [[nodiscard]] virtual bool contentEquals(const DocumentObject& other) const = 0;
    /// Items (parameters or objects) this object's result is computed from.
    /// Drives dirty propagation and regeneration order. Default: none.
    [[nodiscard]] virtual std::vector<ObjectId> dependencies() const;

protected:
    explicit DocumentObject(std::string name);
    DocumentObject(const DocumentObject&) = default;

private:
    friend class Document;

    ObjectId id_;
    std::string name_;
    std::uint64_t revision_ = 1;
};

/// Same ID, name, kind and content; revisions are ignored.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const DocumentObject& a,
                                                    const DocumentObject& b);

} // namespace bettercad
