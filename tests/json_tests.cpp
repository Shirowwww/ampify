#include <string>

#include "../core/json.hpp"
#include "test.hpp"

TEST_CASE(Json_ParsesNestedValuesAndDumpsThemBack) {
    std::string error;
    auto value = amk::Json::Parse(R"({"b":[1,2.5,true,null],"a":{"s":"x"}})", &error);
    CHECK(value.has_value());
    CHECK(error.empty());
    CHECK(value->Dump() == R"({"b":[1,2.5,true,null],"a":{"s":"x"}})");
    CHECK(value->Find("a")->Find("s")->AsString() != nullptr);
    CHECK(*value->Find("a")->Find("s")->AsString() == "x");
}

TEST_CASE(Json_DecodesEscapesAndSurrogatePairs) {
    auto value = amk::Json::Parse(R"("\u00e9\n\ud83c\udfb5")");
    CHECK(value.has_value());
    CHECK(*value->AsString() == "\xC3\xA9\n\xF0\x9F\x8E\xB5");  // é, newline, U+1F3B5
}

TEST_CASE(Json_RejectsMalformedInput) {
    CHECK(!amk::Json::Parse("{\"a\":1,}"));          // trailing comma
    CHECK(!amk::Json::Parse("\"unterminated"));
    CHECK(!amk::Json::Parse("[1 2]"));
    CHECK(!amk::Json::Parse("01"));                 // leading zero
    CHECK(!amk::Json::Parse("\"\x01\""));            // raw control character
    CHECK(!amk::Json::Parse("\"\\ud800\""));         // lone surrogate
    CHECK(!amk::Json::Parse("{} {}"));               // trailing text
}

TEST_CASE(Json_LimitsNestingDepth) {
    std::string deep(100, '[');
    deep += std::string(100, ']');
    std::string error;
    CHECK(!amk::Json::Parse(deep, &error));
    CHECK(!error.empty());
}

TEST_CASE(Json_DumpEscapesStringsAndWritesIntegersWithoutDecimals) {
    amk::Json value(std::string("a\"b\\c\n\x01"));
    CHECK(value.Dump() == "\"a\\\"b\\\\c\\n\\u0001\"");
    CHECK(amk::Json(3.0).Dump() == "3");
    CHECK(amk::Json(0.5).Dump() == "0.5");
}

TEST_CASE(Json_SetReplacesAnExistingMember) {
    amk::Json object(amk::Json::Object{{"k", amk::Json(1.0)}});
    object.Set("k", amk::Json(2.0));
    object.Set("n", amk::Json(true));
    CHECK(object.Dump() == R"({"k":2,"n":true})");
}
