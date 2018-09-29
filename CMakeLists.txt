cmake_minimum_required(VERSION 3.9)
cmake_policy(SET CMP0063 NEW)

project(majak LANGUAGES C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
find_package(RE2C)
find_package(PythonInterp 3)
if (NOT PYTHONINTERP_FOUND)
    find_package(PythonInterp 2.7)
endif()
find_package(Doxygen OPTIONAL_COMPONENTS dot)
find_program(NINJA_ASCIIDOC_EXECUTABLE asciidoc)
find_program(NINJA_XSLTPROC_EXECUTABLE xsltproc)
find_program(NINJA_DBLATEX_EXECUTABLE dblatex)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(FeatureSummary)
include(GoogleTest)
include(utils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

ninja_find_filesystem_library()
message(STATUS "NINJA_FILESYSTEM_INCLUDE: ${NINJA_FILESYSTEM_INCLUDE}")
message(STATUS "NINJA_FILESYSTEM_NAMESPACE: ${NINJA_FILESYSTEM_NAMESPACE}")
message(STATUS "NINJA_FILESYSTEM_LIBRARY: ${NINJA_FILESYSTEM_LIBRARY}")

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
option(
    NINJA_SHARED_FOR_TESTS
    "Build a shared library for tests to reduce relinking."
    ninja_shared_for_tests_default
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
    add_subdirectory(ext/gtest EXCLUDE_FROM_ALL)
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext/gtest/CMakeLists.txt")
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
    add_subdirectory(ext/benchmark EXCLUDE_FROM_ALL)
endif()

set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_LIBCXX_WITH_CLANG OFF CACHE BOOL "" FORCE)
add_subdirectory(ext/flatbuffers EXCLUDE_FROM_ALL)

if (MSVC)
    fix_default_msvc_settings()
endif()

set(
    ninja_sources

    src/build.cc
    src/build_log.cc
    src/clean.cc
    src/clparser.cc
    src/debug_flags.cc
    src/deps_log.cc
    src/disk_interface.cc
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

set_package_properties(RE2C PROPERTIES
    TYPE OPTIONAL
    DESCRIPTION "re2c is a free and open-source software lexer generator for C."
    PURPOSE "Enables generation of manifest lexer and depfile parser."
)

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

if (NOT NINJA_HAVE_GETOPT)
    set(NINJA_OWN_GETOPT TRUE)
    list(APPEND ninja_sources src/getopt.c)
else()
    set(NINJA_OWN_GETOPT FALSE)
endif()

add_feature_info(
    custom_getopt
    "${NINJA_OWN_GETOPT}"
    "custom version of getopt and getopt_long."
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
        -iquote "${CMAKE_CURRENT_SOURCE_DIR}/src"
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
    list(APPEND ninja_sources src/browse.cc)

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/browse_py.h"
        DEPENDS src/browse.py cmake/inline.cmake
        COMMAND
            "${CMAKE_COMMAND}"
            -DINPUT=src/browse.py
            -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/browse_py.h"
            -DVARNAME=kBrowsePy
            -P cmake/inline.cmake
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "[inline] src/browse.py"
    )
    set_property(
        SOURCE src/browse.cc
        PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/browse_py.h"
    )
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/log_schema.h"
    DEPENDS src/log.fbs cmake/inline.cmake
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT=src/log.fbs
        -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/log_schema.h"
        -DVARNAME=kBuildLogSchema
        -P cmake/inline.cmake
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "[inline] src/log.fbs"
)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/log_generated.h"
    DEPENDS src/log.fbs
    COMMAND
        flatc
        --cpp
        -o ${CMAKE_CURRENT_BINARY_DIR}
        --gen-object-api
        ${CMAKE_CURRENT_SOURCE_DIR}/src/log.fbs
    COMMENT "[flatc] src/log.fbs"
)
set_property(
    SOURCE src/build_log.cc
    PROPERTY
    OBJECT_DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/log_schema.h"
        "${CMAKE_CURRENT_BINARY_DIR}/log_generated.h"
)

set_package_properties(PythonInterp PROPERTIES
    TYPE OPTIONAL
    DESCRIPTION "Python language interpreter."
    PURPOSE "Enables manifest browser."
)

configure_file(src/ninja_config.in.h ninja_config.h)

add_library(libninja-config INTERFACE)
target_compile_options(libninja-config INTERFACE ${ninja_compile_options})
target_compile_definitions(libninja-config INTERFACE ${ninja_compile_definitions})
target_include_directories(libninja-config INTERFACE "${CMAKE_CURRENT_BINARY_DIR}" ext/flatbuffers/include)
target_link_libraries(libninja-config INTERFACE flatbuffers "${NINJA_FILESYSTEM_LIBRARY}")

if (NINJA_SHARED_FOR_TESTS)
    add_library(libninja-objects OBJECT ${ninja_sources})
    target_link_libraries(libninja-objects PUBLIC libninja-config)
    target_compile_definitions(libninja-objects PRIVATE ${ninja_private_compile_definitions})

    add_library(libninja STATIC $<TARGET_OBJECTS:libninja-objects>)
    target_link_libraries(libninja PUBLIC libninja-config)
    set_property(TARGET libninja PROPERTY PREFIX "")

    set(CMAKE_LINK_DEPENDS_NO_SHARED ON)
    set_property(TARGET libninja-objects PROPERTY POSITION_INDEPENDENT_CODE ON)
    add_library(libninja-for-tests SHARED $<TARGET_OBJECTS:libninja-objects>)
    target_link_libraries(libninja-for-tests PUBLIC libninja-config)
    set_property(TARGET libninja-for-tests PROPERTY PREFIX "")
else()
    add_library(libninja STATIC ${ninja_sources})
    target_link_libraries(libninja PUBLIC libninja-config)
    target_compile_definitions(libninja PRIVATE ${ninja_private_compile_definitions})

    add_library(libninja-for-tests ALIAS libninja)
endif()

add_executable(ninja src/ninja_main.cc)
target_link_libraries(ninja libninja)

add_executable(majak src/majak_main.cc)
target_link_libraries(majak libninja)

if (NINJA_BUILD_TESTS)
    set(
        ninja_test_sources

        src/build_log_test.cc
        src/build_test.cc
        src/clean_test.cc
        src/clparser_test.cc
        src/depfile_parser_test.cc
        src/deps_log_test.cc
        src/disk_interface_test.cc
        src/graph_test.cc
        src/lexer_test.cc
        src/manifest_parser_test.cc
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
    target_link_libraries(ninja_test ${ninja_filesystem_library} libninja-for-tests gtest gtest_main)
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
        add_executable(${perftest_name} src/${perftest_name}.cc)
        target_link_libraries(${perftest_name} libninja-for-tests)
    endforeach()
    set_property(
        SOURCE src/manifest_parser_perftest.cc
        PROPERTY
        COMPILE_DEFINITIONS "CMAKE_CURRENT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    )

    foreach(perftest_name IN LISTS ninja_bench_perftests)
        add_executable(${perftest_name} src/${perftest_name}.cc)
        target_link_libraries(${perftest_name} libninja-for-tests benchmark)
    endforeach()

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
