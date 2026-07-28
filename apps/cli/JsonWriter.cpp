// SPDX-License-Identifier: MIT

#include "JsonWriter.hpp"

#include <array>
#include <ostream>

namespace zzrk::cli {

void writeJsonString(const std::string_view value, std::ostream &output) {
  constexpr std::array hexDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  output << '"';
  for (const char rawCharacter : value) {
    const auto character = static_cast<unsigned char>(rawCharacter);
    switch (character) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character < 0x20U) {
        output << "\\u00" << hexDigits[(character >> 4U) & 0x0fU]
               << hexDigits[character & 0x0fU];
      } else {
        output << static_cast<char>(character);
      }
      break;
    }
  }
  output << '"';
}

void writeJsonBytes(const std::span<const std::byte> bytes,
                    std::ostream &output) {
  constexpr std::array hexDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  output << '"';
  for (const auto value : bytes) {
    const auto byte = std::to_integer<unsigned int>(value);
    output << hexDigits[(byte >> 4U) & 0x0fU] << hexDigits[byte & 0x0fU];
  }
  output << '"';
}

} // namespace zzrk::cli
