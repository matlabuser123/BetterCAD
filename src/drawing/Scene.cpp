#include <bettercad/drawing/Scene.hpp>

#include <cmath>
#include <format>

namespace bettercad::drawing {

std::string_view toString(LineStyle style) noexcept {
    switch (style) {
    case LineStyle::Continuous:
        return "continuous";
    case LineStyle::Dashed:
        return "dashed";
    case LineStyle::Centre:
        return "centre";
    case LineStyle::Thin:
        return "thin";
    }
    return "unknown";
}

std::optional<LineStyle> lineStyleFromString(std::string_view text) noexcept {
    for (const LineStyle style :
         {LineStyle::Continuous, LineStyle::Dashed, LineStyle::Centre, LineStyle::Thin}) {
        if (toString(style) == text) {
            return style;
        }
    }
    return std::nullopt;
}

std::string_view toString(TextAnchor anchor) noexcept {
    switch (anchor) {
    case TextAnchor::BaselineLeft:
        return "baseline_left";
    case TextAnchor::BaselineCentre:
        return "baseline_centre";
    case TextAnchor::BaselineRight:
        return "baseline_right";
    case TextAnchor::MiddleLeft:
        return "middle_left";
    case TextAnchor::MiddleCentre:
        return "middle_centre";
    case TextAnchor::MiddleRight:
        return "middle_right";
    }
    return "unknown";
}

std::optional<TextAnchor> textAnchorFromString(std::string_view text) noexcept {
    for (const TextAnchor anchor :
         {TextAnchor::BaselineLeft, TextAnchor::BaselineCentre, TextAnchor::BaselineRight,
          TextAnchor::MiddleLeft, TextAnchor::MiddleCentre, TextAnchor::MiddleRight}) {
        if (toString(anchor) == text) {
            return anchor;
        }
    }
    return std::nullopt;
}

void SceneItems::append(const SceneItems& other) {
    lines.insert(lines.end(), other.lines.begin(), other.lines.end());
    texts.insert(texts.end(), other.texts.begin(), other.texts.end());
}

Result<void> validate(const SceneItems& items) {
    const auto finite = [](const Point2D& p) {
        return std::isfinite(p.x.si()) && std::isfinite(p.y.si());
    };
    for (const SceneLine& line : items.lines) {
        if (line.points.size() < 2) {
            return makeError(ErrorCode::InvalidArgument,
                             "a drawn line needs at least two points; one point is not a line");
        }
        if (toString(line.style) == "unknown") {
            return makeError(ErrorCode::InvalidArgument, "a drawn line has an unknown style");
        }
        for (const Point2D& p : line.points) {
            if (!finite(p)) {
                return makeError(ErrorCode::InvalidArgument,
                                 "a drawn line has a point that is not finite");
            }
        }
    }
    for (const SceneText& text : items.texts) {
        if (text.text.empty()) {
            return makeError(ErrorCode::InvalidArgument,
                             "a text run with nothing in it would draw nothing and take up room");
        }
        if (!finite(text.at)) {
            return makeError(ErrorCode::InvalidArgument, "a text run sits at a point that is not finite");
        }
        if (!std::isfinite(text.height.si()) || text.height.si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "a text run's height must be finite and greater than zero");
        }
        if (!std::isfinite(text.rotation.si())) {
            return makeError(ErrorCode::InvalidArgument, "a text run's rotation must be finite");
        }
        if (toString(text.anchor) == "unknown") {
            return makeError(ErrorCode::InvalidArgument, "a text run has an unknown anchor");
        }
    }
    return {};
}

} // namespace bettercad::drawing
