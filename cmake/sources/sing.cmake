# Module sing: Sing's karaoke modes. The portable part in src/core/sing/: the vocal model's STFT, the resampler, the
# studio stream that runs the model a fixed delay behind the song, and the light filter that needs no model.
list(APPEND AMPIFY_CORE_SOURCES src/core/sing/vocal_splitter.cpp src/core/sing/resampler.cpp
    src/core/sing/studio_stream.cpp src/core/sing/light_filter.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/sing_tests.cpp)
# Windows: Sing in Apple's audio process, and the model on the graphics card through ONNX Runtime, loaded at run
# time from the kit (its C header vendored unchanged, MIT, see vendor/onnxruntime/UPSTREAM.txt).
list(APPEND AMPIFY_WIN_SOURCES src/win/audio/sing.cpp src/win/audio/sing_model.cpp)
set_source_files_properties(src/win/audio/sing_model.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime")

# Signalsmith Linear's FFT, vendored unchanged (MIT, see vendor/signalsmith/UPSTREAM.txt): header only, compiled in
# the one file that uses it, with its own warnings silenced.
set_source_files_properties(src/core/sing/vocal_splitter.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/signalsmith"
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
