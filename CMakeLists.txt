cmake_minimum_required(VERSION 3.3)
cmake_policy(SET CMP0063 NEW)

project(majak LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

include(CheckSymbolExists)

set(
    ninja_sources

    src/build.cc
    src/build_log.cc
    src/clean.cc
    src/clparser.cc
    src/debug_flags.cc
    src/deps_log.cc
    src/disk_interface.cc
    src/edit_distance.cc
    src/eval_env.cc
    src/graph.cc
    src/graphviz.cc
    src/line_printer.cc
    src/manifest_parser.cc
    src/metrics.cc
    src/ninja.cc
    src/state.cc
    src/string_piece_util.cc
    src/util.cc
    src/version.cc
)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
find_package(RE2C)

if (RE2C_FOUND AND RE2C_VERSION VERSION_GREATER 0.11.3)
    re2c_target(
        NAME re2c-depfile_parser
        INPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/depfile_parser.in.cc
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/depfile_parser.cc
        OPTIONS -b -i --no-generation-date
    )
    list(APPEND ninja_sources ${CMAKE_CURRENT_BINARY_DIR}/depfile_parser.cc)
    re2c_target(
        NAME re2c-lexer
        INPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.in.cc
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lexer.cc
        OPTIONS -b -i --no-generation-date
    )
    list(APPEND ninja_sources ${CMAKE_CURRENT_BINARY_DIR}/lexer.cc)
else()
    list(APPEND ninja_sources src/depfile_parser.cc src/lexer.cc)
endif()

if (WIN32)
    list(APPEND
        ninja_sources

        src/includes_normalize-win32.cc
        src/msvc_helper_main-win32.cc
        src/msvc_helper-win32.cc
        src/minidump-win32.cc
        src/subprocess-win32.cc
    )
else()
    list(APPEND
        ninja_sources

        src/subprocess-posix.cc
    )
endif()

check_symbol_exists(getopt getopt.h NINJA_HAS_GETOPT)

if (NOT NINJA_HAS_GETOPT)
    list(APPEND ninja_sources src/getopt.c)
endif()

if (WIN32)
    set(
        ninja_compile_options

        /W4  # Highest warning level.
        /WX  # Warnings as errors.
        /wd4530 /wd4100 /wd4706 /wd4244
        /wd4512 /wd4800 /wd4702 /wd4819
        # Disable warnings about constant conditional expressions.
        /wd4127
        # Disable warnings about passing "this" during initialization.
        /wd4355
        # Disable warnings about ignored typedef in DbgHelp.h
        /wd4091
        /GR-  # Disable RTTI.
        # Disable size_t -> int truncation warning.
        # We never have strings or arrays larger than 2**31.
        /wd4267
    )

    set(
        ninja_compile_definitions

        DNOMINMAX
        D_CRT_SECURE_NO_WARNINGS
        D_HAS_EXCEPTIONS=0
    )
else()
    include(CheckCXXCompilerFlag)

    set(
        ninja_compile_options

        -Wall
        -Wextra
        -Wno-deprecated
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-implicit-fallthrough
        -fno-rtti
        -fno-exceptions
        -iquote "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    check_symbol_exists(ppoll poll.h NINJA_HAS_PPOLL)

    if (NINJA_HAS_PPOLL)
        set(ninja_compile_definitions USE_PPOLL)
    endif()

    check_cxx_compiler_flag(-fdiagnostics-color NINJA_COMPILER_SUPPORTS_COLOR)

    if (NINJA_COMPILER_SUPPORTS_COLOR)
        list(APPEND ninja_compile_options -fdiagnostics-color)
    endif()
endif()

add_library(libninja STATIC ${ninja_sources})
set_property(TARGET libninja PROPERTY PREFIX "")
set_property(TARGET libninja PROPERTY CXX_VISIBILITY_PRESET hidden)
target_compile_options(libninja PUBLIC ${ninja_compile_options})
target_compile_definitions(
    libninja
    PUBLIC ${ninja_compile_definitions}
    PRIVATE ${ninja_private_compile_definitions}
)

add_executable(ninja src/ninja_main.cc)
target_link_libraries(ninja libninja)

add_executable(majak src/majak_main.cc)
target_link_libraries(majak libninja)

set(
    ninja_test_sources

    src/build_log_test.cc
    src/build_test.cc
    src/clean_test.cc
    src/clparser_test.cc
    src/depfile_parser_test.cc
    src/deps_log_test.cc
    src/disk_interface_test.cc
    src/edit_distance_test.cc
    src/graph_test.cc
    src/lexer_test.cc
    src/manifest_parser_test.cc
    src/ninja_test.cc
    src/state_test.cc
    src/string_piece_util_test.cc
    src/subprocess_test.cc
    src/test.cc
    src/util_test.cc
)

if (WIN32)
    list(APPEND
        ninja_test_sources

        src/includes_normalize_test.cc
        src/msvc_helper_test.cc
    )
endif()

add_executable(ninja_test ${ninja_test_sources})
target_link_libraries(ninja_test libninja)
add_test(NAME ninja_test COMMAND ninja_test)

set(
    ninja_perftests

    build_log_perftest
    canon_perftest
    depfile_parser_perftest
    hash_collision_bench
    manifest_parser_perftest
    clparser_perftest
)

foreach(perftest_name IN LISTS ninja_perftests)
    add_executable(${perftest_name} src/${perftest_name}.cc)
    target_link_libraries(${perftest_name} libninja)
endforeach()

install(TARGETS ninja majak RUNTIME DESTINATION bin)
