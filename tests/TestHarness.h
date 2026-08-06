#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace rpa::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct AssertionFailure {
    std::string message;
};

inline void fail(const std::string& file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ":" << line << "  " << message;
    throw AssertionFailure{out.str()};
}

inline int runAll() {
    int failed = 0;
    for (const auto& testCase : registry()) {
        try {
            testCase.fn();
            std::cout << "[  ok  ] " << testCase.name << "\n";
        } catch (const AssertionFailure& failure) {
            std::cout << "[ FAIL ] " << testCase.name << "\n         " << failure.message << "\n";
            ++failed;
        } catch (const std::exception& error) {
            std::cout << "[ FAIL ] " << testCase.name << "\n         unexpected exception: "
                      << error.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << (registry().size() - failed) << "/" << registry().size()
              << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace rpa::test

#define RPA_TEST(name)                                                            \
    static void name();                                                           \
    static ::rpa::test::Registrar registrar_##name(#name, name);                  \
    static void name()

#define CHECK(expr)                                                               \
    do {                                                                          \
        if (!(expr)) ::rpa::test::fail(__FILE__, __LINE__, "CHECK failed: " #expr); \
    } while (0)

/// Both operands are copied rather than bound by reference: expressions like
/// `opt.value()` return a reference into a temporary optional, and a reference
/// binding here would dangle before the comparison runs.
#define CHECK_EQ(actual, expected)                                                \
    do {                                                                          \
        const auto a_ = (actual);                                                 \
        const auto e_ = (expected);                                               \
        if (!(a_ == e_)) {                                                        \
            std::ostringstream msg_;                                              \
            msg_ << "CHECK_EQ failed: " #actual " == " #expected                  \
                 << "\n           actual:   " << a_                               \
                 << "\n           expected: " << e_;                              \
            ::rpa::test::fail(__FILE__, __LINE__, msg_.str());                    \
        }                                                                         \
    } while (0)
