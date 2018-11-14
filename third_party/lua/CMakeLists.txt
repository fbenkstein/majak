project(majak_lua)

file(GLOB majak_lua_sources "lua/src/*.c")
file(GLOB majak_lua_interpreter_sources "lua/src/lua.c")
file(GLOB majak_lua_compiler_sources "lua/src/luac.c")
list(REMOVE_ITEM majak_lua_sources ${majak_lua_main_sources} ${majak_lua_compiler_sources})

add_library(majak_lua STATIC ${majak_lua_sources})
target_include_directories(majak_lua PUBLIC lua/src)

add_executable(majak_lua_interpreter ${majak_lua_interpreter_sources})
set_property(TARGET majak_lua_interpreter PROPERTY OUTPUT_NAME majak_luai)
target_link_libraries(majak_lua_interpreter majak_lua)
add_executable(majak_lua_compiler ${majak_lua_compiler_sources})
set_property(TARGET majak_lua_compiler PROPERTY OUTPUT_NAME majak_luac)
target_link_libraries(majak_lua_compiler majak_lua)

# compile lua as C++
foreach(source_file IN LISTS majak_lua_sources majak_lua_main_sources)
    set_property(SOURCE "${source_file}" PROPERTY LANGUAGE CXX)
endforeach()
if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(majak_lua PRIVATE -x c++)
    target_compile_options(majak_lua_compiler PRIVATE -x c++)
    target_compile_options(majak_lua_interpreter PRIVATE -x c++)
endif()
