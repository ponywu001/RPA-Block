#include "TestHarness.h"
#include "rpa/core/TextMatch.h"

using namespace rpa::core;

RPA_TEST(text_match_modes) {
    CHECK(textMatches("Login", "Login", MatchMode::Exact));
    CHECK(!textMatches("Login now", "Login", MatchMode::Exact));

    CHECK(textMatches("Login now", "Login", MatchMode::Contains));
    CHECK(!textMatches("Sign in", "Login", MatchMode::Contains));

    CHECK(textMatches("order 4821", "[0-9]{4}", MatchMode::Regex));
    CHECK(!textMatches("order abc", "[0-9]{4}", MatchMode::Regex));
}

RPA_TEST(an_invalid_regex_matches_nothing_instead_of_throwing) {
    // A bad pattern in a flow should fail its step, not tear down the run.
    CHECK(!textMatches("anything", "([unclosed", MatchMode::Regex));
}

RPA_TEST(nearest_texts_surfaces_the_line_that_carries_the_needle) {
    // The real failure this exists for: clicking the Windows search box by its
    // label "搜尋" found nothing, because OCR reads the magnifier icon as a "Q"
    // and returns "Q 搜尋". Exact matching failed on text that was on screen, and
    // the error said only "107 lines read".
    const std::vector<std::string> lines = {
        "檔案", "編輯", "Q 搜尋", "說明", "工具", "Q搜",
    };

    const std::vector<std::string> near = nearestTexts(lines, "搜尋");
    CHECK(!near.empty());
    CHECK_EQ(near.front(), std::string{"Q 搜尋"});
}

RPA_TEST(nearest_texts_prefers_the_shortest_full_containment) {
    // The shortest line carrying the needle is the closest thing to what was
    // asked for, so it should be reported first.
    const std::vector<std::string> lines = {
        "the login button is over here",
        "login",
        "please login to continue",
    };
    CHECK_EQ(nearestTexts(lines, "login").front(), std::string{"login"});
}

RPA_TEST(nearest_texts_falls_back_to_partial_overlap) {
    // Nothing contains the whole needle, so the best partial match wins rather
    // than reporting nothing at all.
    const std::vector<std::string> lines = {"完全無關", "確認送出", "確認"};
    const std::vector<std::string> near = nearestTexts(lines, "確認送出訂單");
    CHECK(!near.empty());
    CHECK_EQ(near.front(), std::string{"確認送出"});
}

RPA_TEST(nearest_texts_reports_nothing_when_there_is_no_overlap) {
    // Listing unrelated lines would be noise, not a hint.
    const std::vector<std::string> lines = {"alpha", "beta", "gamma"};
    CHECK(nearestTexts(lines, "搜尋").empty());
    CHECK(nearestTexts(lines, "").empty());
    CHECK(nearestTexts({}, "anything").empty());
}

RPA_TEST(nearest_texts_honours_its_limit) {
    const std::vector<std::string> lines = {"a login", "b login", "c login", "d login"};
    CHECK_EQ(nearestTexts(lines, "login", 2).size(), std::size_t{2});
    CHECK_EQ(nearestTexts(lines, "login", 1).size(), std::size_t{1});
}

RPA_TEST(ocr_targets_default_to_contains) {
    // Exact was the old default and it is what made the first real attempt fail.
    Target target;
    CHECK(target.match == MatchMode::Contains);
}
