#include "TestHarness.h"

#include "rpa/core/Variables.h"

using namespace rpa::core;

RPA_TEST(expands_known_placeholders) {
    VariableScope scope({{"user", "alice"}, {"host", "erp.local"}});
    CHECK_EQ(scope.expand("login {{user}}@{{host}}"), std::string{"login alice@erp.local"});
}

RPA_TEST(tolerates_surrounding_whitespace_in_placeholder) {
    VariableScope scope({{"user", "alice"}});
    CHECK_EQ(scope.expand("hi {{  user  }}"), std::string{"hi alice"});
}

RPA_TEST(unknown_placeholder_expands_empty_and_is_reported) {
    VariableScope scope({{"a", "1"}});
    std::vector<std::string> missing;
    CHECK_EQ(scope.expand("{{a}}-{{b}}-{{c}}", &missing), std::string{"1--"});
    CHECK_EQ(missing.size(), size_t{2});
    CHECK_EQ(missing[0], std::string{"b"});
    CHECK_EQ(missing[1], std::string{"c"});
}

RPA_TEST(reports_each_missing_name_once) {
    VariableScope scope;
    std::vector<std::string> missing;
    scope.expand("{{x}} {{x}} {{x}}", &missing);
    CHECK_EQ(missing.size(), size_t{1});
}

RPA_TEST(leaves_unterminated_placeholder_visible) {
    VariableScope scope({{"a", "1"}});
    // Emitting the broken tail verbatim makes the typo obvious in the log
    // rather than silently truncating the rest of the string.
    CHECK_EQ(scope.expand("ok {{a}} then {{oops"), std::string{"ok 1 then {{oops"});
}

RPA_TEST(passes_through_text_without_placeholders) {
    VariableScope scope({{"a", "1"}});
    CHECK_EQ(scope.expand("nothing to see"), std::string{"nothing to see"});
    CHECK_EQ(scope.expand(""), std::string{});
}

RPA_TEST(set_and_get_round_trip) {
    VariableScope scope;
    CHECK(!scope.has("k"));
    scope.set("k", "v");
    CHECK(scope.has("k"));
    CHECK_EQ(scope.get("k").value(), std::string{"v"});
    scope.clear();
    CHECK(!scope.has("k"));
}
