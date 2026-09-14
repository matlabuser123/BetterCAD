#include <bettercad/core/document/DocumentObject.hpp>

namespace bettercad {

DocumentObject::DocumentObject(std::string name) : name_(std::move(name)) {}

DocumentObject::~DocumentObject() = default;

std::vector<ObjectId> DocumentObject::dependencies() const {
    return {};
}

bool equivalent(const DocumentObject& a, const DocumentObject& b) {
    return a.id() == b.id() && a.name() == b.name() && a.typeName() == b.typeName() &&
           a.contentEquals(b);
}

} // namespace bettercad
