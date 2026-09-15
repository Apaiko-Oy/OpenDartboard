#pragma once
// #1257: the one place the default Turnaus address is written.
//
// A board with no --turnaus, no OD_TURNAUS_URL and no base_url in its credential file
// posts here. Until #1257 the fallback named a domain that is not Turnaus' - it is
// parked and for sale - so an unconfigured board would have sent a live pairing code to
// whoever owns it, and accepted whatever credential came back. The default is production.
//
// main.cpp resolves the address and debug.hpp's --help names the default; both read this
// constant, and testers/check_default_address.sh fails the tree if a second copy appears.

namespace turnaus_address
{
    constexpr const char *kDefault = "https://turnaus.apaiko.fi";
}
