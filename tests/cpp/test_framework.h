// Micro-framework de tests sin dependencias (tambien valido como testbench de
// C-simulation en Vitis HLS).
#ifndef AURORA_TEST_FRAMEWORK_H
#define AURORA_TEST_FRAMEWORK_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tf {

struct Case {
    std::string name;
    std::function<void()> fn;
    bool expect_fail;   // true => documenta un fallo conocido (xfail)
};

inline std::vector<Case> &registry() { static std::vector<Case> r; return r; }
inline int &failures() { static int f = 0; return f; }
inline std::string &detail() { static std::string d; return d; }

struct Reg {
    Reg(const char *n, std::function<void()> f, bool xf) { registry().push_back({n, f, xf}); }
};

inline void check(bool cond, const char *expr, const char *file, int line, const std::string &msg) {
    if (cond) return;
    ++failures();
    char buf[512];
    std::snprintf(buf, sizeof buf, "      %s:%d: CHECK(%s) %s\n", file, line, expr, msg.c_str());
    detail() += buf;
}

// Ejecuta todos los tests. Devuelve 0 si todos los normales pasan y todos los
// xfail siguen fallando (si un xfail pasa, se avisa: el bug se ha corregido).
inline int run_all(const char *suite) {
    int pass = 0, fail = 0, xfail = 0, xpass = 0;
    std::printf("==== %s ====\n", suite);
    for (auto &c : registry()) {
        failures() = 0;
        detail().clear();
        c.fn();
        bool ok = failures() == 0;
        const char *tag;
        if (!c.expect_fail) { tag = ok ? "PASS " : "FAIL "; ok ? ++pass : ++fail; }
        else { tag = ok ? "XPASS" : "XFAIL"; ok ? ++xpass : ++xfail; }
        std::printf("[%s] %s\n", tag, c.name.c_str());
        if (!ok || c.expect_fail) std::printf("%s", detail().c_str());
    }
    std::printf("---- %d pass, %d fail, %d xfail (fallos conocidos documentados), %d xpass\n",
                pass, fail, xfail, xpass);
    return (fail == 0 && xpass == 0) ? 0 : 1;
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)
#define TEST(name) \
    static void name(); \
    static tf::Reg TF_CAT(reg_, name)(#name, name, false); \
    static void name()
#define KNOWN_BUG(name) \
    static void name(); \
    static tf::Reg TF_CAT(reg_, name)(#name, name, true); \
    static void name()
#define CHECK(c) tf::check((c), #c, __FILE__, __LINE__, "")
#define CHECK_MSG(c, m) tf::check((c), #c, __FILE__, __LINE__, (m))

#endif
