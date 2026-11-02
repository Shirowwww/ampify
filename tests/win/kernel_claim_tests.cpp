// Windows only: one Ampify kernel per process, whichever folder each copy was built in.
#include <windows.h>

#include "../../src/win/win_common.hpp"
#include "../test.hpp"

// Two launchers built in different folders each load their own kernel into the same process. Only the first one may
// run there; the second must find the process taken. A process id no test runs as keeps the real one free.
TEST_CASE(WinCommon_OnlyTheFirstKernelClaimsAProcess) {
    constexpr DWORD kProcess = 0xFFFFFFF0;
    CHECK(ampify::win::ClaimKernelOfProcess(kProcess));
    CHECK(!ampify::win::ClaimKernelOfProcess(kProcess));
}
