#include <string>

#include "../src/core/json.hpp"
#include "test.hpp"

TEST_CASE(Json_ParsesNestedValuesAndDumpsThemBack) {
    std::string error;
    auto value = ampify::Json::Parse(R"({"b":[1,2.5,true,null],"a":{"s":"x"}})", &error);
    CHECK(value.has_value());
    CHECK(error.empty());
    CHECK(value->Dump() == R"({"b":[1,2.5,true,null],"a":{"s":"x"}})");
    CHECK(value->Find("a")->Find("s")->AsString() != nullptr);
    CHECK(*value->Find("a")->Find("s")->AsString() == "x");
}

TEST_CASE(Json_DecodesEscapesAndSurrogatePairs) {
    auto value = ampify::Json::Parse(R"("\u00e9\n\ud83c\udfb5")");
    CHECK(value.has_value());
    CHECK(*value->AsString() == "\xC3\xA9\n\xF0\x9F\x8E\xB5");  // é, newline, U+1F3B5
}

TEST_CASE(Json_RejectsMalformedInput) {
    CHECK(!ampify::Json::Parse("{\"a\":1,}"));          // trailing comma
    CHECK(!ampify::Json::Parse("\"unterminated"));
    CHECK(!ampify::Json::Parse("[1 2]"));
    CHECK(!ampify::Json::Parse("01"));                 // leading zero
    CHECK(!ampify::Json::Parse("\"\x01\""));            // raw control character
    CHECK(!ampify::Json::Parse("\"\\ud800\""));         // lone surrogate
    CHECK(!ampify::Json::Parse("{} {}"));               // trailing text
}

TEST_CASE(Json_LimitsNestingDepth) {
    std::string deep(100, '[');
    deep += std::string(100, ']');
    std::string error;
    CHECK(!ampify::Json::Parse(deep, &error));
    CHECK(!error.empty());
}

TEST_CASE(Json_DumpEscapesStringsAndWritesIntegersWithoutDecimals) {
    ampify::Json value(std::string("a\"b\\c\n\x01"));
    CHECK(value.Dump() == "\"a\\\"b\\\\c\\n\\u0001\"");
    CHECK(ampify::Json(3.0).Dump() == "3");
    CHECK(ampify::Json(0.5).Dump() == "0.5");
}

TEST_CASE(Json_SetReplacesAnExistingMember) {
    ampify::Json object(ampify::Json::Object{{"k", ampify::Json(1.0)}});
    object.Set("k", ampify::Json(2.0));
    object.Set("n", ampify::Json(true));
    CHECK(object.Dump() == R"({"k":2,"n":true})");
}

// Mutated and random input must never crash the reader or the manifest checks. The seed is fixed, so a failure
// can be replayed.
TEST_CASE(Json_SurvivesMutatedAndRandomInput) {
    const std::string valid = R"({"a":[1,2.5e3,"é\n",true,null],"b":{"c":"d"}})";
    unsigned state = 12345;
    auto next = [&state] {
        state = state * 1103515245u + 12345u;
        return (state >> 16) & 0x7fff;
    };

    int accepted = 0;
    for (int round = 0; round < 4000; round++) {
        std::string text = valid;
        size_t edits = 1 + next() % 4;
        for (size_t i = 0; i < edits && !text.empty(); i++) {
            size_t at = next() % text.size();
            switch (next() % 3) {
                case 0: text[at] = static_cast<char>(next() % 256); break;
                case 1: text.erase(at, 1); break;
                default: text.insert(at, 1, static_cast<char>(next() % 128)); break;
            }
        }
        if (ampify::Json::Parse(text)) accepted++;
    }
    CHECK(accepted > 0);  // some mutations stay valid, and the reader keeps accepting them

    for (int round = 0; round < 2000; round++) {
        std::string noise;
        size_t length = next() % 64;
        for (size_t i = 0; i < length; i++) noise += static_cast<char>(next() % 256);
        ampify::Json::Parse(noise);  // only the absence of a crash matters
    }
    CHECK(ampify::Json::Parse(valid).has_value());
}
