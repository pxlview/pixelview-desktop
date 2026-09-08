# Restricted Apple entitlement is opt-in: ad-hoc/development builds keep custom links.
option(PIXELVIEW_ENABLE_UNIVERSAL_LINKS "Enable provisioned play.pixelview.io Universal Links" OFF)
set(PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE "" CACHE FILEPATH "Developer ID provisioning profile for Pixelview Associated Domains")
if(PIXELVIEW_ENABLE_UNIVERSAL_LINKS)
  if(NOT OBS_CODESIGN_IDENTITY OR OBS_CODESIGN_IDENTITY STREQUAL "-" OR
     NOT OBS_CODESIGN_TEAM STREQUAL "MA47F3M8W9" OR
     NOT EXISTS "${PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE}")
    message(FATAL_ERROR "Universal Links require team MA47F3M8W9 signing and an Associated Domains provisioning profile; disable PIXELVIEW_ENABLE_UNIVERSAL_LINKS for ad-hoc builds.")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/validate-associated-domains.py"
    "${PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE}" RESULT_VARIABLE profile_result
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT profile_result EQUAL 0)
    message(FATAL_ERROR "Associated Domains profile is invalid, expired or not authorized for this app/team/domain.")
  endif()
  set_target_properties(obs-studio PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
    "${CMAKE_CURRENT_LIST_DIR}/macos/pixelview-associated-domains.plist")
  # Must be Contents/embedded.provisionprofile, not Resources; Xcode signs after copying.
  configure_file("${PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE}" "${CMAKE_CURRENT_BINARY_DIR}/embedded.provisionprofile" COPYONLY)
  target_sources(obs-studio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/embedded.provisionprofile")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/embedded.provisionprofile" PROPERTIES MACOSX_PACKAGE_LOCATION ".")
endif()
