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
    "Build ninja tests."
    ON
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
endif()

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

    set(
        std_filesystem_check_source
        [=[
        #if __has_include(<filesystem>)
        #include <filesystem>
        namespace fs = std::filesystem;
        #elif __has_include(<experimental/filesystem>)
        #include <experimental/filesystem>
        namespace fs = std::experimental::filesystem;
        #else
        #error no filesystem library available
        #endif

        int main() { fs::path p = fs::current_path(); }
        ]=]
    )

    check_cxx_source_compiles(std_filesystem_check_source NINJA_STD_FILESYSTEM_WORKS)

    if (NOT NINJA_STD_FILESYSTEM_WITHOUT_LIBSTDCXXFS)
        set(CMAKE_REQUIRED_LIBRARIES "stdc++fs")
        check_cxx_source_compiles("${std_filesystem_check_source}" NINJA_STD_FILESYSTEM_WORKS_WITH_LIBSTDCXXFS)

        if (NINJA_STD_FILESYSTEM_WORKS_WITH_LIBSTDCXXFS)
            set(ninja_filesystem_library stdc++fs)
        endif()
        unset(CMAKE_REQUIRED_LIBRARIES)
    endif()
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
        /GR-  # Disable RTTI.
        # Disable size_t -> int truncation warning.
        # We never have strings or arrays larger than 2**31.
        /wd4267
    )
else()
    set(
        ninja_compile_options

        -Wall
        -Wextra
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-implicit-fallthrough
        -fno-exceptions
        -iquote "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND ninja_compile_definitions _GLIBCXX_DEBUG _GLIBCXX_DEBUG_PEDANTIC)
    else()
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

set_package_properties(PythonInterp PROPERTIES
    TYPE OPTIONAL
    DESCRIPTION "Python language intereter."
    PURPOSE "Enables manifest browser."
)

configure_file(src/ninja_config.in.h ninja_config.h)

add_library(libninja STATIC ${ninja_sources})
set_property(TARGET libninja PROPERTY PREFIX "")
set_property(TARGET libninja PROPERTY CXX_VISIBILITY_PRESET hidden)
target_compile_options(libninja PUBLIC ${ninja_compile_options})
target_compile_definitions(
    libninja
    PUBLIC ${ninja_compile_definitions}
    PRIVATE ${ninja_private_compile_definitions}
)
target_include_directories(libninja PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

if (ninja_filesystem_library)
    target_link_libraries(libninja PRIVATE "${ninja_filesystem_library}")
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
        src/edit_distance_test.cc
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
    target_link_libraries(ninja_test libninja gtest gtest_main)
    if (COMMAND gtest_discover_tests)
        gtest_discover_tests(ninja_test)
    else()
        add_test(NAME ninja_test COMMAND ninja_test)
    endif()

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
