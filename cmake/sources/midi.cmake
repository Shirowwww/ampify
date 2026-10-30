# Module midi: AMK.midi. Portable decoding and bindings in core/midi_map.*, the winmm input in win/midi.*.
list(APPEND AMK_CORE_SOURCES core/midi_map.cpp core/prelude/midi.cpp core/natives/midi.cpp)
list(APPEND AMK_WIN_SOURCES win/midi.cpp)
list(APPEND AMK_TESTS_SOURCES tests/midi_tests.cpp)
