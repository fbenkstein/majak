find_package(Git)

if(__get_git_version_info)
  return()
endif()
set(__get_git_version_info INCLUDED)

function(get_git_version_info prefix)
  if(GIT_EXECUTABLE)
      execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --abbrev=8
          WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
          RESULT_VARIABLE status
          OUTPUT_VARIABLE GIT_VERSION
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
      if(${status})
          set(GIT_VERSION "v0.0.0")
      endif()

      # Work out if the repository is dirty
      execute_process(COMMAND ${GIT_EXECUTABLE} update-index -q --refresh
          WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
          OUTPUT_QUIET
          ERROR_QUIET)
      execute_process(COMMAND ${GIT_EXECUTABLE} diff-index --name-only HEAD --
          WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_DIFF_INDEX
          ERROR_QUIET)
      execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
          WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_COMMIT_ID
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
      string(COMPARE NOTEQUAL "${GIT_DIFF_INDEX}" "" GIT_DIRTY)
      if (${GIT_DIRTY})
          set(GIT_VERSION "${GIT_VERSION}-dirty")
      endif()
  else()
      set(GIT_VERSION "v0.0.0")
      set(GIT_COMMIT_ID "")
  endif()

  message(STATUS "${PROJECT_NAME} git version: ${GIT_VERSION}")
  message(STATUS "${PROJECT_NAME} git commit id: ${GIT_COMMIT_ID}")
  set(${prefix}_VERSION ${GIT_VERSION} PARENT_SCOPE)
  set(${prefix}_COMMIT_ID ${GIT_COMMIT_ID} PARENT_SCOPE)
endfunction()
