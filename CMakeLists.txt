cmake_minimum_required(VERSION 3.9)
cmake_policy(SET CMP0063 NEW)

project(majak LANGUAGES C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
find_package(PythonInterp 3)
if (NOT PYTHONINTERP_FOUND)
    find_package(PythonInterp 2.7)
endif()
find_package(Doxygen OPTIONAL_COMPONENTS dot)
find_program(NINJA_ASCIIDOC_EXECUTABLE asciidoc)
find_program(NINJA_XSLTPROC_EXECUTABLE xsltproc)
find_program(NINJA_DBLATEX_EXECUTABLE dblatex)
find_program(NINJA_RE2C_EXECUTABLE re2c)
find_package(Git)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(FeatureSummary)
include(GetGitVersionInfo)
include(GoogleTest)
include(RE2CTarget)
include(utils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_git_version_info(MAJAK_GIT)

ninja_find_filesystem_library()
message(STATUS "NINJA_FILESYSTEM_INCLUDE: ${NINJA_FILESYSTEM_INCLUDE}")
message(STATUS "NINJA_FILESYSTEM_NAMESPACE: ${NINJA_FILESYSTEM_NAMESPACE}")
message(STATUS "NINJA_FILESYSTEM_LIBRARY: ${NINJA_FILESYSTEM_LIBRARY}")

if (NINJA_RE2C_EXECUTABLE)
    message(STATUS "RE2C: ${NINJA_RE2C_EXECUTABLE}")
    add_executable(re2c IMPORTED)
else()
    message(STATUS "RE2C: <built from third_party/re2c sources>")
    add_subdirectory(third_party/re2c EXCLUDE_FROM_ALL)
    set(NINJA_RE2C_EXECUTABLE re2c)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    add_definitions(-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC)
    set(NINJA_ENABLE_RTTI ON)
else()
    option(
        NINJA_ENABLE_RTTI
        "Build ninja with RTTI enabled"
        OFF
    )
endif()

if (MSVC)
    # required for abseil
    add_definitions(-D_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_VERSION VERSION_GREATER "3.12")
    set(ninja_shared_for_tests_default ON)
else()
    set(ninja_shared_for_tests_default OFF)
endif()

option(
    NINJA_FORCE_PSELECT
    [=[
    ppoll() is used by default where available,
    but some platforms may need to use pselect instead'
    ]=]
    OFF
)
option(
    NINJA_BUILD_TESTS
    "Build ninja tests and benchmarks."
    ON
)
if (NINJA_FILESYSTEM_NAMESPACE STREQUAL "boost::filesystem")
    set(NINJA_ENABLE_EXCEPTIONS ON CACHE INTERNAL FORCE "")
else()
    option(
        NINJA_ENABLE_EXCEPTIONS
        "Build ninja with exceptions enabled."
        OFF
    )
endif()
option(
    NINJA_ENABLE_RTTI
    "Build ninja with RTTI enabled"
    OFF
)

add_feature_info(
    build_tests
    "${NINJA_BUILD_TESTS}"
    "build tests for ninja."
)

if (NINJA_BUILD_TESTS)
    enable_testing()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/gtest EXCLUDE_FROM_ALL)
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gtest/CMakeLists.txt")
        message(
            SEND_ERROR
            [=[
 Please run "git submodule update --init"
 or set option NINJA_BUILD_TESTS to OFF.
            ]=]
        )
    endif()

    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/benchmark EXCLUDE_FROM_ALL)
endif()

set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_LIBCXX_WITH_CLANG OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/flatbuffers EXCLUDE_FROM_ALL)

add_subdirectory(third_party/abseil EXCLUDE_FROM_ALL)

if (MSVC)
    fix_default_msvc_settings()
endif()

set(
    ninja_sources

    src/lib/build.cc
    src/lib/build_log.cc
    src/lib/clean.cc
    src/lib/clparser.cc
    src/lib/debug_flags.cc
    src/lib/disk_interface.cc
    src/lib/eval_env.cc
    src/lib/graph.cc
    src/lib/graphviz.cc
    src/lib/line_printer.cc
    src/lib/manifest_parser.cc
    src/lib/message.cc
    src/lib/metrics.cc
    src/lib/ninja.cc
    src/lib/state.cc
    src/lib/string_piece_util.cc
    src/lib/util.cc
    src/lib/version.cc
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/lib")

re2c_target(
    NAME re2c-depfile_parser
    INPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/depfile_parser.in.cc
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/lib/depfile_parser.cc
    OPTIONS -b -i --no-generation-date
)
list(APPEND ninja_sources ${CMAKE_CURRENT_BINARY_DIR}/src/lib/depfile_parser.cc)
re2c_target(
    NAME re2c-lexer
    INPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/lexer.in.cc
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/lib/lexer.cc
    OPTIONS -b -i --no-generation-date
)
list(APPEND ninja_sources ${CMAKE_CURRENT_BINARY_DIR}/src/lib/lexer.cc)

set_package_properties(RE2C PROPERTIES
    TYPE OPTIONAL
    DESCRIPTION "re2c is a free and open-source software lexer generator for C."
    PURPOSE "Enables generation of manifest lexer and depfile parser."
)

if (WIN32)
    list(APPEND
        ninja_sources

        src/lib/includes_normalize-win32.cc
        src/lib/msvc_helper_main-win32.cc
        src/lib/msvc_helper-win32.cc
        src/lib/minidump-win32.cc
        src/lib/subprocess-win32.cc
    )
else()
    list(APPEND
        ninja_sources

        src/lib/subprocess-posix.cc
    )
endif()

check_cxx_source_compiles(
    [=[
    #include <unistd.h>
    #include <getopt.h>
    int main(int argc, char** argv) {
        int i = getopt(argc, argv, "h");
        int j = getopt_long(argc, argv, "h", nullptr, nullptr);
    }
    ]=]
    NINJA_HAVE_GETOPT
)

if (MSVC)
    set(
        ninja_compile_options

        /nologo
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
        # Disable size_t -> int truncation warning.
        # We never have strings or arrays larger than 2**31.
        /wd4267
    )

    if (NINJA_ENABLE_RTTI)
        list(APPEND ninja_compile_options /GR)
    else()
        list(APPEND ninja_compile_options /GR-)
    endif()
else()
    set(
        ninja_compile_options

        -Wall
        -Wextra
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-implicit-fallthrough
    )

    if (NOT NINJA_ENABLE_EXCEPTIONS)
        list(APPEND ninja_compile_options -fno-exceptions)
    endif()

    if(NOT NINJA_ENABLE_RTTI)
        list(APPEND ninja_compile_options -fno-rtti)
    endif()

    if (NOT NINJA_FORCE_PSELECT)
        check_cxx_source_compiles(
            [=[
            #include <signal.h>
            #include <poll.h>

            int main() {
                int i = ppoll(nullptr, 0, nullptr, nullptr);
            }
            ]=]
            NINJA_HAVE_PPOLL
        )

        if (NINJA_HAVE_PPOLL)
            set(NINJA_USE_PPOLL TRUE)
        endif()
    else()
        set(NINJA_USE_PPOLL FALSE)
    endif()

    add_feature_info(ppoll "${NINJA_USE_PPOLL}" "fast polling mechanism.")

    check_cxx_compiler_flag(-fdiagnostics-color NINJA_COMPILER_SUPPORTS_COLOR)

    if (NINJA_COMPILER_SUPPORTS_COLOR)
        list(APPEND ninja_compile_options -fdiagnostics-color=always)
    endif()
endif()

if (WIN32)
    list(APPEND
        ninja_compile_definitions

        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _HAS_EXCEPTIONS=0
    )
endif()

if (PYTHONINTERP_FOUND AND NOT WIN32)
    set(NINJA_PYTHON "${PYTHON_EXECUTABLE}")
    set(NINJA_HAVE_BROWSE TRUE)

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/src/lib/browse_py.h"
        DEPENDS src/browse.py cmake/inline.cmake
        COMMAND
            "${CMAKE_COMMAND}"
            -DINPUT=src/browse.py
            -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/src/lib/browse_py.h"
            -DVARNAME=kBrowsePy
            -P cmake/inline.cmake
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "[inline] src/browse.py"
    )
    list(APPEND ninja_sources src/lib/browse.cc "${CMAKE_CURRENT_BINARY_DIR}/src/lib/browse_py.h")
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/src/lib/log_schema.h"
    DEPENDS src/log.fbs cmake/inline.cmake
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT=src/log.fbs
        -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/src/lib/log_schema.h"
        -DVARNAME=kBuildLogSchema
        -P cmake/inline.cmake
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "[inline] src/log.fbs"
)
list(APPEND ninja_sources "${CMAKE_CURRENT_BINARY_DIR}/src/lib/log_schema.h")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/ninja/log_generated.h"
    DEPENDS src/log.fbs
    COMMAND
        flatc
        --cpp
        -o ${CMAKE_CURRENT_BINARY_DIR}/include/ninja
        --gen-object-api
        --scoped-enums
        --gen-name-strings
        --reflect-names
        ${CMAKE_CURRENT_SOURCE_DIR}/src/log.fbs
    COMMENT "[flatc] src/log.fbs"
)
list(APPEND ninja_sources "${CMAKE_CURRENT_BINARY_DIR}/include/ninja/log_generated.h")
set_package_properties(PythonInterp PROPERTIES
    TYPE OPTIONAL
    DESCRIPTION "Python language interpreter."
    PURPOSE "Enables manifest browser."
)

configure_file(include/ninja/ninja_config.in.h include/ninja/ninja_config.h)

add_library(ninja-getopt STATIC EXCLUDE_FROM_ALL src/getopt/getopt.c)
target_include_directories(ninja-getopt PUBLIC src/getopt)

add_library(libninja STATIC ${ninja_sources})
target_link_libraries(
    libninja
    PUBLIC
    flatbuffers
    absl::strings
    "${NINJA_FILESYSTEM_LIBRARY}"
    PRIVATE
    str_format_internal
)
if (NOT NINJA_HAVE_GETOPT)
    target_link_libraries(libninja PRIVATE ninja-getopt)
endif()
target_compile_definitions(libninja PRIVATE ${ninja_private_compile_definitions})
target_compile_options(libninja PUBLIC ${ninja_compile_options})
target_compile_definitions(libninja PUBLIC ${ninja_compile_definitions})
target_include_directories(
    libninja
    PUBLIC
    include
    "${CMAKE_CURRENT_BINARY_DIR}/include"
    third_party/flatbuffers/include
    PRIVATE
    src/lib
    "${CMAKE_CURRENT_BINARY_DIR}/src/lib"
)

add_executable(ninja src/main/ninja_main.cc)
target_link_libraries(ninja PRIVATE libninja)
if (NOT NINJA_HAVE_GETOPT)
    target_link_libraries(ninja PRIVATE ninja-getopt)
endif()

add_executable(majak src/main/majak_main.cc)
target_link_libraries(majak PRIVATE libninja)
if (NOT NINJA_HAVE_GETOPT)
    target_link_libraries(majak PRIVATE ninja-getopt)
endif()

if (NINJA_BUILD_TESTS)
    set(
        ninja_test_sources

        src/tests/build_log_test.cc
        src/tests/build_test.cc
        src/tests/clean_test.cc
        src/tests/clparser_test.cc
        src/tests/depfile_parser_test.cc
        src/tests/deps_log_test.cc
        src/tests/disk_interface_test.cc
        src/tests/graph_test.cc
        src/tests/lexer_test.cc
        src/tests/manifest_parser_test.cc
        src/tests/message_test.cc
        src/tests/state_test.cc
        src/tests/string_piece_util_test.cc
        src/tests/subprocess_test.cc
        src/tests/test.cc
        src/tests/util_test.cc
    )

    if (WIN32)
        list(APPEND
            ninja_test_sources

            src/tests/includes_normalize_test.cc
            src/tests/msvc_helper_test.cc
        )
    endif()

    add_executable(ninja_test ${ninja_test_sources})
    target_link_libraries(ninja_test ${ninja_filesystem_library} libninja gtest gtest_main)
    target_include_directories(
        ninja_test
        PRIVATE
        src/lib
    )

    if (COMMAND gtest_discover_tests)
        gtest_discover_tests(ninja_test)
    else()
        add_test(NAME ninja_test COMMAND ninja_test)
    endif()

    set(
        ninja_perftests

        depfile_parser_perftest
        hash_collision_bench
        manifest_parser_perftest
        clparser_perftest
    )
    set(
        ninja_bench_perftests

        build_log_perftest
        canon_perftest
    )

    foreach(perftest_name IN LISTS ninja_perftests)
        add_executable(${perftest_name} src/tests/${perftest_name}.cc)
        target_link_libraries(${perftest_name} libninja)
        target_include_directories(${perftest_name}
            PRIVATE
            src/lib
        )
    endforeach()
    set_property(
        SOURCE src/tests/manifest_parser_perftest.cc
        PROPERTY
        COMPILE_DEFINITIONS "CMAKE_CURRENT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    )

    foreach(perftest_name IN LISTS ninja_bench_perftests)
        add_executable(${perftest_name} src/tests/${perftest_name}.cc)
        target_link_libraries(${perftest_name} libninja benchmark)
        target_include_directories(
            ${perftest_name}
            PRIVATE
            src
            "${CMAKE_CURRENT_BINARY_DIR}/src"
        )
    endforeach()
    if (NOT NINJA_HAVE_GETOPT)
        target_link_libraries(manifest_parser_perftest ninja-getopt)
    endif()

    if (Boost_FILESYSTEM_FOUND)
        target_include_directories(canon_perftest PRIVATE ${Boost_INCLUDE_DIRS})
        target_link_libraries(
            canon_perftest
            ${Boost_FILESYSTEM_LIBRARY}
            ${Boost_SYSTEM_LIBRARY}
        )
    endif()
endif()

if (TARGET Doxygen::dot AND CMAKE_GENERATOR STREQUAL "Ninja")
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/graph.dot
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/build.ninja
        COMMAND
            ninja -t graph all > ${CMAKE_CURRENT_BINARY_DIR}/graph.dot
        COMMENT
            "[graph] build.ninja"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/graph.png
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/graph.dot
        COMMAND
            Doxygen::dot
            -Tpng
            -o${CMAKE_CURRENT_BINARY_DIR}/graph.png
            ${CMAKE_CURRENT_BINARY_DIR}/graph.dot
        COMMENT
            "[dot] graph.dot"
    )
    add_custom_target(graph DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/graph.png)
endif()

if (NINJA_ASCIIDOC_EXECUTABLE)
    add_custom_command(
        DEPENDS doc/manual.asciidoc
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
        COMMAND
            "${NINJA_ASCIIDOC_EXECUTABLE}"
            -b docbook
            -d book
            -o "${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
            ${CMAKE_CURRENT_SOURCE_DIR}/doc/manual.asciidoc
        COMMENT "[asciidoc] doc/manual.asciidoc"
    )
    if (NINJA_XSLTPROC_EXECUTABLE)
        add_custom_command(
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
            DEPENDS doc/style.css doc/docbook.xsl
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manual.html"
            COMMAND
                "${NINJA_XSLTPROC_EXECUTABLE}"
                --nonet
                --output "${CMAKE_CURRENT_BINARY_DIR}/manual.html"
                ${CMAKE_CURRENT_SOURCE_DIR}/doc/docbook.xsl
                "${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
            COMMENT "[xsltproc] ${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
        )
        list(APPEND manuals ${CMAKE_CURRENT_BINARY_DIR}/manual.html)
    endif()

    if (NINJA_DBLATEX_EXECUTABLE)
        add_custom_command(
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
            DEPENDS doc/dblatex.xsl
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manual.pdf"
            COMMAND
                "${NINJA_DBLATEX_EXECUTABLE}"
                -q
                -o "${CMAKE_CURRENT_BINARY_DIR}/manual.pdf"
                -p ${CMAKE_CURRENT_SOURCE_DIR}/doc/dblatex.xsl
                "${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
            COMMENT "[dblatex] ${CMAKE_CURRENT_BINARY_DIR}/manual.xml"
        )
        list(APPEND manuals ${CMAKE_CURRENT_BINARY_DIR}/manual.pdf)
    endif()
endif()

add_custom_target(manual DEPENDS ${manuals})

install(TARGETS ninja majak RUNTIME DESTINATION bin)

feature_summary(INCLUDE_QUIET_PACKAGES WHAT ALL)
