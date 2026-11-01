#include "../src/core/gain_board.hpp"
#include "test.hpp"

TEST_CASE(GainBoard_SumsTheElementsAndClampsToTheCeiling) {
    ampify::GainBoard board;
    board.Set("a", 4, 6);
    board.Set("b", 3, 6);  // 4 + 3 = 7, clamped to 6
    CHECK_NEAR(board.TotalDb(), 6.0, 1e-9);
    CHECK_NEAR(board.Linear(), 1.9953, 1e-3);
}

TEST_CASE(GainBoard_ANegativeGainLowersTheTrack) {
    ampify::GainBoard board;
    board.Set("a", -6, 12);
    CHECK_NEAR(board.TotalDb(), -6.0, 1e-9);
    CHECK_NEAR(board.Linear(), 0.5012, 1e-3);
}

TEST_CASE(GainBoard_ZeroRemovesTheElement) {
    ampify::GainBoard board;
    board.Set("a", 5, 12);
    board.Set("a", 0, 12);
    CHECK(board.Count() == 0);
    CHECK(board.Linear() == 1.0f);
    CHECK_NEAR(board.TotalDb(), 0.0, 1e-9);
}

TEST_CASE(GainBoard_AnElementReplacesItsOwnValue) {
    ampify::GainBoard board;
    board.Set("a", 2, 12);
    board.Set("b", 2, 12);
    board.Set("a", 1, 12);  // a goes from 2 to 1 dB: the total is 3 dB, not 5
    CHECK(board.Count() == 2);
    CHECK_NEAR(board.TotalDb(), 3.0, 1e-9);
}

TEST_CASE(GainBoard_TheCeilingCanBeZero) {
    ampify::GainBoard board;
    board.Set("a", 6, 0);  // audio.maxGainDb = 0: no boost at all
    CHECK_NEAR(board.TotalDb(), 0.0, 1e-9);
    CHECK(board.Linear() == 1.0f);
}
