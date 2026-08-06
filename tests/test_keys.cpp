#include "TestHarness.h"

#include "rpa/core/Input.h"

using namespace rpa::core;

RPA_TEST(parses_a_bare_key) {
    std::vector<std::string> modifiers;
    std::string key;
    CHECK(parseKeyCombo("enter", modifiers, key));
    CHECK_EQ(modifiers.size(), size_t{0});
    CHECK_EQ(key, std::string{"enter"});
}

RPA_TEST(parses_a_single_modifier) {
    std::vector<std::string> modifiers;
    std::string key;
    CHECK(parseKeyCombo("ctrl+s", modifiers, key));
    CHECK_EQ(modifiers.size(), size_t{1});
    CHECK_EQ(modifiers[0], std::string{"ctrl"});
    CHECK_EQ(key, std::string{"s"});
}

RPA_TEST(parses_stacked_modifiers_case_insensitively) {
    std::vector<std::string> modifiers;
    std::string key;
    CHECK(parseKeyCombo("Ctrl + Shift + R", modifiers, key));
    CHECK_EQ(modifiers.size(), size_t{2});
    CHECK_EQ(modifiers[0], std::string{"ctrl"});
    CHECK_EQ(modifiers[1], std::string{"shift"});
    CHECK_EQ(key, std::string{"r"});
}

RPA_TEST(normalises_control_to_ctrl) {
    std::vector<std::string> modifiers;
    std::string key;
    CHECK(parseKeyCombo("control+c", modifiers, key));
    CHECK_EQ(modifiers[0], std::string{"ctrl"});
}

RPA_TEST(rejects_an_unknown_modifier) {
    std::vector<std::string> modifiers;
    std::string key;
    CHECK(!parseKeyCombo("hyper+x", modifiers, key));
}

RPA_TEST(rejects_an_empty_combo) {
    std::vector<std::string> modifiers;
    std::string key;
    CHECK(!parseKeyCombo("", modifiers, key));
    CHECK(!parseKeyCombo("+++", modifiers, key));
}
