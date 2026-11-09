# Reads the ready-made layouts (src/core/theme/layouts/*.css) and writes builtin_layouts.hpp into the build folder,
# one C++ constant per file, which builtins.cpp serves as `@import "ampify:<name>"`. A theme writer reads and edits
# plain CSS; the kernel still carries it, so a layout never goes missing or stale on disk.

set(AMPIFY_LAYOUTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/theme/layouts")
set(AMPIFY_LAYOUTS_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/layouts")
set(AMPIFY_LAYOUT_NAMES floating-panels bottom-bar centre-bar scrollbars)

# MSVC refuses a single string literal over 16380 bytes (C2026).
set(AMPIFY_LAYOUT_MAX_BYTES 16000)

# floating-panels -> kFloatingPanels
function(ampify_layout_constant out name)
    string(REPLACE "-" ";" words "${name}")
    set(constant "k")
    foreach(word IN LISTS words)
        string(SUBSTRING "${word}" 0 1 first)
        string(SUBSTRING "${word}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(APPEND constant "${first}${rest}")
    endforeach()
    set(${out} "${constant}" PARENT_SCOPE)
endfunction()

set(ampify_layouts_header "// Written by cmake/layouts.cmake from src/core/theme/layouts/: edit those files, not this one.\n")
string(APPEND ampify_layouts_header "#pragma once\n\n#include <string_view>\n\nnamespace ampify::theme::layouts {\n")
foreach(name IN LISTS AMPIFY_LAYOUT_NAMES)
    set(file "${AMPIFY_LAYOUTS_DIR}/${name}.css")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    file(READ "${file}" css)
    # The same bytes from a checkout with CRLF line ends as from one with LF.
    string(REPLACE "\r" "" css "${css}")
    string(FIND "${css}" ")css\"" closing)
    if(NOT closing EQUAL -1)
        message(FATAL_ERROR "layouts: ${name}.css must not hold the text )css\" (it would end the C++ literal)")
    endif()
    string(LENGTH "${css}" bytes)
    if(bytes GREATER AMPIFY_LAYOUT_MAX_BYTES)
        message(FATAL_ERROR "layouts: ${name}.css is ${bytes} bytes, over ${AMPIFY_LAYOUT_MAX_BYTES}: split it in two files")
    endif()
    ampify_layout_constant(constant "${name}")
    string(APPEND ampify_layouts_header "\ninline constexpr std::string_view ${constant} = R\"css(\n${css})css\";\n")
endforeach()
string(APPEND ampify_layouts_header "\n}  // namespace ampify::theme::layouts\n")

# Rewritten only when its text changes, as configure_file does, so a configure run rebuilds nothing by itself.
set(ampify_layouts_path "${AMPIFY_LAYOUTS_INCLUDE}/builtin_layouts.hpp")
set(ampify_layouts_old "")
if(EXISTS "${ampify_layouts_path}")
    file(READ "${ampify_layouts_path}" ampify_layouts_old)
endif()
if(NOT ampify_layouts_old STREQUAL ampify_layouts_header)
    file(WRITE "${ampify_layouts_path}" "${ampify_layouts_header}")
endif()
