// SPDX-License-Identifier: MIT

#include "Md5.hpp"

#include <algorithm>
#include <cstring>

namespace zzrk::workflow {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU,
    0x4787c62aU, 0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU,
    0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU,
    0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
    0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U,
    0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
    0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
    0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
    0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U, 0xd9d4d039U,
    0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U, 0x432aff97U,
    0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU,
    0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
};

constexpr std::uint32_t kShiftAmounts[64] = {
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U,
};

constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] constexpr std::uint32_t rotateLeft(
    const std::uint32_t value, const std::uint32_t bits) noexcept
{
    return (value << bits) | (value >> (32U - bits));
}

} // namespace

Md5::Md5() noexcept
    : state_{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U}
{
}

void Md5::update(const void* data, const std::size_t size) noexcept
{
    if (finished_ || (data == nullptr && size != 0U)) {
        return;
    }
    length_ += size;
    appendRaw(static_cast<const std::uint8_t*>(data), size);
}

std::array<char, 33> Md5::finish() noexcept
{
    if (!finished_) {
        finished_ = true;
        const std::uint64_t bitLength = length_ * 8U;
        const std::uint8_t mark = 0x80U;
        appendRaw(&mark, 1U);
        const std::uint8_t zero = 0U;
        while (bufferSize_ != 56U) {
            appendRaw(&zero, 1U);
        }
        std::uint8_t lengthBytes[8]{};
        for (std::size_t index = 0; index < 8U; ++index) {
            lengthBytes[index] =
                static_cast<std::uint8_t>(bitLength >> (index * 8U));
        }
        appendRaw(lengthBytes, 8U);
    }

    std::array<char, 33> digest{};
    std::size_t out = 0;
    for (const auto word : state_) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            const auto value =
                static_cast<std::uint8_t>(word >> (byte * 8U));
            digest[out++] = kHexDigits[value >> 4U];
            digest[out++] = kHexDigits[value & 0x0fU];
        }
    }
    return digest;
}

void Md5::appendRaw(const std::uint8_t* data, const std::size_t size) noexcept
{
    std::size_t offset = 0;
    if (bufferSize_ != 0U) {
        const auto take = std::min(64U - bufferSize_, size);
        std::memcpy(buffer_ + bufferSize_, data, take);
        bufferSize_ += take;
        offset += take;
        if (bufferSize_ == 64U) {
            processBlock(buffer_);
            bufferSize_ = 0U;
        }
    }
    while (offset + 64U <= size) {
        processBlock(data + offset);
        offset += 64U;
    }
    if (offset < size) {
        bufferSize_ = size - offset;
        std::memcpy(buffer_, data + offset, bufferSize_);
    }
}

void Md5::processBlock(const std::uint8_t* block) noexcept
{
    std::uint32_t words[16]{};
    for (std::size_t index = 0; index < 16U; ++index) {
        words[index] = static_cast<std::uint32_t>(block[index * 4U]) |
                       (static_cast<std::uint32_t>(block[index * 4U + 1U])
                        << 8U) |
                       (static_cast<std::uint32_t>(block[index * 4U + 2U])
                        << 16U) |
                       (static_cast<std::uint32_t>(block[index * 4U + 3U])
                        << 24U);
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    for (std::size_t step = 0; step < 64U; ++step) {
        std::uint32_t mixer = 0;
        std::size_t wordIndex = 0;
        if (step < 16U) {
            mixer = (b & c) | (~b & d);
            wordIndex = step;
        } else if (step < 32U) {
            mixer = (d & b) | (~d & c);
            wordIndex = (5U * step + 1U) % 16U;
        } else if (step < 48U) {
            mixer = b ^ c ^ d;
            wordIndex = (3U * step + 5U) % 16U;
        } else {
            mixer = c ^ (b | ~d);
            wordIndex = (7U * step) % 16U;
        }
        const std::uint32_t sum =
            a + mixer + kRoundConstants[step] + words[wordIndex];
        a = d;
        d = c;
        c = b;
        b = b + rotateLeft(sum, kShiftAmounts[step]);
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

} // namespace zzrk::workflow
