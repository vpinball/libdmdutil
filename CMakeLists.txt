cmake_minimum_required(VERSION 3.25)

set(PLATFORM "win" CACHE STRING "Platform")
set(ARCH "x64" CACHE STRING "Arch")

option(BUILD_SHARED "Option to build shared library" ON)
option(BUILD_STATIC "Option to build static library" ON)
option(POST_BUILD_COPY_EXT_LIBS "Option to copy external libraries to build directory" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan for Debug builds" OFF)
set(ENABLE_PIN2DMD_DEFAULT OFF)
if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
   set(ENABLE_PIN2DMD_DEFAULT ON)
endif()
option(ENABLE_PIN2DMD "Enable PIN2DMD support" ${ENABLE_PIN2DMD_DEFAULT})

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "ARCH: ${ARCH}")

message(STATUS "BUILD_SHARED: ${BUILD_SHARED}")
message(STATUS "BUILD_STATIC: ${BUILD_STATIC}")
message(STATUS "POST_BUILD_COPY_EXT_LIBS: ${POST_BUILD_COPY_EXT_LIBS}")
message(STATUS "ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")
message(STATUS "ENABLE_PIN2DMD: ${ENABLE_PIN2DMD}")

if(PLATFORM STREQUAL "macos")
   if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g")
   endif()
elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator")
   set(CMAKE_SYSTEM_NAME iOS)
   if (PLATFORM STREQUAL "ios-simulator")
      set(CMAKE_OSX_SYSROOT iphonesimulator)
   endif()
   set(CMAKE_OSX_DEPLOYMENT_TARGET 17.0)
elseif(PLATFORM STREQUAL "tvos")
   set(CMAKE_SYSTEM_NAME tvOS)
   set(CMAKE_OSX_DEPLOYMENT_TARGET 17.0)
elseif(PLATFORM STREQUAL "android")
   set(CMAKE_SYSTEM_NAME Android)
   set(CMAKE_SYSTEM_VERSION 30)
   set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
endif()

file(READ include/DMDUtil/DMDUtil.h version)
string(REGEX MATCH "DMDUTIL_VERSION_MAJOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "DMDUTIL_VERSION_MINOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "DMDUTIL_VERSION_PATCH[ ]+([0-9]+)" _tmp ${version})
set(VERSION_PATCH "${CMAKE_MATCH_1}")

project(dmdutil VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
   DESCRIPTION "Cross-platform DMD utilities library")

if(PLATFORM STREQUAL "win")
   if(ARCH STREQUAL "x86")
      add_compile_definitions(WIN32)
   endif()
elseif(PLATFORM STREQUAL "macos")
   if (ARCH STREQUAL "arm64")
      set(CMAKE_OSX_ARCHITECTURES arm64)
   elseif(ARCH STREQUAL "x64")
      set(CMAKE_OSX_ARCHITECTURES x86_64)
   endif()
   set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
   set(CMAKE_INSTALL_RPATH "@executable_path")
elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator" OR PLATFORM STREQUAL "tvos")
   set(CMAKE_OSX_ARCHITECTURES arm64)
elseif(PLATFORM STREQUAL "linux" OR PLATFORM STREQUAL "android")
   set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
   set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   if(ENABLE_SANITIZERS AND (PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux"))
      add_compile_options(-fsanitize=address)
      add_link_options(-fsanitize=address)

      add_compile_options(-fsanitize=undefined)
      add_link_options(-fsanitize=undefined)

      add_compile_options(-fno-omit-frame-pointer)
   endif()
endif()

set(DMDUTIL_SOURCES
   src/Config.cpp
   src/DMD.cpp
   src/LevelDMD.cpp
   src/RGB24DMD.cpp
   src/ConsoleDMD.cpp
   src/Logger.cpp
   src/AlphaNumeric.cpp
   src/DMDServer.cpp
)

set(DMDUTIL_COMPILE_DEFINITIONS "")
set(DMDUTIL_PIN2DMD_LIBS "")
set(DMDUTIL_PIN2DMD_INCLUDE_DIRS "")
set(DMDUTIL_PIN2DMD_LIBRARY_DIRS "")
set(DMDUTIL_PIN2DMD_COMPILE_OPTIONS "")

if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
   list(APPEND DMDUTIL_SOURCES
      src/PixelcadeDMD.cpp
   )
endif()

set(DMDUTIL_PIN2DMD_ENABLED FALSE)
if(ENABLE_PIN2DMD AND (PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux"))
   if(PLATFORM STREQUAL "win")
      find_path(LIBUSB_INCLUDE_DIR libusb.h)
      find_library(LIBUSB_LIBRARY NAMES libusb-1.0 usb-1.0)
      if(LIBUSB_INCLUDE_DIR)
         list(APPEND DMDUTIL_PIN2DMD_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIR})
      endif()
      if(LIBUSB_LIBRARY)
         list(APPEND DMDUTIL_PIN2DMD_LIBS ${LIBUSB_LIBRARY})
      endif()
   else()
      find_package(PkgConfig)
      if(PkgConfig_FOUND)
         pkg_check_modules(LIBUSB libusb-1.0)
      endif()
      if(LIBUSB_FOUND)
         list(APPEND DMDUTIL_PIN2DMD_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIRS})
         list(APPEND DMDUTIL_PIN2DMD_LIBRARY_DIRS ${LIBUSB_LIBRARY_DIRS})
         list(APPEND DMDUTIL_PIN2DMD_LIBS ${LIBUSB_LIBRARIES})
         list(APPEND DMDUTIL_PIN2DMD_COMPILE_OPTIONS ${LIBUSB_CFLAGS_OTHER})
      else()
         if(PLATFORM STREQUAL "macos" AND ARCH STREQUAL "x64")
            set(LIBUSB_SEARCH_PREFIXES /usr/local)
         elseif(PLATFORM STREQUAL "macos")
            set(LIBUSB_SEARCH_PREFIXES /opt/homebrew)
         else()
            set(LIBUSB_SEARCH_PREFIXES /opt/homebrew /usr/local)
         endif()

         find_path(LIBUSB_INCLUDE_DIR libusb-1.0/libusb.h
            PATHS ${LIBUSB_SEARCH_PREFIXES}
            PATH_SUFFIXES include)
         if(LIBUSB_INCLUDE_DIR)
            list(APPEND DMDUTIL_PIN2DMD_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIR})
         endif()

         find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0
            PATHS ${LIBUSB_SEARCH_PREFIXES}
            PATH_SUFFIXES lib)
         if(LIBUSB_LIBRARY)
            list(APPEND DMDUTIL_PIN2DMD_LIBS ${LIBUSB_LIBRARY})
         endif()
      endif()
   endif()

   if(DMDUTIL_PIN2DMD_INCLUDE_DIRS AND DMDUTIL_PIN2DMD_LIBS)
      list(APPEND DMDUTIL_SOURCES
         src/pin2dmd.cpp
      )
      list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_PIN2DMD)
      set(DMDUTIL_PIN2DMD_ENABLED TRUE)
   else()
      message(WARNING "PIN2DMD disabled: libusb headers/libraries not found.")
   endif()
endif()

set(DMDUTIL_INCLUDE_DIRS
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   third-party/include
)

if(BUILD_SHARED)
   add_library(dmdutil_shared SHARED ${DMDUTIL_SOURCES})

   target_include_directories(dmdutil_shared PUBLIC ${DMDUTIL_INCLUDE_DIRS})
   if(DMDUTIL_PIN2DMD_INCLUDE_DIRS)
      target_include_directories(dmdutil_shared PUBLIC ${DMDUTIL_PIN2DMD_INCLUDE_DIRS})
   endif()
   if(DMDUTIL_COMPILE_DEFINITIONS)
      target_compile_definitions(dmdutil_shared PUBLIC ${DMDUTIL_COMPILE_DEFINITIONS})
   endif()
   if(DMDUTIL_PIN2DMD_COMPILE_OPTIONS)
      target_compile_options(dmdutil_shared PUBLIC ${DMDUTIL_PIN2DMD_COMPILE_OPTIONS})
   endif()

   if(PLATFORM STREQUAL "win")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      if(ARCH STREQUAL "x64")
         target_link_libraries(dmdutil_shared PUBLIC cargs64 zedmd64 serum64 vni64 libserialport64 sockpp64 pupdmd64 ws2_32)
      else()
        target_link_libraries(dmdutil_shared PUBLIC cargs zedmd serum vni libserialport sockpp pupdmd ws2_32)
      endif()
   elseif(PLATFORM STREQUAL "macos")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC cargs zedmd serum vni serialport sockpp pupdmd)
   elseif(PLATFORM STREQUAL "linux")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC cargs zedmd serum vni serialport sockpp pupdmd)
   elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator" OR PLATFORM STREQUAL "tvos")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd serum vni sockpp pupdmd)
   elseif(PLATFORM STREQUAL "android")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd serum vni sockpp pupdmd)
   endif()

   if(DMDUTIL_PIN2DMD_LIBS)
      target_link_libraries(dmdutil_shared PUBLIC ${DMDUTIL_PIN2DMD_LIBS})
   endif()
   if(DMDUTIL_PIN2DMD_LIBRARY_DIRS)
      target_link_directories(dmdutil_shared PUBLIC ${DMDUTIL_PIN2DMD_LIBRARY_DIRS})
   endif()

   if(PLATFORM STREQUAL "win" AND ARCH STREQUAL "x64")
      set(DMDUTIL_OUTPUT_NAME "dmdutil64")
   else()
      set(DMDUTIL_OUTPUT_NAME "dmdutil")
   endif()

   set_target_properties(dmdutil_shared PROPERTIES
      OUTPUT_NAME ${DMDUTIL_OUTPUT_NAME}
      VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
   )

   install(TARGETS dmdutil_shared
      LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   )
   install(DIRECTORY include/DMDUtil DESTINATION ${CMAKE_INSTALL_PREFIX}/include)

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      add_executable(dmdserver
         src/server.cpp
         src/Logger.cpp
      )
      target_link_libraries(dmdserver PUBLIC dmdutil_shared)

      add_executable(dmdserver_test
         src/testDmdServer.cpp
      )
      target_link_libraries(dmdserver_test PUBLIC dmdutil_shared)

      add_executable(dmdutil_test
         src/test.cpp
      )
      target_link_libraries(dmdutil_test PUBLIC dmdutil_shared)

      add_executable(dmdutil-generate-scenes
         src/generateScenesDump.cpp
      )
      target_link_libraries(dmdutil-generate-scenes PUBLIC dmdutil_shared)

      add_executable(dmdutil-play-dump
         src/playDump.cpp
      )
      target_link_libraries(dmdutil-play-dump PUBLIC dmdutil_shared)

      if(POST_BUILD_COPY_EXT_LIBS)
         add_dependencies(dmdserver copy_ext_libs)
         add_dependencies(dmdserver_test copy_ext_libs)
         add_dependencies(dmdutil_test copy_ext_libs)
         add_dependencies(dmdutil-generate-scenes copy_ext_libs)
         add_dependencies(dmdutil-play-dump copy_ext_libs)
      endif()
   endif()
endif()

if(BUILD_STATIC)
   add_library(dmdutil_static STATIC ${DMDUTIL_SOURCES})

   target_include_directories(dmdutil_static PUBLIC ${DMDUTIL_INCLUDE_DIRS})
   if(DMDUTIL_PIN2DMD_INCLUDE_DIRS)
      target_include_directories(dmdutil_static PUBLIC ${DMDUTIL_PIN2DMD_INCLUDE_DIRS})
   endif()
   if(DMDUTIL_COMPILE_DEFINITIONS)
      target_compile_definitions(dmdutil_static PUBLIC ${DMDUTIL_COMPILE_DEFINITIONS})
   endif()
   if(DMDUTIL_PIN2DMD_COMPILE_OPTIONS)
      target_compile_options(dmdutil_static PUBLIC ${DMDUTIL_PIN2DMD_COMPILE_OPTIONS})
   endif()

   if(DMDUTIL_PIN2DMD_LIBS)
      target_link_libraries(dmdutil_static PUBLIC ${DMDUTIL_PIN2DMD_LIBS})
   endif()
   if(DMDUTIL_PIN2DMD_LIBRARY_DIRS)
      target_link_directories(dmdutil_static PUBLIC ${DMDUTIL_PIN2DMD_LIBRARY_DIRS})
   endif()

   if(PLATFORM STREQUAL "win")
      set_target_properties(dmdutil_static PROPERTIES
         OUTPUT_NAME "dmdutil_static"
      )
   else()
      set_target_properties(dmdutil_static PROPERTIES
         OUTPUT_NAME "dmdutil"
      )
   endif()

   install(TARGETS dmdutil_static
      LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   )
   install(DIRECTORY include/DMDUtil DESTINATION ${CMAKE_INSTALL_PREFIX}/include)

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      add_executable(dmdutil_test_s
         src/test.cpp
      )

      if(PLATFORM STREQUAL "win")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/build-libs/${PLATFORM}/${ARCH}
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         if(ARCH STREQUAL "x64")
            target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs64 zedmd64 serum64 vni64 libserialport64 sockpp64 pupdmd64 ws2_32)
          else()
            target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs zedmd serum vni libserialport sockpp pupdmd ws2_32)
          endif()
      elseif(PLATFORM STREQUAL "macos")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs zedmd serum vni serialport sockpp pupdmd)
      elseif(PLATFORM STREQUAL "linux")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs zedmd serum vni serialport sockpp pupdmd)
      endif()

      if(POST_BUILD_COPY_EXT_LIBS)
         add_dependencies(dmdutil_test_s copy_ext_libs)
      endif()
   endif()
endif()

if(POST_BUILD_COPY_EXT_LIBS AND (PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux"))
   add_custom_target(copy_ext_libs ALL)

   if(PLATFORM STREQUAL "win")
      if(ARCH STREQUAL "x64")
         add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/cargs64.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/cargs64.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd64.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd64.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum64.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum64.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport64.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport64.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/sockpp64.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/sockpp64.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/pupdmd64.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/pupdmd64.dll" "${CMAKE_BINARY_DIR}"
         )
      else()
         add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/cargs.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/cargs.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/sockpp.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/sockpp.dll" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/pupdmd.lib" "${CMAKE_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/pupdmd.dll" "${CMAKE_BINARY_DIR}"
         )
      endif()
   elseif(PLATFORM STREQUAL "macos")
      add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND cp -a "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.dylib" "${CMAKE_BINARY_DIR}/"
      )
   elseif(PLATFORM STREQUAL "linux")
      add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND cp -a "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.so" "${CMAKE_BINARY_DIR}/"
         COMMAND cp -a "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.so.*" "${CMAKE_BINARY_DIR}/"
      )
   endif()
endif()
