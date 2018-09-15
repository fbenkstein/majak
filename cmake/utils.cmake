# remove some default flags from MSVC because we want to set them differently
macro(fix_default_msvc_settings)
    foreach (flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/W3" "" ${flag_var} "${${flag_var}}")
        string(REPLACE "/GR" "" ${flag_var} "${${flag_var}}")
        string(REPLACE "/EHsc" "" ${flag_var} "${${flag_var}}")
    endforeach()
endmacro()

function(_define_boost_ninja_fs)
    find_package(Boost COMPONENTS filesystem)
    add_library(ninja-fs INTERFACE)
    target_include_directories(ninja-fs INTERFACE ${Boost_INCLUDE_DIRS})
    target_link_libraries(ninja-fs INTERFACE ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY})
endfunction()

function(ninja_find_filesystem_library)
    if (DEFINED NINJA_FILESYSTEM_INCLUDE AND DEFINED NINJA_FILESYSTEM_NAMESPACE)
        if (NINJA_FILESYSTEM_NAMESPACE STREQUAL "boost::filesystem")
            _define_boost_ninja_fs()
        endif()
        return()
    endif()

    macro (_ninja_define_filesystem)
        set(NINJA_FILESYSTEM_INCLUDE "${NINJA_FILESYSTEM_INCLUDE}" PARENT_SCOPE)
        set(NINJA_FILESYSTEM_INCLUDE "${NINJA_FILESYSTEM_INCLUDE}" CACHE INTERNAL "")
        set(NINJA_FILESYSTEM_NAMESPACE "${NINJA_FILESYSTEM_NAMESPACE}" PARENT_SCOPE)
        set(NINJA_FILESYSTEM_NAMESPACE "${NINJA_FILESYSTEM_NAMESPACE}" CACHE INTERNAL "")
        if (DEFINED NINJA_FILESYSTEM_LIBRARY)
            set(NINJA_FILESYSTEM_LIBRARY "${NINJA_FILESYSTEM_LIBRARY}" PARENT_SCOPE)
            set(NINJA_FILESYSTEM_LIBRARY "${NINJA_FILESYSTEM_LIBRARY}" CACHE INTERNAL "")
        endif()
    endmacro()

    set(
        filesystem_check_source
        [=[
        #include <@NINJA_FILESYSTEM_INCLUDE@>
        namespace fs = @NINJA_FILESYSTEM_NAMESPACE@;
        int main() { fs::path p = fs::current_path(); }
        ]=]
    )

    set(NINJA_FILESYSTEM_INCLUDE "<filesystem>")
    set(NINJA_FILESYSTEM_NAMESPACE "std::filesystem")
    string(CONFIGURE "${filesystem_check_source}" std_filesystem_check_source @ONLY)

    check_cxx_source_compiles("${std_filesystem_check_source}" NINJA_STD_FILESYSTEM_WORKS)

    if (NINJA_STD_FILESYSTEM_WORKS)
        _ninja_define_filesystem()
        return()
    endif()

    set(CMAKE_REQUIRED_LIBRARIES "stdc++fs")
    check_cxx_source_compiles("${std_filesystem_check_source}" NINJA_STD_FILESYSTEM_WITH_LIBSTDCXXFS_WORKS)
    unset(CMAKE_REQUIRED_LIBRARIES)

    if (NINJA_STD_FILESYSTEM_WITH_LIBSTDCXXFS_WORKS)
        set(NINJA_FILESYSTEM_LIBRARY "stdc++fs")
        _ninja_define_filesystem()
        return()
    endif()

    set(NINJA_FILESYSTEM_INCLUDE "experimental/filesystem")
    set(NINJA_FILESYSTEM_NAMESPACE "std::experimental::filesystem")
    string(CONFIGURE "${filesystem_check_source}" experimental_filesystem_check_source @ONLY)

    check_cxx_source_compiles("${experimental_filesystem_check_source}" NINJA_EXPERIMENTAL_FILESYSTEM_WORKS)

    if (NINJA_EXPERIMENTAL_FILESYSTEM_WORKS)
        _ninja_define_filesystem()
        return()
    endif()

    set(CMAKE_REQUIRED_LIBRARIES "stdc++fs")
    check_cxx_source_compiles("${experimental_filesystem_check_source}" NINJA_EXPERIMENTAL_FILESYSTEM_WITH_LIBSTDCXXFS_WORKS)
    unset(CMAKE_REQUIRED_LIBRARIES)

    if (NINJA_EXPERIMENTAL_FILESYSTEM_WITH_LIBSTDCXXFS_WORKS)
        set(NINJA_FILESYSTEM_LIBRARY "stdc++fs")
        _ninja_define_filesystem()
        return()
    endif()

    find_package(Boost COMPONENTS filesystem)

    if (Boost_FILESYSTEM_FOUND)
        set(NINJA_FILESYSTEM_INCLUDE "boost/filesystem.hpp")
        set(NINJA_FILESYSTEM_NAMESPACE "boost::filesystem")
        _define_boost_ninja_fs()
        set(NINJA_FILESYSTEM_LIBRARY "ninja-fs")
        _ninja_define_filesystem()
        return()
    endif()

    message(SEND_ERROR "no suitable filesystem library found")
    message(SEND_ERROR "please install Boost.Filesystem or update your compiler")
endfunction()
