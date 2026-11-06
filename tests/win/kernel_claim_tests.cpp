// Windows only: one Ampify kernel per process, whichever folder each copy was built in.
#include <windows.h>

#include "../../src/win/win_common.hpp"
#include "../test.hpp"

// Two launchers built in different folders each load their own kernel into the same process. Only the first one may
// run there; the second must find the process taken. An id with the top bit set is no real process, and carrying this
// run's id keeps two runs of the suite at once apart.
TEST_CASE(WinCommon_OnlyTheFirstKernelClaimsAProcess) {
    const DWORD kProcess = 0x80000000u | GetCurrentProcessId();
    CHECK(ampify::win::ClaimKernelOfProcess(kProcess));
    CHECK(!ampify::win::ClaimKernelOfProcess(kProcess));
}
