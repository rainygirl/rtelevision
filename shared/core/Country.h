// ISO 3166-1 alpha-2 country codes: flag emoji and English name.
// Lives in the core so every front end labels channels the same way.
#pragma once

#include <string>

namespace tv {

// Returns the flag emoji for a two-letter code, or "" when the code is not a
// usable country code. Handles the non-ISO codes iptv-org uses (uk, el, ...).
std::string countryFlagEmoji(const std::string& code);

// Returns the English country name, or the uppercased code when it is unknown.
std::string countryName(const std::string& code);

}  // namespace tv
