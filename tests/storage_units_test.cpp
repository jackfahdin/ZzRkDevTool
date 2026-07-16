// SPDX-License-Identifier: MIT

#include <zzrk/core/StorageUnits.hpp>

#include <cstdint>
#include <limits>

int main()
{
    using zzrk::core::ByteCount;
    using zzrk::core::SectorCount;

    const auto bytes = zzrk::core::toBytes(SectorCount{2});
    if (!bytes.has_value() || bytes->value != 1024) {
        return 1;
    }

    const auto sectors = zzrk::core::toWholeSectors(ByteCount{4096});
    if (!sectors.has_value() || sectors->value != 8) {
        return 2;
    }
    if (zzrk::core::toWholeSectors(ByteCount{513}).has_value()) {
        return 3;
    }

    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (zzrk::core::toBytes(SectorCount{maximum}).has_value()) {
        return 4;
    }

    const auto zeroBytes = zzrk::core::toBytes(SectorCount{0});
    const auto zeroSectors = zzrk::core::toWholeSectors(ByteCount{0});
    if (!zeroBytes.has_value() || zeroBytes->value != 0
        || !zeroSectors.has_value() || zeroSectors->value != 0) {
        return 5;
    }

    constexpr std::uint64_t maximumConvertibleSectors =
        maximum / zzrk::core::kSectorSize;
    const auto boundary =
        zzrk::core::toBytes(SectorCount{maximumConvertibleSectors});
    if (!boundary.has_value()
        || boundary->value != maximumConvertibleSectors
                                  * zzrk::core::kSectorSize) {
        return 6;
    }

    if (zzrk::core::toBytes(SectorCount{maximumConvertibleSectors + 1})
            .has_value()) {
        return 7;
    }
    return 0;
}
