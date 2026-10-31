# Module fs: Ampify.fs.writeText, text files in <Ampify folder>\output\<id>\ only (core/output_files.*), permission fs.output.
list(APPEND AMPIFY_CORE_SOURCES core/output_files.cpp core/prelude/fs.cpp core/natives/fs.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/fs_tests.cpp)
