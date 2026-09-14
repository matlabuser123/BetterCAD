#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/io/Stl.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <vector>

namespace bettercad::io {

namespace {

struct Vec3f {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

std::string printable(std::string_view text) {
    std::string result{text};
    for (char& c : result) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte > 0x7E) {
            c = '_';
        }
    }
    return result;
}

Result<float> toFloat(double value) {
    if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("STL: coordinate {} mm cannot be stored as a 32-bit float", value));
    }
    return static_cast<float>(value);
}

Result<std::vector<Vec3f>> verticesInMm(const geometry::Mesh& mesh) {
    std::vector<Vec3f> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const Point3D& p : mesh.vertices) {
        auto x = toFloat(p.x.in(units::mm));
        auto y = toFloat(p.y.in(units::mm));
        auto z = toFloat(p.z.in(units::mm));
        if (!x || !y || !z) {
            return std::unexpected(!x ? x.error() : !y ? y.error() : z.error());
        }
        vertices.push_back({*x, *y, *z});
    }
    return vertices;
}

/// Unit normal of the counter-clockwise triangle (a, b, c); zero if degenerate.
Vec3f normalOf(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
    const double ux = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double uy = static_cast<double>(b.y) - static_cast<double>(a.y);
    const double uz = static_cast<double>(b.z) - static_cast<double>(a.z);
    const double vx = static_cast<double>(c.x) - static_cast<double>(a.x);
    const double vy = static_cast<double>(c.y) - static_cast<double>(a.y);
    const double vz = static_cast<double>(c.z) - static_cast<double>(a.z);
    const double nx = uy * vz - uz * vy;
    const double ny = uz * vx - ux * vz;
    const double nz = ux * vy - uy * vx;
    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!(length > 0.0) || !std::isfinite(length)) {
        return {};
    }
    return {static_cast<float>(nx / length), static_cast<float>(ny / length), static_cast<float>(nz / length)};
}

void appendUint(std::string& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFU)); // little-endian
    }
}

void appendFloat(std::string& out, float value) {
    appendUint(out, std::bit_cast<std::uint32_t>(value), 4);
}

void appendVector(std::string& out, const Vec3f& v) {
    appendFloat(out, v.x);
    appendFloat(out, v.y);
    appendFloat(out, v.z);
}

} // namespace

Result<std::string> meshesToStl(std::span<const geometry::Mesh> meshes, StlFormat format, std::string_view name) {
    std::size_t triangleCount = 0;
    for (const geometry::Mesh& mesh : meshes) {
        triangleCount += mesh.triangles.size();
    }
    if (format == StlFormat::Binary && triangleCount > std::numeric_limits<std::uint32_t>::max()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("STL: {} triangles exceed the binary format's limit", triangleCount));
    }
    const std::string label = name.empty() ? std::string{"BetterCAD"} : printable(name);

    std::string out;
    if (format == StlFormat::Binary) {
        // The header must not start with "solid", which marks ASCII STL.
        std::string header = std::format("BetterCAD {} binary STL, units mm: {}", buildInfo().version, label);
        header.resize(80, ' ');
        out.reserve(84 + 50 * triangleCount);
        out += header;
        appendUint(out, static_cast<std::uint32_t>(triangleCount), 4);
    } else {
        out += std::format("solid {}\n", label);
    }

    for (const geometry::Mesh& mesh : meshes) {
        auto vertices = verticesInMm(mesh);
        if (!vertices) {
            return std::unexpected(vertices.error());
        }
        for (const auto& triangle : mesh.triangles) {
            for (const std::uint32_t index : triangle) {
                if (index >= vertices->size()) {
                    return makeError(ErrorCode::InvalidArgument,
                                     std::format("STL: a triangle references vertex {} of {}", index,
                                                 vertices->size()));
                }
            }
            const Vec3f& a = (*vertices)[triangle[0]];
            const Vec3f& b = (*vertices)[triangle[1]];
            const Vec3f& c = (*vertices)[triangle[2]];
            const Vec3f n = normalOf(a, b, c);
            if (format == StlFormat::Binary) {
                appendVector(out, n);
                appendVector(out, a);
                appendVector(out, b);
                appendVector(out, c);
                appendUint(out, 0, 2); // attribute byte count
            } else {
                auto it = std::back_inserter(out);
                std::format_to(it, "  facet normal {} {} {}\n    outer loop\n", n.x, n.y, n.z);
                for (const Vec3f* v : {&a, &b, &c}) {
                    std::format_to(it, "      vertex {} {} {}\n", v->x, v->y, v->z);
                }
                out += "    endloop\n  endfacet\n";
            }
        }
    }
    if (format == StlFormat::Ascii) {
        out += std::format("endsolid {}\n", label);
    }
    return out;
}

} // namespace bettercad::io
