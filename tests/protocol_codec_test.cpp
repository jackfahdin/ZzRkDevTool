// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>

namespace {

using zzrk::protocol::CommandEncodeError;
using zzrk::protocol::CommandEncodeErrorCode;
using zzrk::protocol::RockusbCommand;
using zzrk::protocol::RockusbCommandBytes;
using zzrk::protocol::RockusbStatus;
using zzrk::protocol::StatusDecodeError;
using zzrk::protocol::StatusDecodeErrorCode;

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

constexpr zzrk::protocol::RockusbStatusBytes makeSuccessStatus()
{
    return {
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53},
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
        std::byte{0x00},
    };
}

void testCommandGoldenVector()
{
    RockusbCommand command{
        .tag = 0x04030201U,
        .transferLength = 0x11223344U,
        .direction = zzrk::protocol::DataDirection::deviceToHost,
        .logicalUnit = 0x05,
        .commandLength = 0x06,
        .commandBlock = {
            std::byte{0x1b}, std::byte{0xa1}, std::byte{0xa2},
            std::byte{0xa3}, std::byte{0xa4}, std::byte{0xa5},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00},
        },
    };

    constexpr RockusbCommandBytes golden{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x43},
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11},
        std::byte{0x80}, std::byte{0x05}, std::byte{0x06},
        std::byte{0x1b}, std::byte{0xa1}, std::byte{0xa2},
        std::byte{0xa3}, std::byte{0xa4}, std::byte{0xa5},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00},
    };

    const auto encoded = zzrk::protocol::encodeCommand(command);
    const auto* bytes = std::get_if<RockusbCommandBytes>(&encoded);
    expect(bytes != nullptr, "valid command encodes successfully");
    expect(bytes != nullptr && *bytes == golden,
           "command encoding matches the 31-byte golden vector");
}

void testCommandValidation()
{
    RockusbCommand command{.commandLength = 6};
    command.logicalUnit = 16;
    auto encoded = zzrk::protocol::encodeCommand(command);
    const auto* error = std::get_if<CommandEncodeError>(&encoded);
    expect(error != nullptr
               && error->code == CommandEncodeErrorCode::invalidLogicalUnit
               && error->value == 16,
           "logical units above 15 are rejected");

    command.logicalUnit = 0;
    command.commandLength = 0;
    encoded = zzrk::protocol::encodeCommand(command);
    error = std::get_if<CommandEncodeError>(&encoded);
    expect(error != nullptr
               && error->code == CommandEncodeErrorCode::invalidCommandLength,
           "zero-length command blocks are rejected");

    command.commandLength = 17;
    encoded = zzrk::protocol::encodeCommand(command);
    error = std::get_if<CommandEncodeError>(&encoded);
    expect(error != nullptr
               && error->code == CommandEncodeErrorCode::invalidCommandLength,
           "command blocks longer than 16 bytes are rejected");

    command.commandLength = 6;
    command.direction = static_cast<zzrk::protocol::DataDirection>(0x01);
    encoded = zzrk::protocol::encodeCommand(command);
    error = std::get_if<CommandEncodeError>(&encoded);
    expect(error != nullptr
               && error->code == CommandEncodeErrorCode::invalidDirection
               && error->value == 0x01,
           "unknown data directions are rejected");
}

void testUnusedCommandBytesAreZeroed()
{
    RockusbCommand command{.commandLength = 1};
    command.commandBlock.fill(std::byte{0xff});

    const auto encoded = zzrk::protocol::encodeCommand(command);
    const auto* bytes = std::get_if<RockusbCommandBytes>(&encoded);
    expect(bytes != nullptr, "one-byte command encodes successfully");
    expect(bytes != nullptr && (*bytes)[15] == std::byte{0xff},
           "declared command bytes are preserved");
    expect(bytes != nullptr
               && std::all_of(bytes->begin() + 16, bytes->end(),
                              [](const std::byte value) {
                                  return value == std::byte{0x00};
                              }),
           "bytes after the declared command length are zeroed");
}

void testStatusGoldenVector()
{
    constexpr auto wire = makeSuccessStatus();
    const auto decoded =
        zzrk::protocol::decodeStatus(wire, 0x04030201U);
    const auto* status = std::get_if<RockusbStatus>(&decoded);
    expect(status != nullptr, "valid status decodes successfully");
    expect(status != nullptr && status->tag == 0x04030201U,
           "status tag is decoded little-endian");
    expect(status != nullptr && status->dataResidue == 0x12345678U,
           "status residue is decoded little-endian");
}

void testInvalidSize()
{
    constexpr auto wire = makeSuccessStatus();
    const auto decoded = zzrk::protocol::decodeStatus(
        std::span<const std::byte>{wire}.first(12), 0x04030201U);
    const auto* error = std::get_if<StatusDecodeError>(&decoded);
    expect(error != nullptr
               && error->code == StatusDecodeErrorCode::unexpectedSize
               && error->actualSize == 12,
           "short status wrappers are rejected");
}

void testInvalidSignature()
{
    auto wire = makeSuccessStatus();
    wire[3] = std::byte{0x00};
    const auto decoded =
        zzrk::protocol::decodeStatus(wire, 0x04030201U);
    const auto* error = std::get_if<StatusDecodeError>(&decoded);
    expect(error != nullptr
               && error->code == StatusDecodeErrorCode::invalidSignature,
           "non-USBS signatures are rejected");
}

void testTagMismatch()
{
    constexpr auto wire = makeSuccessStatus();
    const auto decoded =
        zzrk::protocol::decodeStatus(wire, 0xaabbccddU);
    const auto* error = std::get_if<StatusDecodeError>(&decoded);
    expect(error != nullptr
               && error->code == StatusDecodeErrorCode::tagMismatch
               && error->expectedTag == 0xaabbccddU
               && error->actualTag == 0x04030201U,
           "a status tag must match its command tag");
}

void testStatusErrors()
{
    auto wire = makeSuccessStatus();
    wire[12] = std::byte{0x01};
    auto decoded = zzrk::protocol::decodeStatus(wire, 0x04030201U);
    auto* error = std::get_if<StatusDecodeError>(&decoded);
    expect(error != nullptr
               && error->code == StatusDecodeErrorCode::commandFailed
               && error->deviceDetail == 0x12345678U,
           "device-reported command failures preserve the detail field");

    wire[12] = std::byte{0x02};
    decoded = zzrk::protocol::decodeStatus(wire, 0x04030201U);
    error = std::get_if<StatusDecodeError>(&decoded);
    expect(error != nullptr
               && error->code == StatusDecodeErrorCode::phaseError,
           "phase errors have a distinct error code");

    wire[12] = std::byte{0x7f};
    decoded = zzrk::protocol::decodeStatus(wire, 0x04030201U);
    error = std::get_if<StatusDecodeError>(&decoded);
    expect(error != nullptr
               && error->code == StatusDecodeErrorCode::invalidStatus
               && error->rawStatus == 0x7f,
           "unknown status values are rejected and preserved");
}

} // namespace

int main()
{
    testCommandGoldenVector();
    testCommandValidation();
    testUnusedCommandBytesAreZeroed();
    testStatusGoldenVector();
    testInvalidSize();
    testInvalidSignature();
    testTagMismatch();
    testStatusErrors();

    if (failures != 0) {
        std::cerr << failures << " protocol codec test(s) failed\n";
        return 1;
    }
    return 0;
}
