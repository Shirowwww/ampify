# Module automix: Ampify.automix. The plan and the session that plays it in src/core/automix/ (portable, tested), the
# two players of Apple Music in src/win/audio/automix.*.
list(APPEND AMPIFY_CORE_SOURCES src/core/automix/mix_plan.cpp src/core/automix/mix_session.cpp
    src/core/prelude/automix.cpp src/core/natives/automix.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/audio/automix.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/automix_tests.cpp)
