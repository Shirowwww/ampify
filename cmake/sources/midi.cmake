# Module midi: Ampify.midi. Portable decoding and bindings in src/core/midi_map.*, the winmm input in src/win/midi.*.
list(APPEND AMPIFY_CORE_SOURCES src/core/midi_map.cpp src/core/prelude/midi.cpp src/core/natives/midi.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/midi.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/midi_tests.cpp)
