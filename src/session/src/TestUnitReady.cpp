// SPDX-License-Identifier: MIT

#include <zzrk/session/TestUnitReady.hpp>

#include "RockusbSessionExecutor.hpp"

namespace zzrk::session {

SessionResult testUnitReady(RockusbSession &session) noexcept {
  detail::RockusbNoDataRequest request{
      .logicalUnit = 0,
      .commandLength = 6,
      .direction = protocol::DataDirection::deviceToHost,
      .allowNonZeroResidue = true,
  };
  request.commandBlock[0] = std::byte{0x00};
  return detail::RockusbSessionExecutor::executeNoData(session, request);
}

} // namespace zzrk::session
