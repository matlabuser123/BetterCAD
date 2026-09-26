#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/core/units/Units.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace bettercad {

namespace {

constexpr std::array kCatalog{
    describe(units::m),        describe(units::mm),        describe(units::cm),
    describe(units::um),       describe(units::km),        describe(units::inch),
    describe(units::ft),       describe(units::m2),        describe(units::cm2),
    describe(units::mm2),      describe(units::m3),        describe(units::L),
    describe(units::cm3),      describe(units::mm3),       describe(units::rad),
    describe(units::deg),      describe(units::kg),        describe(units::g),
    describe(units::tonne),    describe(units::s),         describe(units::ms),
    describe(units::minute),   describe(units::hour),      describe(units::K),
    describe(units::m_per_s),  describe(units::mm_per_s),  describe(units::km_per_h),
    describe(units::m_per_s2), describe(units::N),         describe(units::kN),
    describe(units::Pa),       describe(units::kPa),       describe(units::MPa),
    describe(units::GPa),      describe(units::bar),       describe(units::kg_per_m3),
    describe(units::g_per_cm3), describe(units::J),          describe(units::kJ),
    describe(units::W),         describe(units::kW),         describe(units::W_per_m_K),
    describe(units::J_per_kg_K), describe(units::kJ_per_kg_K), describe(units::per_K),
    describe(units::um_per_m_K), describe(units::Pa_s),      describe(units::mPa_s),
    describe(units::m2_per_s),  describe(units::mm2_per_s),
};

consteval bool symbolsAreUnique() {
    for (std::size_t i = 0; i < kCatalog.size(); ++i) {
        for (std::size_t j = i + 1; j < kCatalog.size(); ++j) {
            if (kCatalog[i].symbol == kCatalog[j].symbol) {
                return false;
            }
        }
    }
    return true;
}

static_assert(symbolsAreUnique(), "unit symbols must be unique");

} // namespace

std::span<const UnitDescriptor> unitCatalog() noexcept {
    return kCatalog;
}

std::optional<UnitDescriptor> findUnit(std::string_view symbol) noexcept {
    const auto it = std::ranges::find(kCatalog, symbol, &UnitDescriptor::symbol);
    if (it == kCatalog.end()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace bettercad
