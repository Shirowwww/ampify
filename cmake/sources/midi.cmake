# Module midi: Ampify.midi. Portable decoding and bindings in core/midi_map.*, the winmm input in win/midi.*.
list(APPEND AMPIFY_CORE_SOURCES core/midi_map.cpp core/prelude/midi.cpp core/natives/midi.cpp)
list(APPEND AMPIFY_WIN_SOURCES win/midi.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/midi_tests.cpp)
