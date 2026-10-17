// turnaus#1886: a stand-in for <windows.h>, for testers/i1886_check.cpp only.
//
// keep_awake.hpp's Windows branch is five lines around SetThreadExecutionState, and the
// container that runs the testers has no Windows SDK. This header gives that branch the
// three constants and the one function it names, with the SDK's values, and records every
// call so the check can say what the detector asked Windows for and when it let go.
#pragma once
#include <vector>

typedef unsigned long EXECUTION_STATE;
#define ES_SYSTEM_REQUIRED ((EXECUTION_STATE)0x00000001)
#define ES_DISPLAY_REQUIRED ((EXECUTION_STATE)0x00000002)
#define ES_AWAYMODE_REQUIRED ((EXECUTION_STATE)0x00000040)
#define ES_CONTINUOUS ((EXECUTION_STATE)0x80000000)

namespace fake_windows
{
    inline std::vector<EXECUTION_STATE> &calls()
    {
        static std::vector<EXECUTION_STATE> v;
        return v;
    }
    // What the next call answers; the real function answers the previous state, 0 on failure.
    inline EXECUTION_STATE &answer()
    {
        static EXECUTION_STATE a = ES_CONTINUOUS;
        return a;
    }
}

inline EXECUTION_STATE SetThreadExecutionState(EXECUTION_STATE flags)
{
    fake_windows::calls().push_back(flags);
    return fake_windows::answer();
}
