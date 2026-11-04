# Module library: Ampify.library.tracks, the tracks and play counts of Apple Music's library file
# (src/core/library/musicdb.*, with its zlib decoder src/core/library/inflate.*), permission library.read.
list(APPEND AMPIFY_CORE_SOURCES src/core/library/inflate.cpp src/core/library/musicdb.cpp src/core/prelude/library.cpp
     src/core/natives/library.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/library_tests.cpp)
