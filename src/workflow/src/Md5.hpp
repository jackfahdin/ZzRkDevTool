// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zzrk::workflow {

class Md5 final {
  public:
    Md5() noexcept;

    void update(const void* data, std::size_t size) noexcept;
    [[nodiscard]] std::array<char, 33> finish() noexcept;

  private:
    void processBlock(const std::uint8_t* block) noexcept;
    void appendRaw(const std::uint8_t* data, std::size_t size) noexcept;

    std::uint32_t state_[4];
    std::uint64_t length_{};
    std::uint8_t buffer_[64]{};
    std::size_t bufferSize_{};
    bool finished_{};
};

} // namespace zzrk::workflow
