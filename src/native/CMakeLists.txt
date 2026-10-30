cmake_minimum_required(VERSION 3.22)
project(sun_gobject_native LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUILD_TESTING "Build native support for the Sun test suite" ON)
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
   AND NOT (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"))
  message(FATAL_ERROR "Supported native targets are x86_64 Linux and Apple Silicon macOS")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4 gio-2.0 gobject-2.0 glib-2.0)
file(WRITE "${CMAKE_BINARY_DIR}/native-versions.txt" "gtk4=${GTK_gtk4_VERSION}\nglib=${GTK_glib-2.0_VERSION}\ngobject=${GTK_gobject-2.0_VERSION}\ngio=${GTK_gio-2.0_VERSION}\n")
# Group native mechanisms separately from custom widget behavior.
set(SUN_GOBJECT_NATIVE_SOURCES
  gobject/signals.c
  gtk/runtime/ui.c
  gtk/widgets/view.c
  gtk/widgets/media.c
  gtk/widgets/image.c
  gtk/surfaces/gl_area.c
)
add_library(sun_gobject_native STATIC ${SUN_GOBJECT_NATIVE_SOURCES})
target_include_directories(sun_gobject_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sun_gobject_native PRIVATE PkgConfig::GTK)
target_compile_options(sun_gobject_native PRIVATE -Wall -Wextra -Werror)
if(BUILD_TESTING)
  add_library(sun_gobject_test_support STATIC tests/probes.c ${SUN_GOBJECT_NATIVE_SOURCES})
  target_include_directories(sun_gobject_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(sun_gobject_test_support PRIVATE PkgConfig::GTK)
  target_compile_options(sun_gobject_test_support PRIVATE -Wall -Wextra -Werror)
endif()
install(TARGETS sun_gobject_native ARCHIVE DESTINATION lib)
