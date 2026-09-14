#include <bettercad/core/Error.hpp>

namespace bettercad {

std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidArgument:
        return "invalid argument";
    case ErrorCode::NotFound:
        return "not found";
    case ErrorCode::AlreadyExists:
        return "already exists";
    case ErrorCode::DimensionMismatch:
        return "dimension mismatch";
    case ErrorCode::ParseError:
        return "parse error";
    case ErrorCode::FailedPrecondition:
        return "failed precondition";
    case ErrorCode::IoError:
        return "input/output error";
    case ErrorCode::Internal:
        return "internal error";
    }
    return "unknown error";
}

} // namespace bettercad
