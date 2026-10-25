#pragma once
// A comment naming ValidationStatus::Pass.
struct ThermalAnalysisRun {
    [[nodiscard]] bool accepted() const noexcept { return validation.accepted(); }
};
