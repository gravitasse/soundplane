# soundplane/CMakeLists.txt
# root CMake file for soundplane project.

#

cmake_minimum_required(VERSION 2.8.12)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

project(soundplane)

set(PROJECT_NAME Soundplane)
set(SP_VERSION_MAJOR 1)
set(SP_VERSION_MINOR 7)
set(SP_VERSION_UPDATE 0)
set(SP_VERSION "${SP_VERSION_MAJOR}.${SP_VERSION_MINOR}.${SP_VERSION_UPDATE}")

if(APPLE)
  set(EXECUTABLE_NAME Soundplane)

else(APPLE)
  set(EXECUTABLE_NAME soundplane)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
  add_definitions(
    -DDEBUG
  )
endif()

set(ML_JUCE_HEADER_PATH "${CMAKE_SOURCE_DIR}/Source")

# set the path to madronalib here

if(NOT SP_MADRONALIB_DIR)
  set(SP_MADRONALIB_DIR "${CMAKE_SOURCE_DIR}/../madronalib")
endif()

# set options to turn off extras and include madronalib

set(BUILD_NEW_ONLY OFF CACHE BOOL "Build the examples" FORCE)
set(ML_BUILD_EXAMPLES OFF CACHE BOOL "Build the examples" FORCE)
set(ML_BUILD_TESTS OFF CACHE BOOL "Build the examples" FORCE)

add_subdirectory(${SP_MADRONALIB_DIR} madronalib)
add_subdirectory(SoundplaneLib)

# add_subdirectory(${SP_MADRONALIB_DIR}/external/juce)

set(SP_SOURCES
  Data/SoundplaneBinaryData/SoundplaneBinaryData.cpp
  Data/SoundplaneBinaryData/SoundplaneBinaryData.h
  Source/AppConfig.h  
  Source/Controller.h
  Source/JuceHeader.h
  Source/MLProjectInfo.h
  Source/SoundplaneApp.cpp
  Source/SoundplaneApp.h
  Source/SoundplaneController.cpp
  Source/SoundplaneController.h
  Source/SoundplaneOutput.h
  Source/SoundplaneGridView.cpp
  Source/SoundplaneGridView.h
  Source/SoundplaneMIDIOutput.cpp
  Source/SoundplaneMIDIOutput.h
  Source/SoundplaneModel.cpp
  Source/SoundplaneModel.h
  Source/SoundplaneOSCOutput.cpp
  Source/SoundplaneOSCOutput.h
  Source/SoundplaneOutput.h
  Source/SoundplaneTouchGraphView.cpp
  Source/SoundplaneTouchGraphView.h
  Source/SoundplaneView.cpp
  Source/SoundplaneView.h
  Source/SoundplaneZoneView.cpp
  Source/SoundplaneZoneView.h
  Source/Touch.h
  Source/TouchTracker.cpp
  Source/TouchTracker.h
  Source/Zone.cpp
  Source/Zone.h
)

set(ICON_FULL_PATH "Data/soundplane.icns")
set(PLIST_FULL_PATH "Data/Info.plist.in")
set(SP_DATA
  ${ICON_FULL_PATH}
  ${PLIST_FULL_PATH}
  )

set(SP_CODE_SIGN_IDENTITY "" CACHE STRING "Code sign identity")

add_executable(
  ${EXECUTABLE_NAME}
  MACOSX_BUNDLE
  ${SP_DATA}
  ${SP_SOURCES})

include_directories("${SP_MADRONALIB_DIR}/external")
include_directories("${SP_MADRONALIB_DIR}/external/utf")

#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/external/")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/external/juce/JUCE")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/source/core")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/source/DSP")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/source/MLApp")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/source/MLJuceApp")
#target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/madronalib/source/LookAndFeel")


target_include_directories(${EXECUTABLE_NAME} PRIVATE Data/SoundplaneBinaryData)

target_include_directories(${EXECUTABLE_NAME} PRIVATE "${ML_JUCE_DIR}")
target_include_directories(${EXECUTABLE_NAME} PRIVATE Data/SoundplaneBinaryData)

target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/SoundplaneLib/")

set_source_files_properties(${ICON_FULL_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set_target_properties(${EXECUTABLE_NAME}
  PROPERTIES
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${SP_CODE_SIGN_IDENTITY}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/${PLIST_FULL_PATH}"
  MACOSX_BUNDLE_ICON_FILE soundplane.icns
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.madronalabs.soundplane"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${SP_VERSION}"
  MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
  )

target_link_libraries(${EXECUTABLE_NAME} madronalib)
target_link_libraries(${EXECUTABLE_NAME} soundplanelib)


install(TARGETS ${EXECUTABLE_NAME} DESTINATION usr/bin)


# Linux package generation
install(FILES Data/59-soundplane.rules DESTINATION /lib/udev/rules.d)
install(
  FILES Data/postinst
  PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  DESTINATION DEBIAN)

set(PACKAGE_NAME ${PROJECT_NAME})
set(PACKAGE_VERSION "${SP_VERSION}")
set(PACKAGE_RELEASE 0)
set(PACKAGE_MAINTAINER_NAME "Randy Jones")
set(PACKAGE_MAINTAINER_EMAIL "randy@madronalabs.com")
set(PACKAGE_DESCRIPTION "Client Software for the Madrona Labs Soundplane")
set(PACKAGE_DESCRIPTION_SUMMARY "Soundplane Client")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "libx11-6 (>= 1.6.2), libusb-1.0-0 (>= 1.0.17), libfreetype6 (>= 2.5.2), libxrandr2 (>= 1.4.2), libxinerama1 (>= 1.1.3), libxcursor1 (>= 1.1.14), libasound2 (>= 1.0.27), freeglut3 (>= 2.8.1), libavahi-compat-libdnssd1 (>= 0.6.31)")

include(CPack)

# Debian packages
include(cmake/DpkgDeb.cmake)
if(DPKG_FOUND)
  add_debian_package(${PROJECT_NAME})
endif()
