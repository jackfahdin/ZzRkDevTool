// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <variant>

namespace zzrk::session {

enum class TransportErrorCode {
    timedOut,
    disconnected,
    accessDenied,
    stalled,
    overflow,
    cancelled,
    busy,
    interrupted,
    notSupported,
    resourceExhausted,
    invalidArgument,
    io,
    other,
};

struct TransportError {
    TransportErrorCode code{TransportErrorCode::other};
    int nativeCode{};
    // A failed transfer may still have changed the USB stream position.
    std::size_t bytesTransferred{};

    friend constexpr bool operator==(const TransportError&,
                                     const TransportError&) = default;
};

struct TransferSuccess {
    std::size_t bytesTransferred{};

    friend constexpr bool operator==(const TransferSuccess&,
                                     const TransferSuccess&) = default;
};

using TransferResult = std::variant<TransferSuccess, TransportError>;

// One call maps to exactly one underlying bulk transfer. Implementations must
// not retry, complete a short transfer, or clear a halt implicitly because the
// next bytes can be a RockUSB status wrapper. Callers provide a positive,
// finite timeout. A success count must not exceed the supplied span; an error
// count reports bytes transferred by that one failed operation.
class IUsbTransport {
  public:
    virtual ~IUsbTransport() = default;

    IUsbTransport(const IUsbTransport&) = delete;
    IUsbTransport& operator=(const IUsbTransport&) = delete;
    IUsbTransport(IUsbTransport&&) = delete;
    IUsbTransport& operator=(IUsbTransport&&) = delete;

    [[nodiscard]] virtual TransferResult
    bulkOut(std::span<const std::byte> bytes,
            std::chrono::milliseconds timeout) noexcept = 0;

    [[nodiscard]] virtual TransferResult
    bulkIn(std::span<std::byte> bytes,
           std::chrono::milliseconds timeout) noexcept = 0;

  protected:
    // Each instance is bound to one claimed device and endpoint pair.
    IUsbTransport() = default;
};

} // namespace zzrk::session
