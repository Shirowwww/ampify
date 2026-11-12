# Module automix: Ampify.automix. The plan, the session that plays it and the deck that mixes the two songs in
# src/core/automix/ (portable, tested), the two players of Apple Music in src/win/audio/automix.*.
list(APPEND AMPIFY_CORE_SOURCES src/core/automix/mix_plan.cpp src/core/automix/mix_session.cpp
    src/core/automix/mix_deck.cpp src/core/automix/tempo_stretch.cpp
    src/core/prelude/automix.cpp src/core/natives/automix.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/audio/automix.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/automix_tests.cpp tests/automix_deck_tests.cpp)

# Signalsmith Stretch, vendored unchanged (MIT, see vendor/signalsmith/UPSTREAM.txt): header only, compiled in the
# one file that wraps it, with its own warnings silenced.
set_source_files_properties(src/core/automix/tempo_stretch.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/signalsmith"
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
