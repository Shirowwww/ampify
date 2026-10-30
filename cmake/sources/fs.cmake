# Module fs: AMK.fs.writeText, text files in <kit>\output\<id>\ only (core/output_files.*), permission fs.output.
list(APPEND AMK_CORE_SOURCES core/output_files.cpp core/prelude/fs.cpp core/natives/fs.cpp)
list(APPEND AMK_TESTS_SOURCES tests/fs_tests.cpp)
