// SPDX-License-Identifier: MIT

#include <zzrk/image/RkMaskromLoader.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace zzrk::image {
namespace {

using namespace std::chrono_literals;

constexpr std::size_t kControlChunkSize = 4096U;
constexpr auto kControlTimeout = 5s;

[[nodiscard]] MaskromLoaderError
makeError(const MaskromLoaderErrorCode code, std::string stage,
          const std::uint64_t completedBytes = 0,
          std::optional<session::TransportError> transportError = {}) {
  return MaskromLoaderError{
      .code = code,
      .stage = std::move(stage),
      .completedBytes = completedBytes,
      .transportError = std::move(transportError),
  };
}

[[nodiscard]] std::uint16_t
crcCcitt(const std::span<const std::byte> bytes) noexcept {
  std::uint16_t crc = 0xffffU;
  for (const auto byte : bytes) {
    const auto value = std::to_integer<std::uint8_t>(byte);
    for (std::uint16_t mask = 0x80U; mask != 0U; mask >>= 1U) {
      const bool highBit = (crc & 0x8000U) != 0U;
      crc = static_cast<std::uint16_t>(crc << 1U);
      if (highBit) {
        crc ^= 0x1021U;
      }
      if ((value & mask) != 0U) {
        crc ^= 0x1021U;
      }
    }
  }
  return crc;
}

[[nodiscard]] std::optional<MaskromLoaderError>
sendEntry(session::IUsbTransport &transport, std::ifstream &stream,
          const std::uint64_t loaderOffset,
          const FirmwareLoaderBootEntry &entry,
          const std::uint16_t requestIndex, const std::string_view stage,
          const std::uint64_t completedBefore,
          const MaskromLoaderProgressCallback &progress,
          const MaskromLoaderCancellationCallback &cancellation) {
  if (entry.component.relativeOffset >
      std::numeric_limits<std::uint64_t>::max() - loaderOffset) {
    return makeError(MaskromLoaderErrorCode::invalidImage, std::string{stage},
                     completedBefore);
  }
  std::vector<std::byte> payload(
      static_cast<std::size_t>(entry.component.size) + 5U);
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(loaderOffset +
                                           entry.component.relativeOffset),
               std::ios::beg);
  if (!stream) {
    return makeError(MaskromLoaderErrorCode::imageSeekFailed,
                     std::string{stage}, completedBefore);
  }
  stream.read(reinterpret_cast<char *>(payload.data()),
              static_cast<std::streamsize>(entry.component.size));
  if (stream.gcount() != static_cast<std::streamsize>(entry.component.size) ||
      !stream) {
    return makeError(MaskromLoaderErrorCode::imageReadFailed,
                     std::string{stage}, completedBefore);
  }

  std::size_t crcSize = entry.component.size;
  bool sendPendingPacket = false;
  switch (crcSize % kControlChunkSize) {
  case kControlChunkSize - 1U:
    ++crcSize;
    break;
  case kControlChunkSize - 2U:
    sendPendingPacket = true;
    break;
  default:
    break;
  }
  const std::uint16_t crc =
      crcCcitt(std::span<const std::byte>{payload}.first(crcSize));
  payload[crcSize] = static_cast<std::byte>(crc >> 8U);
  payload[crcSize + 1U] = static_cast<std::byte>(crc & 0xffU);
  const std::size_t transferSize = crcSize + 2U;

  if (progress) {
    progress(stage, 0, entry.component.size);
  }
  std::size_t transferred = 0;
  while (transferred < transferSize) {
    if (cancellation && cancellation()) {
      return makeError(MaskromLoaderErrorCode::cancelled, std::string{stage},
                       completedBefore + transferred);
    }
    const std::size_t chunkSize =
        std::min(kControlChunkSize, transferSize - transferred);
    const auto chunk =
        std::span<const std::byte>{payload}.subspan(transferred, chunkSize);
    const auto result =
        transport.maskromControlOut(requestIndex, chunk, kControlTimeout);
    if (const auto *error = std::get_if<session::TransportError>(&result)) {
      return makeError(MaskromLoaderErrorCode::transferFailed,
                       std::string{stage}, completedBefore + transferred,
                       *error);
    }
    if (std::get<session::TransferSuccess>(result).bytesTransferred !=
        chunkSize) {
      return makeError(MaskromLoaderErrorCode::shortTransfer,
                       std::string{stage}, completedBefore + transferred);
    }
    transferred += chunkSize;
    if (progress) {
      progress(stage,
               std::min<std::uint64_t>(transferred, entry.component.size),
               entry.component.size);
    }
  }

  if (sendPendingPacket) {
    constexpr std::array pending{std::byte{0}};
    const auto result =
        transport.maskromControlOut(requestIndex, pending, kControlTimeout);
    if (const auto *error = std::get_if<session::TransportError>(&result)) {
      return makeError(MaskromLoaderErrorCode::transferFailed,
                       std::string{stage},
                       completedBefore + entry.component.size, *error);
    }
    const auto count =
        std::get<session::TransferSuccess>(result).bytesTransferred;
    if (count != 0U && count != pending.size()) {
      return makeError(MaskromLoaderErrorCode::shortTransfer,
                       std::string{stage},
                       completedBefore + entry.component.size);
    }
  }

  if (entry.delayMilliseconds != 0U) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds{entry.delayMilliseconds});
  }
  if (progress) {
    progress(stage, entry.component.size, entry.component.size);
  }
  return std::nullopt;
}

} // namespace

MaskromLoaderResult
downloadRkMaskromLoader(session::IUsbTransport &transport,
                        const RkFirmwareImage &image,
                        const MaskromLoaderProgressCallback &progress,
                        const MaskromLoaderCancellationCallback &cancellation) {
  if (!image.loader.has_value()) {
    return makeError(MaskromLoaderErrorCode::invalidImage, "Loader");
  }
  const auto loaderEntry = std::ranges::find(
      image.entries, std::string{"bootloader"}, &FirmwareEntry::name);
  if (loaderEntry == image.entries.end() || image.loader->code471.empty() ||
      image.loader->code472.empty()) {
    return makeError(MaskromLoaderErrorCode::invalidImage, "Loader");
  }

  return downloadRkMaskromLoader(
      transport,
      RkLoaderImage{
          .source = FirmwarePayloadSource{
              .sourcePath = image.sourcePath,
              .fileOffset = loaderEntry->fileOffset,
              .contentSize = loaderEntry->contentSize,
              .logicalSize = loaderEntry->logicalSize,
              .format = PayloadFormat::raw,
          },
          .info = *image.loader,
      },
      progress, cancellation);
}

MaskromLoaderResult
downloadRkMaskromLoader(session::IUsbTransport &transport,
                        const RkLoaderImage &loader,
                        const MaskromLoaderProgressCallback &progress,
                        const MaskromLoaderCancellationCallback &cancellation) {
  if (loader.source.contentSize == 0U || loader.info.code471.empty() ||
      loader.info.code472.empty()) {
    return makeError(MaskromLoaderErrorCode::invalidImage, "Loader");
  }

  std::ifstream stream{loader.source.sourcePath, std::ios::binary};
  if (!stream) {
    return makeError(MaskromLoaderErrorCode::imageOpenFailed, "Loader");
  }

  MaskromLoaderSuccess success{};
  const auto downloadEntries =
      [&](const auto &entries, const std::uint16_t requestIndex,
          const std::string_view stage) -> std::optional<MaskromLoaderError> {
    for (const auto &entry : entries) {
      if (auto error = sendEntry(
              transport, stream, loader.source.fileOffset, entry, requestIndex,
              stage, success.bytesDownloaded, progress, cancellation)) {
        return error;
      }
      ++success.entriesDownloaded;
      success.bytesDownloaded += entry.component.size;
    }
    return std::nullopt;
  };

  if (auto error =
          downloadEntries(loader.info.code471, 0x0471U, "DDR code")) {
    return *error;
  }
  if (auto error =
          downloadEntries(loader.info.code472, 0x0472U, "usbplug code")) {
    return *error;
  }
  return success;
}

} // namespace zzrk::image
