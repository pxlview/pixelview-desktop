target_sources(
  obs-studio
  PRIVATE
    utility/CrashHandler_FreeBSD.cpp
    utility/NativeEventFilter.cpp
    utility/platform-x11.cpp
    utility/system-info-posix.cpp
)
target_compile_definitions(obs-studio PRIVATE OBS_INSTALL_PREFIX="${OBS_INSTALL_PREFIX}")
target_link_libraries(obs-studio PRIVATE Qt::DBus procstat)

if(Qt6_VERSION AND Qt6_VERSION VERSION_LESS "6.9.0")
  target_link_libraries(obs-studio PRIVATE Qt::GuiPrivate)
endif()

find_package(Libpci REQUIRED)
target_link_libraries(obs-studio PRIVATE Libpci::pci)

if(TARGET OBS::python)
  find_package(Python REQUIRED COMPONENTS Interpreter Development)
  target_link_libraries(obs-studio PRIVATE Python::Python)
  target_link_options(obs-studio PRIVATE LINKER:-no-as-needed)
endif()

# Pixelview launcher identity is separate from the upstream OBS installation.
install(
  FILES cmake/linux/com.pixelview.desktop.metainfo.xml
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
)

install(FILES cmake/linux/com.pixelview.desktop.desktop DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")

install(
  FILES data/images/pixelview-app.png
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/1024x1024/apps"
  RENAME com.pixelview.desktop.png
)
