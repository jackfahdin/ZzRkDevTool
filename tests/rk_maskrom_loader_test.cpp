// SPDX-License-Identifier: MIT

#include <zzrk/image/RkMaskromLoader.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

struct ControlCall {
  std::uint16_t requestIndex{};
  std::vector<std::byte> bytes;
};

class FakeTransport final : public zzrk::session::IUsbTransport {
public:
  std::vector<ControlCall> calls;
  std::size_t shortCall{std::numeric_limits<std::size_t>::max()};

  [[nodiscard]] zzrk::session::TransferResult
  bulkOut(std::span<const std::byte>,
          std::chrono::milliseconds) noexcept override {
    return unsupported();
  }

  [[nodiscard]] zzrk::session::TransferResult
  bulkIn(std::span<std::byte>, std::chrono::milliseconds) noexcept override {
    return unsupported();
  }

  [[nodiscard]] zzrk::session::TransferResult
  maskromControlOut(const std::uint16_t requestIndex,
                    const std::span<const std::byte> bytes,
                    std::chrono::milliseconds) noexcept override {
    calls.push_back(ControlCall{
        .requestIndex = requestIndex,
        .bytes = {bytes.begin(), bytes.end()},
    });
    const std::size_t index = calls.size() - 1U;
    return zzrk::session::TransferSuccess{
        .bytesTransferred =
            index == shortCall ? bytes.size() - 1U : bytes.size(),
    };
  }

private:
  [[nodiscard]] static zzrk::session::TransferResult unsupported() noexcept {
    return zzrk::session::TransportError{
        .code = zzrk::session::TransportErrorCode::notSupported,
    };
  }
};

int g_failures{};

void expect(const bool condition, const std::string_view description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
  }
}

struct TemporaryImage {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() / "zzrk-maskrom-loader-test.bin";

  TemporaryImage() {
    std::vector<char> bytes(16U + 3U + 4095U);
    bytes[16] = 1;
    bytes[17] = 2;
    bytes[18] = 3;
    for (std::size_t index = 19; index < bytes.size(); ++index) {
      bytes[index] = static_cast<char>(index & 0xffU);
    }
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  ~TemporaryImage() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
};

zzrk::image::RkFirmwareImage makeImage(const std::filesystem::path &path) {
  zzrk::image::RkFirmwareImage image{};
  image.sourcePath = path;
  image.entries.push_back(zzrk::image::FirmwareEntry{
      .name = "bootloader",
      .path = {},
      .fileOffset = 16,
      .contentSize = 3U + 4095U,
  });
  zzrk::image::FirmwareLoaderInfo loader{};
  loader.code471.push_back(zzrk::image::FirmwareLoaderBootEntry{
      .component = {.relativeOffset = 0, .size = 3},
  });
  loader.code472.push_back(zzrk::image::FirmwareLoaderBootEntry{
      .component = {.relativeOffset = 3, .size = 4095},
  });
  image.loader = std::move(loader);
  return image;
}

void testBootEntriesUseRockchipCrcAndControlChunking() {
  TemporaryImage file;
  auto image = makeImage(file.path);
  FakeTransport transport;
  const auto result = zzrk::image::downloadRkMaskromLoader(transport, image);
  const auto *success = std::get_if<zzrk::image::MaskromLoaderSuccess>(&result);
  expect(success != nullptr && success->entriesDownloaded == 2 &&
             success->bytesDownloaded == 4098,
         "both boot stages report their original payload byte counts");
  expect(transport.calls.size() == 3,
         "a 4095-byte stage is padded and split at 4096 bytes");
  if (transport.calls.size() != 3U) {
    return;
  }
  expect(transport.calls[0].requestIndex == 0x0471 &&
             transport.calls[0].bytes.size() == 5 &&
             transport.calls[0].bytes[0] == std::byte{1} &&
             transport.calls[0].bytes[1] == std::byte{2} &&
             transport.calls[0].bytes[2] == std::byte{3} &&
             transport.calls[0].bytes[3] == std::byte{0xad} &&
             transport.calls[0].bytes[4] == std::byte{0xad},
         "the 0x471 payload has the exact Rockchip CRC-CCITT trailer");
  expect(transport.calls[1].requestIndex == 0x0472 &&
             transport.calls[1].bytes.size() == 4096 &&
             transport.calls[1].bytes.back() == std::byte{0},
         "the 4095-byte 0x472 payload receives one zero padding byte");
  expect(transport.calls[2].requestIndex == 0x0472 &&
             transport.calls[2].bytes.size() == 2,
         "the padded 0x472 CRC trailer is sent in its final chunk");
}

void testShortControlTransferFailsClosed() {
  TemporaryImage file;
  auto image = makeImage(file.path);
  FakeTransport transport;
  transport.shortCall = 0;
  const auto result = zzrk::image::downloadRkMaskromLoader(transport, image);
  const auto *error = std::get_if<zzrk::image::MaskromLoaderError>(&result);
  expect(error != nullptr &&
             error->code ==
                 zzrk::image::MaskromLoaderErrorCode::shortTransfer &&
             error->stage == "DDR code" && transport.calls.size() == 1,
         "a short Maskrom control transfer stops before the next stage");
}

} // namespace

int main() {
  testBootEntriesUseRockchipCrcAndControlChunking();
  testShortControlTransferFailsClosed();
  return g_failures == 0 ? 0 : 1;
}
