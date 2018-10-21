#=============================================================================
# Copyright (c) 2015, julp
#
# Distributed under the OSI-approved BSD License
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#=============================================================================

include(CMakeParseArguments)

function(RE2C_TARGET)
    cmake_parse_arguments(PARSED_ARGS "" "NAME;INPUT;OUTPUT" "OPTIONS;DEPENDS" ${ARGN})

    if(NOT PARSED_ARGS_OUTPUT)
        message(FATAL_ERROR "RE2C_TARGET expect an output filename")
    endif(NOT PARSED_ARGS_OUTPUT)
    if(NOT PARSED_ARGS_INPUT)
        message(FATAL_ERROR "RE2C_TARGET expect an input filename")
    endif(NOT PARSED_ARGS_INPUT)
    if(NOT PARSED_ARGS_NAME)
        message(FATAL_ERROR "RE2C_TARGET expect a target name")
    endif(NOT PARSED_ARGS_NAME)
    # TODO:
    # - get_filename_component(PARSED_ARGS_INPUT ${PARSED_ARGS_INPUT} ABSOLUTE)
    # - get_filename_component(PARSED_ARGS_OUTPUT ${PARSED_ARGS_OUTPUT} ABSOLUTE)
    # ?
    add_custom_command(
        OUTPUT ${PARSED_ARGS_OUTPUT}
        COMMAND ${NINJA_RE2C_EXECUTABLE} ${PARSED_ARGS_OPTIONS} -o ${PARSED_ARGS_OUTPUT} ${PARSED_ARGS_INPUT}
        DEPENDS ${PARSED_ARGS_INPUT} ${PARSED_ARGS_DEPENDS}
        COMMENT "[RE2C][${PARSED_ARGS_NAME}] Building lexer with re2c"
    )
    add_custom_target(
        ${PARSED_ARGS_NAME}
        SOURCES ${PARSED_ARGS_INPUT}
        DEPENDS ${PARSED_ARGS_OUTPUT}
    )
endfunction(RE2C_TARGET)
