#pragma once
// turnaus#1793: how a delivered dart's answer is said. Its own header, with nothing but
// <string> under it, so testers/i1793_round_check.cpp holds the rule without the HTTP
// client's dependencies.

#include <string>

/**
 * turnaus#1793: whether a delivered dart's answer is said at INFO. A DROPPED answer is
 * ordinary under OD_PAST_THREE=on -- a board that pushes every arrival past three meets it
 * whenever the round in hand already holds three (turnaus#1281) -- so it is said once per
 * round and the rest go to DEBUG. Never retried and never a delivery fault either way: it
 * is a 202, and deliver() settles it. With the switch off every answer is said, as before.
 */
inline bool droppedAnswerIsSaid(const std::string &outcome, bool said_this_round, bool switch_on)
{
    return !switch_on || outcome != "DROPPED" || !said_this_round;
}
