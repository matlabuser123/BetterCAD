#pragma once

#include <bettercad/core/Error.hpp>

#include <Standard_Failure.hxx>

#include <exception>
#include <format>
#include <string_view>
#include <utility>

namespace bettercad::geometry::occt {

/// Runs a kernel call and converts any exception into an Internal error, so
/// that kernel failures surface as diagnostics rather than crossing the
/// BetterCAD API as exceptions.
template <typename Fn>
auto guardKernelCall(std::string_view operation, Fn&& fn) -> decltype(std::forward<Fn>(fn)()) {
    try {
        return std::forward<Fn>(fn)();
    } catch (const Standard_Failure& e) {
        return makeError(ErrorCode::Internal, std::format("{}: geometry kernel raised {}: {}",
                                                          operation, e.ExceptionType(), e.what()));
    } catch (const std::exception& e) {
        return makeError(ErrorCode::Internal, std::format("{}: {}", operation, e.what()));
    }
}

} // namespace bettercad::geometry::occt
