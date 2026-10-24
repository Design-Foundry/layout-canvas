#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vc {
/// Immutable local, straight-alpha sRGB RGBA8 pixels. No URL or executable format.
struct Raster {
    int width = 0;
    int height = 0;
    std::string pixels;
    std::string id;
    friend bool operator==(const Raster&, const Raster&) = default;
};
inline constexpr int kRasterMaxSide = 512;
inline constexpr std::size_t kRasterMaxDocumentBytes = 1400000;
[[nodiscard]] std::string rasterDigest(int width, int height, const std::vector<std::uint8_t>& pixels);
[[nodiscard]] std::string encodeRasterPixels(const std::vector<std::uint8_t>& pixels);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> decodeRaster(const Raster& raster);
[[nodiscard]] bool validRaster(const Raster& raster);
/// GPU upload pixels with RGB weighted by alpha, for correct linear filtering.
/// Stored Raster pixels remain straight alpha. Malformed non-RGBA input returns empty.
[[nodiscard]] std::vector<std::uint8_t> premultiplyRasterPixels(const std::vector<std::uint8_t>& pixels);
} // namespace vc
