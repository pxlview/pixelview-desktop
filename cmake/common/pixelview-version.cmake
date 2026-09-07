# Pixelview product version and pinned OBS base.
# version.json is the single source of truth used by CMake and release tooling.
include_guard(GLOBAL)

set(_pixelview_version_file "${CMAKE_SOURCE_DIR}/version.json")
if(NOT EXISTS "${_pixelview_version_file}")
  message(FATAL_ERROR "Missing Pixelview version metadata: ${_pixelview_version_file}")
endif()

file(READ "${CMAKE_SOURCE_DIR}/version.json" _pixelview_version_json)
string(JSON PIXELVIEW_VERSION GET "${_pixelview_version_json}" pixelview_version)
string(JSON PIXELVIEW_BUILD_NUMBER GET "${_pixelview_version_json}" pixelview_build_number)
string(JSON PIXELVIEW_OBS_BASE_VERSION GET "${_pixelview_version_json}" obs_base_version)
string(JSON PIXELVIEW_OBS_BASE_DESCRIBE GET "${_pixelview_version_json}" obs_base_describe)
string(JSON PIXELVIEW_OBS_BASE_COMMIT GET "${_pixelview_version_json}" obs_base_commit)

if(NOT PIXELVIEW_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "pixelview_version must use MAJOR.MINOR.PATCH")
endif()
if(NOT PIXELVIEW_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "pixelview_build_number must be a positive integer")
endif()
if(NOT PIXELVIEW_OBS_BASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "obs_base_version must use MAJOR.MINOR.PATCH")
endif()
if(NOT PIXELVIEW_OBS_BASE_DESCRIBE MATCHES "^${PIXELVIEW_OBS_BASE_VERSION}-[0-9]+-g[0-9a-f]+$")
  message(FATAL_ERROR "obs_base_describe must be a long git describe based on obs_base_version")
endif()
string(LENGTH "${PIXELVIEW_OBS_BASE_COMMIT}" _pixelview_obs_commit_length)
if(NOT _pixelview_obs_commit_length EQUAL 40 OR NOT PIXELVIEW_OBS_BASE_COMMIT MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "obs_base_commit must be a full lowercase Git commit hash")
endif()

if(NOT DEFINED PIXELVIEW_SOURCE_COMMIT)
  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE PIXELVIEW_SOURCE_COMMIT
    RESULT_VARIABLE _pixelview_git_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _pixelview_git_result EQUAL 0)
    set(PIXELVIEW_SOURCE_COMMIT "unknown")
  endif()
endif()
if(NOT DEFINED PIXELVIEW_SOURCE_TAG)
  set(PIXELVIEW_SOURCE_TAG "v${PIXELVIEW_VERSION}")
endif()

unset(_pixelview_git_result)
unset(_pixelview_obs_commit_length)
unset(_pixelview_version_file)
unset(_pixelview_version_json)
