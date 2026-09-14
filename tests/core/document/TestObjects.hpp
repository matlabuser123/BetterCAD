#pragma once

#include <bettercad/core/document/DocumentObject.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

// Minimal object kinds for document tests. Real kinds (sketches, features,
// bodies) live in higher-level modules and plug in the same way.
namespace bettercad::test {

class TestBlock final : public DocumentObject {
public:
    TestBlock(std::string name, double size) : DocumentObject(std::move(name)), size_(size) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "test_block"; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override {
        return std::make_unique<TestBlock>(*this);
    }
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override {
        return size_ == static_cast<const TestBlock&>(other).size_;
    }

    [[nodiscard]] double size() const noexcept { return size_; }
    Result<bool> setSize(double size) {
        if (size == size_) {
            return false;
        }
        size_ = size;
        return true;
    }

private:
    double size_;
};

class TestMarker final : public DocumentObject {
public:
    explicit TestMarker(std::string name) : DocumentObject(std::move(name)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "test_marker"; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override {
        return std::make_unique<TestMarker>(*this);
    }
    [[nodiscard]] bool contentEquals(const DocumentObject& /*other*/) const override { return true; }
};

} // namespace bettercad::test
