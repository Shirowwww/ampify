# Module fs: Ampify.fs.writeText, text files in <Ampify folder>\output\<id>\ only (src/core/output_files.*), permission fs.output.
list(APPEND AMPIFY_CORE_SOURCES src/core/output_files.cpp src/core/prelude/fs.cpp src/core/natives/fs.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/fs_tests.cpp)
