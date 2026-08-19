#pragma once
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Mini framework de tests auto-enregistrés (sans dépendance externe : le
// build doit rester possible hors-ligne / dans un environnement fermé).
// Pour une suite de tests plus riche (fixtures paramétrées, mocks...),
// migrer vers Catch2/GoogleTest est prévu en Phase 6 (voir ARCHITECTURE.md,
// section Tests).

namespace vsm::test {

struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct AssertionFailure : std::exception {
    std::string message;
    explicit AssertionFailure(std::string m) : message(std::move(m)) {}
    const char* what() const noexcept override { return message.c_str(); }
};

inline int runAll() {
    int passed = 0, failed = 0;
    for (auto& tc : registry()) {
        try {
            tc.fn();
            std::cout << "[PASS] " << tc.name << "\n";
            ++passed;
        } catch (const AssertionFailure& e) {
            std::cout << "[FAIL] " << tc.name << " - " << e.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << tc.name << " - exception inattendue: " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " réussis, " << failed << " échoués (" << (passed + failed) << " au total)\n";
    return failed == 0 ? 0 : 1;
}

} // namespace vsm::test

#define VSM_TEST(name)                                                          \
    void name();                                                                \
    static vsm::test::Registrar registrar_##name(#name, name);                  \
    void name()

#define VSM_ASSERT(cond)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::ostringstream oss;                                            \
            oss << "assertion échouée: " #cond " (" << __FILE__ << ":" << __LINE__ << ")"; \
            throw vsm::test::AssertionFailure(oss.str());                       \
        }                                                                        \
    } while (0)

// Les variables internes portent un préfixe vsmAssert_ : avec des noms
// courants comme `va`/`vb`, toute variable de test du même nom déclenchait un
// -Wshadow (que ce projet traite comme une erreur à corriger, pas à ignorer),
// et l'avertissement pointait vers le framework plutôt que vers le test.
#define VSM_ASSERT_EQ(a, b)                                                     \
    do {                                                                        \
        auto vsmAssert_a = (a); auto vsmAssert_b = (b);                        \
        if (!(vsmAssert_a == vsmAssert_b)) {                                    \
            std::ostringstream oss;                                            \
            oss << "assertion échouée: " #a " == " #b " (" << vsmAssert_a       \
                << " != " << vsmAssert_b                                        \
                << ") (" << __FILE__ << ":" << __LINE__ << ")";                \
            throw vsm::test::AssertionFailure(oss.str());                       \
        }                                                                        \
    } while (0)

#define VSM_ASSERT_NEAR(a, b, eps)                                              \
    do {                                                                        \
        double vsmAssert_a = static_cast<double>(a);                            \
        double vsmAssert_b = static_cast<double>(b);                            \
        double vsmAssert_diff = vsmAssert_a > vsmAssert_b ? vsmAssert_a - vsmAssert_b \
                                                          : vsmAssert_b - vsmAssert_a; \
        if (!(vsmAssert_diff <= (eps))) {                                       \
            std::ostringstream oss;                                            \
            oss << "assertion échouée: " #a " ~= " #b " a " << eps << " pres (" \
                << vsmAssert_a << " vs " << vsmAssert_b << ") ("                \
                << __FILE__ << ":" << __LINE__ << ")";                          \
            throw vsm::test::AssertionFailure(oss.str());                       \
        }                                                                        \
    } while (0)
