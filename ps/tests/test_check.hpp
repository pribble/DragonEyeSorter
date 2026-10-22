// test_check.hpp — the shared CHECK macro + pass/fail counters of ps/tests/test_*.cpp.
//
// Adoption is output-neutral: include this header and delete the local copy of the two counters and the CHECK macro
// (test_scheduler.cpp:18-24 is the richest one; test_scheduler / test_modbus / test_calib / test_measure all use
// exactly this format). A passing check stays silent and a failure prints exactly
//     FAIL <file>:<line>: <message>
// so existing logs and the last "N passed, M failed" line do not change. Do NOT keep a local g_pass / g_fail next to
// this header: they would redeclare the ones below.
//
//   int main()
//   {
//       CHECK(cond, "message %d", x);        // silent on success, counts one check
//       ...
//       return test_summary();               // prints "%d passed, %d failed\n", returns the failure count
//   }
//
// test_modbus prefixes the program name ("test_modbus: %d passed..."), so use test_summary("test_modbus") there.
#pragma once

#include <cstdio>

inline int g_pass = 0;   // C++17 inline: one instance for the whole test binary
inline int g_fail = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) g_pass++;                                           \
        else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
               printf(__VA_ARGS__); printf("\n"); }                   \
    } while (0)

// Configurable summary: prog = nullptr for the plain "%d passed, %d failed" line, else "<prog>: %d passed, ...".
inline int test_summary(const char *prog = nullptr)
{
    if (prog && prog[0]) printf("%s: %d passed, %d failed\n", prog, g_pass, g_fail);
    else printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
