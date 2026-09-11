cmake_minimum_required(VERSION 3.28...3.30)

# Runtime staging is deliberately mandatory on the macOS receiver target.
set(PIXELVIEW_GST_SDK "${CMAKE_SOURCE_DIR}/.deps/gstreamer-upstream-1.28.3/sdk")
if(NOT EXISTS "${PIXELVIEW_GST_SDK}/upstream-provenance.json")
  message(FATAL_ERROR "Missing official GStreamer SDK. Run scripts/fetch-gstreamer.py in plugins/pixelview-whep.")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PIXELVIEW_GST_RUNTIME "${CMAKE_SOURCE_DIR}/.deps/pixelview-gstreamer" CACHE PATH "Pinned curated GStreamer runtime staging directory")
if(NOT EXISTS "${PIXELVIEW_GST_RUNTIME}/sbom.json")
  message(FATAL_ERROR "Missing bundled WHEP runtime. Run python3 plugins/pixelview-whep/scripts/bundle-runtime.py stage .deps/pixelview-gstreamer (pixelview-build.sh does this automatically).")
endif()

add_library(pixelview-whep MODULE pixelview-whep.c runtime.h
 video-format.c video-format.h profile-offer.c profile-offer.h
 capability-probe.c capability-probe.h capability-fixtures.h
 native-422.m native-422.h native-422-filter.c native-422-filter.h)
add_library(OBS::pixelview-whep ALIAS pixelview-whep)
target_include_directories(pixelview-whep PRIVATE
 "${PIXELVIEW_GST_SDK}/include/gstreamer-1.0"
 "${PIXELVIEW_GST_SDK}/include/glib-2.0"
 "${PIXELVIEW_GST_SDK}/lib/glib-2.0/include")
# Link only staged dylibs; do not discover host/global GStreamer installations.
target_link_directories(pixelview-whep PRIVATE "${PIXELVIEW_GST_RUNTIME}/lib")
target_link_libraries(pixelview-whep PRIVATE OBS::libobs gstapp-1.0.0 gstvideo-1.0.0 gstaudio-1.0.0 gstbase-1.0.0 gstreamer-1.0.0 gobject-2.0.0 glib-2.0.0)
target_link_options(pixelview-whep PRIVATE "LINKER:-headerpad_max_install_names")
target_link_libraries(pixelview-whep PRIVATE gstcodecparsers-1.0.0 gstrtp-1.0.0
 "-framework Foundation" "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo")
set_target_properties_obs(pixelview-whep PROPERTIES FOLDER plugins PREFIX "")
set_property(TARGET pixelview-whep APPEND PROPERTY BUILD_RPATH "@loader_path/../Resources/GStreamer/lib")
add_custom_command(TARGET pixelview-whep POST_BUILD
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle-runtime.py" embed
 "${PIXELVIEW_GST_RUNTIME}" "$<TARGET_BUNDLE_CONTENT_DIR:pixelview-whep>" "$<TARGET_FILE:pixelview-whep>"
 "--identity=${OBS_CODESIGN_IDENTITY}"
 COMMENT "Embed/sign the curated WHEP runtime before plugin/app signing" VERBATIM)
