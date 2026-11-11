#include "../src/core/user_order.hpp"
#include "test.hpp"

using ampify::JoinOrder;
using ampify::ParseOrder;
using ampify::UserOrder;

using Keys = std::vector<std::string>;
using Order = std::vector<std::size_t>;

TEST_CASE(UserOrder_ASettingReadsAsItsKeysAndBack) {
    CHECK((ParseOrder("Radio|Home|Library") == Keys{"Radio", "Home", "Library"}));
    CHECK(ParseOrder("").empty());
    CHECK((ParseOrder("|Radio||Home|") == Keys{"Radio", "Home"}));
    CHECK(JoinOrder({"Radio", "Home"}) == "Radio|Home");
    CHECK(JoinOrder({"Radio", "", "A|B", "Home"}) == "Radio|Home");  // a key that would split is left out
}

TEST_CASE(UserOrder_NoChoiceKeepsApplesOrder) {
    CHECK((UserOrder({"Home", "New", "Radio"}, {}) == Order{0, 1, 2}));
}

TEST_CASE(UserOrder_TheNamedEntriesTakeTheWantedOrder) {
    CHECK((UserOrder({"Home", "New", "Radio"}, {"Radio", "Home", "New"}) == Order{2, 0, 1}));
}

// An app installed after the order was chosen stays where it shows; the named ones fill their own places.
TEST_CASE(UserOrder_AnEntryLeftOutKeepsItsPlace) {
    CHECK((UserOrder({"Home", "Stats", "Radio", "New"}, {"Radio", "New", "Home"}) == Order{2, 1, 3, 0}));
}

TEST_CASE(UserOrder_AnEntryThatWentIsSkipped) {
    CHECK((UserOrder({"Home", "Radio"}, {"Gone", "Radio", "Home"}) == Order{1, 0}));
}

TEST_CASE(UserOrder_TwoEntriesWithTheSameKeyKeepTheirOrder) {
    CHECK((UserOrder({"A", "B", "A"}, {"B", "A"}) == Order{1, 0, 2}));
}
