#include "../src/core/sidebar_groups.hpp"
#include "test.hpp"

#include <utility>
#include <vector>

using Groups = std::vector<std::pair<int, bool>>;

TEST_CASE(SidebarGroups_FoldCollapsesOnlyTheExpandedGroups) {
    ampify::GroupMemory<int> memory;
    auto collapse = memory.Fold(Groups{{1, true}, {2, false}, {3, true}});
    CHECK((collapse == std::vector<int>{1, 3}));
    CHECK(memory.Holds());
}

TEST_CASE(SidebarGroups_UnfoldGivesBackTheSameGroupsOnce) {
    ampify::GroupMemory<int> memory;
    memory.Fold(Groups{{1, true}, {2, false}, {3, true}});
    CHECK((memory.Unfold() == std::vector<int>{1, 3}));
    CHECK(!memory.Holds());
    CHECK(memory.Unfold().empty());
}

TEST_CASE(SidebarGroups_FoldingTwiceKeepsWhatTheFirstFoldCollapsed) {
    ampify::GroupMemory<int> memory;
    memory.Fold(Groups{{1, true}, {2, true}});
    // The second look sees them already collapsed: nothing new to collapse, nothing forgotten.
    CHECK(memory.Fold(Groups{{1, false}, {2, false}}).empty());
    CHECK((memory.Unfold() == std::vector<int>{1, 2}));
}

TEST_CASE(SidebarGroups_NothingExpandedRemembersNothing) {
    ampify::GroupMemory<int> memory;
    CHECK(memory.Fold(Groups{{1, false}}).empty());
    CHECK(!memory.Holds());
}
