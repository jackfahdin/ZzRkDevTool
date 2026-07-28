// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string_view>

namespace zzrk::cli {

void writeJsonString(std::string_view value, std::ostream &output);
void writeJsonBytes(std::span<const std::byte> bytes, std::ostream &output);

} // namespace zzrk::cli
