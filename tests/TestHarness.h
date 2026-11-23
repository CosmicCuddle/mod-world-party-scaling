/*
 * Minimal dependency-free test runner for the World Party Scaling regression suite.
 */

#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace wpstest
{
struct TestCase
{
    char const* group;
    char const* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Counters
{
    unsigned checks = 0;
    unsigned failures = 0;
};

inline Counters& Count()
{
    static Counters counters;
    return counters;
}

struct Registrar
{
    Registrar(char const* group, char const* name, std::function<void()> body)
    {
        Registry().push_back({ group, name, std::move(body) });
    }
};

template<class A, class B>
void CheckEq(A const& actual, B const& expected, char const* actualText, char const* expectedText, char const* file, int line)
{
    ++Count().checks;
    if (actual == expected)
        return;

    ++Count().failures;
    std::ostringstream message;
    message << file << ':' << line << ": CHECK_EQ(" << actualText << ", " << expectedText << ") got " << +actual
            << " expected " << +expected;
    std::fprintf(stderr, "    FAIL %s\n", message.str().c_str());
}

inline void Check(bool ok, char const* text, char const* file, int line)
{
    ++Count().checks;
    if (ok)
        return;

    ++Count().failures;
    std::fprintf(stderr, "    FAIL %s:%d: CHECK(%s)\n", file, line, text);
}

inline void CheckNear(double actual, double expected, double tolerance, char const* text, char const* file, int line)
{
    ++Count().checks;
    if (std::fabs(actual - expected) <= tolerance)
        return;

    ++Count().failures;
    std::fprintf(stderr, "    FAIL %s:%d: CHECK_NEAR(%s) got %.6f expected %.6f (+/- %.6f)\n", file, line, text, actual,
        expected, tolerance);
}
} // namespace wpstest

#define WPS_CAT2(a, b) a##b
#define WPS_CAT(a, b) WPS_CAT2(a, b)
#define TEST(group, name)                                                                                              \
    static void WPS_CAT(test_, name)();                                                                                \
    static ::wpstest::Registrar WPS_CAT(registrar_, name)(group, #name, &WPS_CAT(test_, name));                        \
    static void WPS_CAT(test_, name)()

#define CHECK(cond) ::wpstest::Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) ::wpstest::CheckEq((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tol) ::wpstest::CheckNear((actual), (expected), (tol), #actual, __FILE__, __LINE__)
