cmake_minimum_required(VERSION 3.28)

project(microagenda VERSION 0.1.2 LANGUAGES CXX C)

if(NOT UNIX OR APPLE)
  message(FATAL_ERROR "microagenda is Linux-only for this product scope.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(GNUInstallDirs)

enable_testing()

option(MICROAGENDA_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MICROAGENDA_ENABLE_SDL3_IMAGE "Enable SDL3_image when available" ON)
option(MICROAGENDA_ENABLE_SDL3_TTF "Enable SDL3_ttf when available" ON)
option(MICROAGENDA_PERF_HARNESS_BUILD "Build performance harness" ON)
option(MICROAGENDA_ASSERT_OFFLINE_BUILD "Fail if generated dependency-fetch hooks are present" ON)

find_package(PkgConfig QUIET)

find_package(SDL3 CONFIG QUIET)
set(MICROAGENDA_SDL3_TARGET "")
if(TARGET SDL3::SDL3)
  set(MICROAGENDA_SDL3_TARGET SDL3::SDL3)
elseif(TARGET SDL3::SDL3-shared)
  set(MICROAGENDA_SDL3_TARGET SDL3::SDL3-shared)
elseif(TARGET SDL3::SDL3-static)
  set(MICROAGENDA_SDL3_TARGET SDL3::SDL3-static)
elseif(PkgConfig_FOUND)
  pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
  if(TARGET PkgConfig::SDL3)
    set(MICROAGENDA_SDL3_TARGET PkgConfig::SDL3)
  endif()
endif()
if(MICROAGENDA_SDL3_TARGET STREQUAL "")
  message(FATAL_ERROR "SDL3 development files were not found. Install SDL3 and rerun CMake.")
endif()

set(MICROAGENDA_SQLITE_TARGET "")
find_package(SQLite3 QUIET)
if(TARGET SQLite::SQLite3)
  set(MICROAGENDA_SQLITE_TARGET SQLite::SQLite3)
elseif(PkgConfig_FOUND)
  pkg_check_modules(SQLITE3 QUIET IMPORTED_TARGET sqlite3)
  if(TARGET PkgConfig::SQLITE3)
    set(MICROAGENDA_SQLITE_TARGET PkgConfig::SQLITE3)
  endif()
endif()
if(MICROAGENDA_SQLITE_TARGET STREQUAL "")
  find_path(MICROAGENDA_SQLITE_INCLUDE_DIR sqlite3.h)
  find_library(MICROAGENDA_SQLITE_LIBRARY sqlite3)
  if(MICROAGENDA_SQLITE_INCLUDE_DIR AND MICROAGENDA_SQLITE_LIBRARY)
    add_library(microagenda_sqlite INTERFACE)
    target_include_directories(microagenda_sqlite INTERFACE "${MICROAGENDA_SQLITE_INCLUDE_DIR}")
    target_link_libraries(microagenda_sqlite INTERFACE "${MICROAGENDA_SQLITE_LIBRARY}")
    set(MICROAGENDA_SQLITE_TARGET microagenda_sqlite)
  else()
    message(FATAL_ERROR "SQLite3 development files were not found. Install sqlite3 headers and rerun CMake.")
  endif()
endif()

set(MICROAGENDA_SDL3_IMAGE_TARGET "")
if(MICROAGENDA_ENABLE_SDL3_IMAGE)
  find_package(SDL3_image CONFIG QUIET)
  if(TARGET SDL3_image::SDL3_image)
    set(MICROAGENDA_SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
  elseif(PkgConfig_FOUND)
    pkg_check_modules(SDL3_IMAGE QUIET IMPORTED_TARGET SDL3_image)
    if(TARGET PkgConfig::SDL3_IMAGE)
      set(MICROAGENDA_SDL3_IMAGE_TARGET PkgConfig::SDL3_IMAGE)
    endif()
  endif()
endif()

set(MICROAGENDA_SDL3_TTF_TARGET "")
if(MICROAGENDA_ENABLE_SDL3_TTF)
  find_package(SDL3_ttf CONFIG QUIET)
  if(TARGET SDL3_ttf::SDL3_ttf)
    set(MICROAGENDA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
  elseif(PkgConfig_FOUND)
    pkg_check_modules(SDL3_TTF QUIET IMPORTED_TARGET SDL3_ttf)
    if(TARGET PkgConfig::SDL3_TTF)
      set(MICROAGENDA_SDL3_TTF_TARGET PkgConfig::SDL3_TTF)
    endif()
  endif()
endif()

if(MICROAGENDA_ASSERT_OFFLINE_BUILD)
  file(GLOB_RECURSE MICROAGENDA_FETCH_MARKERS
    "${CMAKE_SOURCE_DIR}/cmake/*.cmake"
  )
  set(MICROAGENDA_FETCH_TOKEN_A "Fetch")
  set(MICROAGENDA_FETCH_TOKEN_B "Content")
  set(MICROAGENDA_EXTERNAL_TOKEN_A "External")
  set(MICROAGENDA_EXTERNAL_TOKEN_B "Project_Add")
  set(MICROAGENDA_URL_TOKEN "://")
  foreach(marker_file IN LISTS MICROAGENDA_FETCH_MARKERS)
    file(READ "${marker_file}" marker_contents)
    if(marker_contents MATCHES "${MICROAGENDA_FETCH_TOKEN_A}${MICROAGENDA_FETCH_TOKEN_B}|${MICROAGENDA_EXTERNAL_TOKEN_A}${MICROAGENDA_EXTERNAL_TOKEN_B}|git[ ]+clone|${MICROAGENDA_URL_TOKEN}")
      message(FATAL_ERROR "Offline build invariant failed: ${marker_file} contains dependency-fetch syntax.")
    endif()
  endforeach()
endif()

add_library(md4c STATIC third_party/md4c/src/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c/src)
add_library(md4c_html STATIC third_party/md4c/src/md4c-html.c third_party/md4c/src/entity.c)
target_include_directories(md4c_html PUBLIC third_party/md4c/src)
target_link_libraries(md4c_html PUBLIC md4c)

set(MICROAGENDA_CORE_SOURCES
  src/agenda/AgendaStore.cpp
  src/attachments/AttachmentService.cpp
  src/editor/MarkdownEditor.cpp
  src/editor/SoftWrap.cpp
  src/markdown/MarkdownParser.cpp
  src/markdown/RenderModel.cpp
  src/perf/Perf.cpp
  src/platform/DefaultOpener.cpp
  src/platform/DurableFile.cpp
  src/platform/PathUtils.cpp
  src/ui/AppState.cpp
)

add_library(microagenda_core STATIC ${MICROAGENDA_CORE_SOURCES})
target_include_directories(microagenda_core PUBLIC src)
target_link_libraries(microagenda_core PUBLIC md4c md4c_html ${MICROAGENDA_SQLITE_TARGET} ${MICROAGENDA_SDL3_TARGET})
target_compile_definitions(microagenda_core PUBLIC
  MICROAGENDA_HAS_SDL3_IMAGE=$<BOOL:${MICROAGENDA_SDL3_IMAGE_TARGET}>
  MICROAGENDA_HAS_SDL3_TTF=$<BOOL:${MICROAGENDA_SDL3_TTF_TARGET}>
)

add_executable(microagenda src/app/main.cpp src/app/Application.cpp)
target_include_directories(microagenda PRIVATE src)
target_link_libraries(microagenda PRIVATE microagenda_core ${MICROAGENDA_SDL3_TARGET})
if(NOT MICROAGENDA_SDL3_IMAGE_TARGET STREQUAL "")
  target_link_libraries(microagenda PRIVATE ${MICROAGENDA_SDL3_IMAGE_TARGET})
endif()
if(NOT MICROAGENDA_SDL3_TTF_TARGET STREQUAL "")
  target_link_libraries(microagenda PRIVATE ${MICROAGENDA_SDL3_TTF_TARGET})
endif()

add_executable(microagenda_tests
  tests/TestMain.cpp
  tests/AppStateTests.cpp
  tests/AgendaStoreTests.cpp
  tests/AttachmentTests.cpp
  tests/EditorViewerTests.cpp
  tests/MarkdownParserTests.cpp
  tests/PathUtilsTests.cpp
)
target_include_directories(microagenda_tests PRIVATE src tests)
target_link_libraries(microagenda_tests PRIVATE microagenda_core)
add_test(NAME microagenda_tests COMMAND microagenda_tests)
set_tests_properties(microagenda_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

if(MICROAGENDA_PERF_HARNESS_BUILD)
  add_executable(microagenda_perf tools/PerfMain.cpp)
  target_include_directories(microagenda_perf PRIVATE src)
  target_link_libraries(microagenda_perf PRIVATE microagenda_core)
endif()

foreach(target microagenda_core microagenda microagenda_tests)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(MICROAGENDA_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endforeach()

set(MICROAGENDA_DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/microagenda.desktop")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/packaging/microagenda.desktop.in"
  "${MICROAGENDA_DESKTOP_FILE}"
  @ONLY
)

install(TARGETS microagenda RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${MICROAGENDA_DESKTOP_FILE}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/microagenda.svg" DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "${PROJECT_NAME}")
set(CPACK_PACKAGE_CONTACT "microagenda")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "microagenda - a small Linux agenda app")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
