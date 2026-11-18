# Module library: Ampify.library.tracks, the tracks and play counts of Apple Music's library file
# (src/core/library/musicdb.*, with its zlib decoder src/core/library/inflate.*), permission library.read, and the
# tracks of one album with Apple's ids for them (src/core/library/album_tracks.*, apple.albumClick).
list(APPEND AMPIFY_CORE_SOURCES src/core/library/inflate.cpp src/core/library/musicdb.cpp
     src/core/library/album_tracks.cpp src/core/prelude/library.cpp src/core/natives/library.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/library_tests.cpp tests/album_tracks_tests.cpp)
