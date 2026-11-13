// A beta build's end (docs/beta-release.md): tools/package_beta.py builds the editor with the date it stops working
// (AIGE_BETA_EXPIRES, today + 30 days); a build without one (every build of your own) never expires.
#pragma once

#include <string>

namespace aige {

// The last day a beta build works, "YYYY-MM-DD"; "" in a build that isn't a beta.
std::string BetaExpires();

// Why this beta can't be used any more ("" while it can). The day is the later of the clock's and the latest seen on
// this computer (%LOCALAPPDATA%/aige/beta-seen), so setting the clock back doesn't bring an expired beta back.
std::string BetaExpired();

} // namespace aige
