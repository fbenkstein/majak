project(re2c)

file(GLOB_RECURSE RE2C_SOURCES "re2c/re2c/src/*.cc")
list(FILTER RE2C_SOURCES EXCLUDE REGEX "/test/")
file(GLOB_RECURSE RE2C_BOOTSTRAP_SOURCES "re2c/re2c/bootstrap/*.cc")
add_executable(re2c ${RE2C_SOURCES} ${RE2C_BOOTSTRAP_SOURCES})
target_include_directories(re2c PRIVATE re2c/re2c "${CMAKE_CURRENT_BINARY_DIR}")
set(RE2C_VERSION "1.1.1-ninja")
configure_file(config.h.in config.h)
