option(PIXELVIEW_RELEASE_BUILD "Enable fail-closed Pixelview release checks" OFF)
set(PIXELVIEW_WINSPARKLE_ROOT "" CACHE PATH "Extracted pinned WinSparkle binary package (cmake/windows/pixelview-build.py stages it)")

if(PIXELVIEW_RELEASE_BUILD)
  if(NOT CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
    message(FATAL_ERROR "Pixelview Windows releases currently support x64 only")
  endif()
  if(NOT SPARKLE_APPCAST_URL STREQUAL "https://downloads.pixelview.io/desktop/windows/appcast-x64.xml")
    message(FATAL_ERROR "Pixelview releases require the production downloads.pixelview.io x64 appcast")
  endif()
  if(NOT SPARKLE_PUBLIC_KEY)
    message(FATAL_ERROR "Pixelview releases require the Pixelview WinSparkle public key")
  endif()
endif()
if(SPARKLE_APPCAST_URL MATCHES "obsproject\\.com")
  message(FATAL_ERROR "Pixelview must never use OBS update infrastructure")
endif()

if(SPARKLE_APPCAST_URL AND SPARKLE_PUBLIC_KEY)
  cmake_path(ABSOLUTE_PATH PIXELVIEW_WINSPARKLE_ROOT NORMALIZE OUTPUT_VARIABLE _winsparkle_root)
  set(_winsparkle_arch "${CMAKE_VS_PLATFORM_NAME}")
  if(NOT EXISTS "${_winsparkle_root}/include/winsparkle.h"
     OR NOT EXISTS "${_winsparkle_root}/${_winsparkle_arch}/Release/WinSparkle.dll"
  )
    message(FATAL_ERROR "WinSparkle is not staged at '${PIXELVIEW_WINSPARKLE_ROOT}'. Run cmake/windows/pixelview-build.py.")
  endif()
  add_library(WinSparkle::WinSparkle SHARED IMPORTED)
  set_target_properties(
    WinSparkle::WinSparkle
    PROPERTIES
      IMPORTED_LOCATION "${_winsparkle_root}/${_winsparkle_arch}/Release/WinSparkle.dll"
      IMPORTED_IMPLIB "${_winsparkle_root}/${_winsparkle_arch}/Release/WinSparkle.lib"
      INTERFACE_INCLUDE_DIRECTORIES "${_winsparkle_root}/include"
  )
  target_sources(obs-studio PRIVATE utility/PixelviewSparkle.hpp utility/PixelviewWinSparkle.cpp)
  target_link_libraries(obs-studio PRIVATE WinSparkle::WinSparkle)
  target_compile_definitions(
    obs-studio
    PRIVATE "PIXELVIEW_SPARKLE_APPCAST_URL=\"${SPARKLE_APPCAST_URL}\""
            "PIXELVIEW_SPARKLE_PUBLIC_KEY=\"${SPARKLE_PUBLIC_KEY}\""
  )
  target_enable_feature(obs-studio "Pixelview WinSparkle updater" ENABLE_SPARKLE_UPDATER)
  unset(_winsparkle_root)
  unset(_winsparkle_arch)
else()
  target_disable_feature(obs-studio "Pixelview WinSparkle updater")
endif()
