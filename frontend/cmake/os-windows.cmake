if(NOT TARGET OBS::w32-pthreads)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/w32-pthreads" "${CMAKE_BINARY_DIR}/deps/w32-pthreads")
endif()

find_package(Detours REQUIRED)

# Pixelview never builds the OBS updater, What's New or their obsproject.com
# endpoints; WinSparkle replaces them when an appcast is configured.
include(cmake/feature-winsparkle.cmake)
find_package(nlohmann_json 3.11 REQUIRED)

configure_file(cmake/windows/obs.rc.in obs.rc)

target_sources(
  obs-studio
  PRIVATE
    cmake/windows/obs.manifest
    obs.rc
    utility/CrashHandler_Windows.cpp
    utility/NativeEventFilter_Windows.cpp
    utility/models/branches.hpp
    utility/platform-windows.cpp
    utility/system-info-windows.cpp
    utility/win-dll-blocklist.c
)

target_link_libraries(
  obs-studio
  PRIVATE
    crypt32
    OBS::w32-pthreads
    nlohmann_json::nlohmann_json
    Detours::Detours
)

target_compile_definitions(obs-studio PRIVATE PSAPI_VERSION=2)

target_link_options(obs-studio PRIVATE /IGNORE:4099 $<$<CONFIG:DEBUG>:/NODEFAULTLIB:MSVCRT>)

set_property(TARGET obs-studio APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT obs-studio)
set_target_properties(
  obs-studio
  PROPERTIES
    WIN32_EXECUTABLE TRUE
    VS_DEBUGGER_COMMAND "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit/$<TARGET_FILE_NAME:obs-studio>"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit"
)
