// SPDX-License-Identifier: MIT

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace zzrk::core {

inline constexpr std::uint64_t kSectorSize = 512;

struct ByteCount {
    std::uint64_t value{};

    friend constexpr auto operator<=>(const ByteCount&, const ByteCount&) =
        default;
};

struct SectorCount {
    std::uint64_t value{};

    friend constexpr auto operator<=>(const SectorCount&, const SectorCount&) =
        default;
};

struct Lba {
    std::uint64_t value{};

    friend constexpr auto operator<=>(const Lba&, const Lba&) = default;
};

[[nodiscard]] constexpr std::optional<ByteCount>
toBytes(const SectorCount sectors) noexcept
{
    if (sectors.value
        > std::numeric_limits<std::uint64_t>::max() / kSectorSize) {
        return std::nullopt;
    }
    return ByteCount{sectors.value * kSectorSize};
}

[[nodiscard]] constexpr std::optional<SectorCount>
toWholeSectors(const ByteCount bytes) noexcept
{
    if (bytes.value % kSectorSize != 0) {
        return std::nullopt;
    }
    return SectorCount{bytes.value / kSectorSize};
}

} // namespace zzrk::core
