cmake_minimum_required(VERSION 3.28...3.30)

add_library(pixelview-test-pattern MODULE)
add_library(OBS::pixelview-test-pattern ALIAS pixelview-test-pattern)

target_sources(pixelview-test-pattern PRIVATE pixelview-test-pattern.c test-pattern.c test-pattern.h)

target_link_libraries(pixelview-test-pattern PRIVATE OBS::libobs)

set_target_properties_obs(pixelview-test-pattern PROPERTIES FOLDER plugins PREFIX "")
