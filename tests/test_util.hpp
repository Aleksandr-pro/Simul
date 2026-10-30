// Минимальный тестовый каркас без внешних зависимостей.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int f = 0;
    return f;
}

inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int runAll() {
    const char* filter = std::getenv("TEST_FILTER");
    for (const Case& c : registry()) {
        if (filter && std::string(c.name).find(filter) == std::string::npos) continue;
        const int before = failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (failures() == before) {
            std::printf("[  OK  ] %s\n", c.name);
        } else {
            std::printf("[ FAIL ] %s\n", c.name);
        }
    }
    std::printf("\n%d проверок, %d ошибок\n", checks(), failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace test

#define TEST(name)                                        \
    static void test_##name();                            \
    static ::test::Registrar registrar_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++::test::checks();                                                      \
        if (!(cond)) {                                                           \
            ++::test::failures();                                                \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                         \
    do {                                                                                       \
        ++::test::checks();                                                                    \
        const auto va = (a);                                                                   \
        const auto vb = (b);                                                                   \
        if (!(va == vb)) {                                                                     \
            ++::test::failures();                                                              \
            std::printf("  FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);               \
        }                                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                  \
    do {                                                                                       \
        ++::test::checks();                                                                    \
        const double va = static_cast<double>(a);                                              \
        const double vb = static_cast<double>(b);                                              \
        if (std::abs(va - vb) > (eps)) {                                                       \
            ++::test::failures();                                                              \
            std::printf("  FAIL %s:%d  %s ~ %s (%.4f vs %.4f)\n", __FILE__, __LINE__, #a, #b,  \
                        va, vb);                                                               \
        }                                                                                      \
    } while (0)
