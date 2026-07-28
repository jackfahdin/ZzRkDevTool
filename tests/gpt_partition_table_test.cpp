// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/storage/GptPartitionTable.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t kSectorSize = 512;
constexpr std::size_t kEntryCount = 128;
constexpr std::size_t kEntrySize = 128;

int failures = 0;

void expect(const bool condition, const std::string_view description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

constexpr void writeLe16(const std::span<std::byte> bytes,
                         const std::size_t offset,
                         const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

constexpr void writeLe32(const std::span<std::byte> bytes,
                         const std::size_t offset,
                         const std::uint32_t value) {
  for (std::size_t index = 0; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

constexpr void writeLe64(const std::span<std::byte> bytes,
                         const std::size_t offset,
                         const std::uint64_t value) {
  for (std::size_t index = 0; index < 8U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::uint32_t crc32(const std::span<const std::byte> bytes) {
  std::uint32_t crc = 0xffffffffU;
  for (const auto byte : bytes) {
    crc ^= std::to_integer<std::uint8_t>(byte);
    for (unsigned int bit = 0; bit < 8U; ++bit) {
      const auto mask = static_cast<std::uint32_t>(0U - (crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

struct GptFixture {
  std::array<std::byte, kSectorSize> header{};
  std::array<std::byte, kEntryCount * kEntrySize> entries{};
};

class FakeTransport final : public zzrk::session::IUsbTransport {
public:
  void queueRead(const std::span<const std::byte> payload,
                 const std::uint32_t tag) {
    inputs_.emplace_back(payload.begin(), payload.end());
    zzrk::protocol::RockusbStatusBytes status{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53}};
    writeLe32(status, 4U, tag);
    inputs_.emplace_back(status.begin(), status.end());
  }

  [[nodiscard]] zzrk::session::TransferResult
  bulkOut(const std::span<const std::byte> bytes,
          std::chrono::milliseconds) noexcept override {
    if (bytes.size() != zzrk::protocol::kRockusbCommandSize) {
      mismatch_ = true;
      return zzrk::session::TransportError{
          zzrk::session::TransportErrorCode::other, -1, 0};
    }
    ++commands_;
    return zzrk::session::TransferSuccess{bytes.size()};
  }

  [[nodiscard]] zzrk::session::TransferResult
  bulkIn(const std::span<std::byte> bytes,
         std::chrono::milliseconds) noexcept override {
    if (nextInput_ >= inputs_.size() ||
        inputs_[nextInput_].size() != bytes.size()) {
      mismatch_ = true;
      return zzrk::session::TransportError{
          zzrk::session::TransportErrorCode::other, -2, 0};
    }
    std::copy(inputs_[nextInput_].begin(), inputs_[nextInput_].end(),
              bytes.begin());
    ++nextInput_;
    return zzrk::session::TransferSuccess{bytes.size()};
  }

  [[nodiscard]] bool complete() const noexcept {
    return !mismatch_ && commands_ == 3U && nextInput_ == inputs_.size();
  }

private:
  std::vector<std::vector<std::byte>> inputs_;
  std::size_t nextInput_{};
  std::size_t commands_{};
  bool mismatch_{};
};

void finishCrcs(GptFixture &fixture) {
  writeLe32(fixture.entries, 0, 0xaabbccddU);
  writeLe32(fixture.header, 88, crc32(fixture.entries));
  writeLe32(fixture.header, 16, 0U);
  writeLe32(fixture.header, 16,
            crc32(std::span{fixture.header}.first(92U)));
}

GptFixture makeFixture() {
  GptFixture fixture;
  constexpr std::array signature{std::byte{'E'}, std::byte{'F'},
                                 std::byte{'I'}, std::byte{' '},
                                 std::byte{'P'}, std::byte{'A'},
                                 std::byte{'R'}, std::byte{'T'}};
  std::copy(signature.begin(), signature.end(), fixture.header.begin());
  writeLe32(fixture.header, 8, 0x00010000U);
  writeLe32(fixture.header, 12, 92U);
  writeLe64(fixture.header, 24, 1U);
  writeLe64(fixture.header, 32, 4095U);
  writeLe64(fixture.header, 40, 34U);
  writeLe64(fixture.header, 48, 4062U);
  writeLe32(fixture.header, 56, 0x12345678U);
  writeLe64(fixture.header, 72, 2U);
  writeLe32(fixture.header, 80, kEntryCount);
  writeLe32(fixture.header, 84, kEntrySize);

  auto entry = std::span{fixture.entries}.first(kEntrySize);
  writeLe32(entry, 0, 0xaabbccddU);
  writeLe32(entry, 16, 0x11223344U);
  writeLe64(entry, 32, 64U);
  writeLe64(entry, 40, 127U);
  constexpr std::string_view name = "boot_a";
  for (std::size_t index = 0; index < name.size(); ++index) {
    writeLe16(entry, 56U + index * 2U,
              static_cast<std::uint8_t>(name[index]));
  }
  finishCrcs(fixture);
  return fixture;
}

const zzrk::storage::GptError *getError(
    const zzrk::storage::GptPartitionTableResult &result,
    const std::string_view description) {
  const auto *error = std::get_if<zzrk::storage::GptError>(&result);
  expect(error != nullptr, description);
  return error;
}

void testValidPrimaryGpt() {
  const auto fixture = makeFixture();
  const auto result =
      zzrk::storage::parseGptCopy(fixture.header, fixture.entries, 1U, 4096U);
  const auto *table =
      std::get_if<zzrk::storage::GptPartitionTable>(&result);
  expect(table != nullptr, "a valid primary GPT is accepted");
  if (table == nullptr) {
    return;
  }
  expect(table->partitions.size() == 1U,
         "only used GPT entries become typed partitions");
  expect(table->partitions.front().name == "boot_a",
         "the UTF-16LE GPT name is decoded");
  expect(table->partitions.front().firstLba == 64U &&
             table->partitions.front().sectorCount() == 64U,
         "the typed GPT partition has an exact inclusive range");
}

void testHeaderCrcIsMandatory() {
  auto fixture = makeFixture();
  fixture.header[20] = std::byte{0x01};
  const auto result =
      zzrk::storage::parseGptCopy(fixture.header, fixture.entries, 1U, 4096U);
  const auto *error = getError(result, "a corrupt GPT header is rejected");
  expect(error != nullptr &&
             error->code == zzrk::storage::GptErrorCode::invalidHeaderCrc,
         "header corruption is reported as a CRC failure");
}

void testEntryArrayCrcIsMandatory() {
  auto fixture = makeFixture();
  fixture.entries[1000] = std::byte{0x01};
  const auto result =
      zzrk::storage::parseGptCopy(fixture.header, fixture.entries, 1U, 4096U);
  const auto *error = getError(result, "a corrupt GPT entry array is rejected");
  expect(error != nullptr &&
             error->code ==
                 zzrk::storage::GptErrorCode::invalidEntryArrayCrc,
         "entry corruption is reported as an entry-array CRC failure");
}

void testPartitionMustStayInsideUsableRange() {
  auto fixture = makeFixture();
  writeLe64(fixture.entries, 40, 4090U);
  finishCrcs(fixture);
  const auto result =
      zzrk::storage::parseGptCopy(fixture.header, fixture.entries, 1U, 4096U);
  const auto *error = getError(result, "an out-of-range partition is rejected");
  expect(error != nullptr &&
             error->code ==
                 zzrk::storage::GptErrorCode::invalidPartitionRange,
         "partition bounds are validated after both GPT CRCs");
}

void testOpaqueBackupDoesNotInvalidatePrimary() {
  const auto fixture = makeFixture();
  FakeTransport transport;
  transport.queueRead(fixture.header, 1U);
  transport.queueRead(fixture.entries, 2U);
  std::array<std::byte, kSectorSize> opaqueBackup{};
  std::fill(opaqueBackup.begin(), opaqueBackup.end(), std::byte{0xcc});
  transport.queueRead(opaqueBackup, 3U);
  zzrk::session::RockusbSession session{transport};
  const auto result = zzrk::storage::readDeviceGpt(session, 4096U, true);
  const auto *table =
      std::get_if<zzrk::storage::GptPartitionTable>(&result);
  expect(table != nullptr && table->partitions.size() == 1U,
         "an opaque backup read preserves the CRC-valid primary GPT");
  expect(table != nullptr &&
             table->backupStatus ==
                 zzrk::storage::BackupGptStatus::unavailable &&
             table->backupError ==
                 zzrk::storage::GptErrorCode::invalidSignature,
         "an opaque disk tail is explicitly reported as backup unavailable");
  expect(transport.complete(),
         "primary header, primary entries, and backup header use three framed reads");
}

} // namespace

int main() {
  testValidPrimaryGpt();
  testHeaderCrcIsMandatory();
  testEntryArrayCrcIsMandatory();
  testPartitionMustStayInsideUsableRange();
  testOpaqueBackupDoesNotInvalidatePrimary();
  return failures == 0 ? 0 : 1;
}
