// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>

#include <algorithm>

namespace zzrk::protocol {
namespace {

constexpr std::array<std::byte, 4> kCommandSignature{
    std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x43}};
constexpr std::array<std::byte, 4> kStatusSignature{
    std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53}};

constexpr void writeLe32(std::span<std::byte, 4> destination,
                         const std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < destination.size(); ++index) {
        destination[index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

constexpr std::uint32_t readLe32(
    const std::span<const std::byte, 4> source) noexcept
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < source.size(); ++index) {
        value |= std::to_integer<std::uint32_t>(source[index])
                 << (index * 8U);
    }
    return value;
}

} // namespace

CommandEncodeResult encodeCommand(const RockusbCommand& command) noexcept
{
    if (command.direction != DataDirection::hostToDevice
        && command.direction != DataDirection::deviceToHost) {
        return CommandEncodeError{
            CommandEncodeErrorCode::invalidDirection,
            static_cast<std::uint8_t>(command.direction)};
    }
    if (command.logicalUnit > 0x0fU) {
        return CommandEncodeError{CommandEncodeErrorCode::invalidLogicalUnit,
                                  command.logicalUnit};
    }
    if (command.commandLength == 0U
        || command.commandLength > kRockusbCommandBlockSize) {
        return CommandEncodeError{
            CommandEncodeErrorCode::invalidCommandLength,
            command.commandLength};
    }

    RockusbCommandBytes bytes{};
    std::copy(kCommandSignature.begin(), kCommandSignature.end(),
              bytes.begin());
    writeLe32(std::span<std::byte, 4>{bytes.data() + 4, 4}, command.tag);
    writeLe32(std::span<std::byte, 4>{bytes.data() + 8, 4},
              command.transferLength);
    bytes[12] = static_cast<std::byte>(command.direction);
    bytes[13] = static_cast<std::byte>(command.logicalUnit);
    bytes[14] = static_cast<std::byte>(command.commandLength);
    std::copy_n(command.commandBlock.begin(), command.commandLength,
                bytes.begin() + 15);
    return bytes;
}

StatusDecodeResult decodeStatus(const std::span<const std::byte> bytes,
                                const std::uint32_t expectedTag) noexcept
{
    if (bytes.size() != kRockusbStatusSize) {
        return StatusDecodeError{.code = StatusDecodeErrorCode::unexpectedSize,
                                 .actualSize = bytes.size(),
                                 .expectedTag = expectedTag};
    }

    const auto signature =
        readLe32(std::span<const std::byte, 4>{bytes.data(), 4});
    if (!std::equal(kStatusSignature.begin(), kStatusSignature.end(),
                    bytes.begin())) {
        return StatusDecodeError{
            .code = StatusDecodeErrorCode::invalidSignature,
            .actualSize = bytes.size(),
            .expectedTag = expectedTag,
            .actualSignature = signature,
        };
    }

    const auto tag =
        readLe32(std::span<const std::byte, 4>{bytes.data() + 4, 4});
    const auto dataResidue =
        readLe32(std::span<const std::byte, 4>{bytes.data() + 8, 4});
    const auto status = std::to_integer<std::uint8_t>(bytes[12]);

    if (tag != expectedTag) {
        return StatusDecodeError{
            .code = StatusDecodeErrorCode::tagMismatch,
            .actualSize = bytes.size(),
            .expectedTag = expectedTag,
            .actualTag = tag,
            .actualSignature = signature,
            .dataResidue = dataResidue,
            .rawStatus = status,
        };
    }

    switch (status) {
    case 0:
        return RockusbStatus{.tag = tag, .dataResidue = dataResidue};
    case 1:
        return StatusDecodeError{
            .code = StatusDecodeErrorCode::commandFailed,
            .actualSize = bytes.size(),
            .expectedTag = expectedTag,
            .actualTag = tag,
            .actualSignature = signature,
            .dataResidue = dataResidue,
            .rawStatus = status,
        };
    case 2:
        return StatusDecodeError{
            .code = StatusDecodeErrorCode::phaseError,
            .actualSize = bytes.size(),
            .expectedTag = expectedTag,
            .actualTag = tag,
            .actualSignature = signature,
            .dataResidue = dataResidue,
            .rawStatus = status,
        };
    default:
        return StatusDecodeError{
            .code = StatusDecodeErrorCode::invalidStatus,
            .actualSize = bytes.size(),
            .expectedTag = expectedTag,
            .actualTag = tag,
            .actualSignature = signature,
            .dataResidue = dataResidue,
            .rawStatus = status,
        };
    }
}

} // namespace zzrk::protocol
