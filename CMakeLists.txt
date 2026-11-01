cmake_minimum_required(VERSION 3.25)

set(PLATFORM "win" CACHE STRING "Platform")
set(ARCH "x64" CACHE STRING "Arch")

option(BUILD_SHARED "Option to build shared library" ON)
option(BUILD_STATIC "Option to build static library" ON)
option(POST_BUILD_COPY_EXT_LIBS "Option to copy external libraries to build directory" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan for Debug builds" OFF)
option(ENABLE_SERUM "Enable Serum colorization support" ON)
option(ENABLE_VNI "Enable VNI colorization support" ON)
option(ENABLE_PUP "Enable PUP DMD trigger support" ON)
option(ENABLE_PIXELCADE "Enable Pixelcade support" ON)
option(ENABLE_PIN2DMD "Enable PIN2DMD support" ON)
option(ENABLE_PINDMD3 "Enable PinDMD3 support" ON)
option(ENABLE_DMDEXTSTREAM "Enable streaming to a dmd-extensions server" ON)

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "ARCH: ${ARCH}")

message(STATUS "BUILD_SHARED: ${BUILD_SHARED}")
message(STATUS "BUILD_STATIC: ${BUILD_STATIC}")
message(STATUS "POST_BUILD_COPY_EXT_LIBS: ${POST_BUILD_COPY_EXT_LIBS}")
message(STATUS "ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")
message(STATUS "ENABLE_SERUM: ${ENABLE_SERUM}")
message(STATUS "ENABLE_VNI: ${ENABLE_VNI}")
message(STATUS "ENABLE_PUP: ${ENABLE_PUP}")
message(STATUS "ENABLE_PIXELCADE: ${ENABLE_PIXELCADE}")
message(STATUS "ENABLE_PIN2DMD: ${ENABLE_PIN2DMD}")
message(STATUS "ENABLE_PINDMD3: ${ENABLE_PINDMD3}")
message(STATUS "ENABLE_DMDEXTSTREAM: ${ENABLE_DMDEXTSTREAM}")

if(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator")
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
string(REGEX MATCH "#[ \t]*define[ \t]+DMDUTIL_VERSION_MAJOR[ \t]+([0-9]+)" _tmp "${version}")
if("${CMAKE_MATCH_1}" STREQUAL "")
   message(FATAL_ERROR "Failed to parse DMDUTIL_VERSION_MAJOR from include/DMDUtil/DMDUtil.h")
endif()
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#[ \t]*define[ \t]+DMDUTIL_VERSION_MINOR[ \t]+([0-9]+)" _tmp "${version}")
if("${CMAKE_MATCH_1}" STREQUAL "")
   message(FATAL_ERROR "Failed to parse DMDUTIL_VERSION_MINOR from include/DMDUtil/DMDUtil.h")
endif()
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#[ \t]*define[ \t]+DMDUTIL_VERSION_PATCH[ \t]+([0-9]+)" _tmp "${version}")
if("${CMAKE_MATCH_1}" STREQUAL "")
   message(FATAL_ERROR "Failed to parse DMDUTIL_VERSION_PATCH from include/DMDUtil/DMDUtil.h")
endif()
set(VERSION_PATCH "${CMAKE_MATCH_1}")

project(dmdutil VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
   DESCRIPTION "Cross-platform DMD utilities library")

if(PLATFORM STREQUAL "win")
   if(ARCH STREQUAL "x86")
      add_compile_definitions(WIN32)
   endif()
elseif(PLATFORM STREQUAL "win-mingw")
   add_compile_options(-Wa,-mbig-obj)
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
      set(SANITIZER_FLAGS -fsanitize=address,undefined)
      add_compile_options(${SANITIZER_FLAGS} -fno-omit-frame-pointer -g)
      add_link_options(${SANITIZER_FLAGS})
   endif()
endif()

set(MINIZ_SOURCE "third-party/include/miniz/miniz.c")
set(MINIZ_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/include")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${MINIZ_SOURCE}")
   message(FATAL_ERROR "Could not find required local miniz source: ${MINIZ_SOURCE}")
endif()

set(DMDUTIL_SOURCES
   src/Config.cpp
   src/DMD.cpp
   src/LevelDMD.cpp
   src/RGB24DMD.cpp
   src/OutputFilters.cpp
   src/ConsoleDMD.cpp
   src/Logger.cpp
   src/AlphaNumeric.cpp
   src/DMDServer.cpp
   ${MINIZ_SOURCE}
)

list(APPEND DMDUTIL_SOURCES src/devices/DeviceThreads.cpp)

set(DMDUTIL_COMPILE_DEFINITIONS "")
if(ENABLE_SERUM)
   list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_SERUM)
   list(APPEND DMDUTIL_SOURCES src/colorization/Serum.cpp)
endif()
if(ENABLE_VNI)
   list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_VNI)
   list(APPEND DMDUTIL_SOURCES src/colorization/Vni.cpp)
endif()
if(ENABLE_PUP)
   list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_PUP)
   list(APPEND DMDUTIL_SOURCES src/colorization/Pup.cpp)
endif()
if(ENABLE_DMDEXTSTREAM)
   list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_DMDEXTSTREAM)
   list(APPEND DMDUTIL_SOURCES src/devices/DMDExtStream.cpp)
endif()

if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
   if(ENABLE_PIXELCADE)
      list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_PIXELCADE)
      list(APPEND DMDUTIL_SOURCES src/devices/PixelcadeDMD.cpp)
   endif()
   if(ENABLE_PIN2DMD)
      list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_PIN2DMD)
      list(APPEND DMDUTIL_SOURCES src/devices/PIN2DMD.cpp)
   endif()
   if(ENABLE_PINDMD3)
      list(APPEND DMDUTIL_COMPILE_DEFINITIONS DMDUTIL_ENABLE_PINDMD3)
      list(APPEND DMDUTIL_SOURCES src/devices/PinDMD3.cpp)
   endif()
endif()

set(DMDUTIL_INCLUDE_DIRS
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/src
   third-party/include
   ${MINIZ_INCLUDE_DIR}
)

if(BUILD_SHARED)
   add_library(dmdutil_shared SHARED ${DMDUTIL_SOURCES})

   target_include_directories(dmdutil_shared PUBLIC ${DMDUTIL_INCLUDE_DIRS})
   if(DMDUTIL_COMPILE_DEFINITIONS)
      target_compile_definitions(dmdutil_shared PUBLIC ${DMDUTIL_COMPILE_DEFINITIONS})
   endif()

   if(PLATFORM STREQUAL "win")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      if(ARCH STREQUAL "x64")
         target_link_libraries(dmdutil_shared PUBLIC cargs64 zedmd64 libserialport64 sockpp64 ws2_32)
         if(ENABLE_SERUM)
            target_link_libraries(dmdutil_shared PUBLIC serum64)
         endif()
         if(ENABLE_VNI)
            target_link_libraries(dmdutil_shared PUBLIC vni64)
         endif()
         if(ENABLE_PUP)
            target_link_libraries(dmdutil_shared PUBLIC pupdmd64)
         endif()
         if(ENABLE_PIN2DMD)
            target_link_libraries(dmdutil_shared PUBLIC libusb64-1.0)
         endif()
      else()
        target_link_libraries(dmdutil_shared PUBLIC cargs zedmd libserialport sockpp ws2_32)
        if(ENABLE_SERUM)
           target_link_libraries(dmdutil_shared PUBLIC serum)
        endif()
        if(ENABLE_VNI)
           target_link_libraries(dmdutil_shared PUBLIC vni)
        endif()
        if(ENABLE_PUP)
           target_link_libraries(dmdutil_shared PUBLIC pupdmd)
        endif()
        if(ENABLE_PIN2DMD)
           target_link_libraries(dmdutil_shared PUBLIC libusb-1.0)
        endif()
      endif()
   elseif(PLATFORM STREQUAL "win-mingw")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC cargs64 zedmd64 serialport64 sockpp64 ws2_32)
      if(ENABLE_SERUM)
         target_link_libraries(dmdutil_shared PUBLIC serum64)
      endif()
      if(ENABLE_VNI)
         target_link_libraries(dmdutil_shared PUBLIC vni64)
      endif()
      if(ENABLE_PUP)
         target_link_libraries(dmdutil_shared PUBLIC pupdmd64)
      endif()
      if(ENABLE_PIN2DMD)
         target_link_libraries(dmdutil_shared PUBLIC usb64-1.0)
      endif()
   elseif(PLATFORM STREQUAL "macos")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC cargs zedmd serialport sockpp)
      if(ENABLE_SERUM)
         target_link_libraries(dmdutil_shared PUBLIC serum)
      endif()
      if(ENABLE_VNI)
         target_link_libraries(dmdutil_shared PUBLIC vni)
      endif()
      if(ENABLE_PUP)
         target_link_libraries(dmdutil_shared PUBLIC pupdmd)
      endif()
      if(ENABLE_PIN2DMD)
         target_link_libraries(dmdutil_shared PUBLIC usb-1.0)
      endif()
   elseif(PLATFORM STREQUAL "linux")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC cargs zedmd serialport sockpp)
      if(ENABLE_SERUM)
         target_link_libraries(dmdutil_shared PUBLIC serum)
      endif()
      if(ENABLE_VNI)
         target_link_libraries(dmdutil_shared PUBLIC vni)
      endif()
      if(ENABLE_PUP)
         target_link_libraries(dmdutil_shared PUBLIC pupdmd)
      endif()
      if(ENABLE_PIN2DMD)
         target_link_libraries(dmdutil_shared PUBLIC usb-1.0)
      endif()
   elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator" OR PLATFORM STREQUAL "tvos")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd sockpp)
      if(ENABLE_SERUM)
         target_link_libraries(dmdutil_shared PUBLIC serum)
      endif()
      if(ENABLE_VNI)
         target_link_libraries(dmdutil_shared PUBLIC vni)
      endif()
      if(ENABLE_PUP)
         target_link_libraries(dmdutil_shared PUBLIC pupdmd)
      endif()
   elseif(PLATFORM STREQUAL "android")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd sockpp)
      if(ENABLE_SERUM)
         target_link_libraries(dmdutil_shared PUBLIC serum)
      endif()
      if(ENABLE_VNI)
         target_link_libraries(dmdutil_shared PUBLIC vni)
      endif()
      if(ENABLE_PUP)
         target_link_libraries(dmdutil_shared PUBLIC pupdmd)
      endif()
   endif()

   if((PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw") AND ARCH STREQUAL "x64")
      set(DMDUTIL_OUTPUT_NAME "dmdutil64")
   else()
      set(DMDUTIL_OUTPUT_NAME "dmdutil")
   endif()

   if(PLATFORM STREQUAL "win-mingw")
      set_target_properties(dmdutil_shared PROPERTIES
         PREFIX ""
         IMPORT_PREFIX ""
         OUTPUT_NAME ${DMDUTIL_OUTPUT_NAME}
         VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
      )
   else()
      set_target_properties(dmdutil_shared PROPERTIES
         OUTPUT_NAME ${DMDUTIL_OUTPUT_NAME}
         VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
      )
   endif()

   install(TARGETS dmdutil_shared
      LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   )
   install(DIRECTORY include/DMDUtil DESTINATION ${CMAKE_INSTALL_PREFIX}/include)

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      add_executable(dmdserver
         src/tools/server.cpp
         src/Logger.cpp
      )
      target_link_libraries(dmdserver PUBLIC dmdutil_shared)

      add_executable(dmdserver_test
         src/tools/testDmdServer.cpp
      )
      target_link_libraries(dmdserver_test PUBLIC dmdutil_shared)

      add_executable(dmdutil_test
         src/tools/test.cpp
      )
      target_link_libraries(dmdutil_test PUBLIC dmdutil_shared)

      add_executable(dmdutil_unit_test
         src/tools/unitTest.cpp
      )
      target_link_libraries(dmdutil_unit_test PUBLIC dmdutil_shared)

      if(ENABLE_SERUM)
         add_executable(dmdutil-generate-scenes
            src/tools/generateScenesDump.cpp
         )
         target_link_libraries(dmdutil-generate-scenes PUBLIC dmdutil_shared)
      endif()

      if(ENABLE_SERUM)
         add_executable(dmdutil-play-dump
            src/tools/playDump.cpp
            src/tools/DumpLoader.cpp
            ${MINIZ_SOURCE}
         )
         target_link_libraries(dmdutil-play-dump PUBLIC dmdutil_shared)
      endif()

      if(ENABLE_SERUM)
         add_executable(dmdutil-convert-serum
            src/tools/convertSerum.cpp
         )
         target_link_libraries(dmdutil-convert-serum PUBLIC dmdutil_shared)
      endif()

      add_executable(dmdutil-compare-dumps
         src/tools/compareJsonDumps.cpp
      )
      target_link_libraries(dmdutil-compare-dumps PUBLIC dmdutil_shared)

      if(POST_BUILD_COPY_EXT_LIBS)
         add_dependencies(dmdserver copy_ext_libs)
         add_dependencies(dmdserver_test copy_ext_libs)
         add_dependencies(dmdutil_test copy_ext_libs)
         add_dependencies(dmdutil_unit_test copy_ext_libs)
         if(ENABLE_SERUM)
            add_dependencies(dmdutil-generate-scenes copy_ext_libs)
         endif()
         if(ENABLE_SERUM)
            add_dependencies(dmdutil-play-dump copy_ext_libs)
         endif()
         if(ENABLE_SERUM)
            add_dependencies(dmdutil-convert-serum copy_ext_libs)
         endif()
         add_dependencies(dmdutil-compare-dumps copy_ext_libs)
      endif()
   endif()
endif()

if(BUILD_STATIC)
   add_library(dmdutil_static STATIC ${DMDUTIL_SOURCES})

   target_include_directories(dmdutil_static PUBLIC ${DMDUTIL_INCLUDE_DIRS})
   if(DMDUTIL_COMPILE_DEFINITIONS)
      target_compile_definitions(dmdutil_static PUBLIC ${DMDUTIL_COMPILE_DEFINITIONS})
   endif()

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw")
      set_target_properties(dmdutil_static PROPERTIES
         PREFIX ""
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

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      add_executable(dmdutil_test_s
         src/tools/test.cpp
      )

      if(PLATFORM STREQUAL "win")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/build-libs/${PLATFORM}/${ARCH}
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         if(ARCH STREQUAL "x64")
            target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs64 zedmd64 libserialport64 sockpp64 ws2_32)
            if(ENABLE_SERUM)
               target_link_libraries(dmdutil_test_s PUBLIC serum64)
            endif()
            if(ENABLE_VNI)
               target_link_libraries(dmdutil_test_s PUBLIC vni64)
            endif()
            if(ENABLE_PUP)
               target_link_libraries(dmdutil_test_s PUBLIC pupdmd64)
            endif()
            if(ENABLE_PIN2DMD)
               target_link_libraries(dmdutil_test_s PUBLIC libusb64-1.0)
            endif()
          else()
            target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs zedmd libserialport sockpp ws2_32)
            if(ENABLE_SERUM)
               target_link_libraries(dmdutil_test_s PUBLIC serum)
            endif()
            if(ENABLE_VNI)
               target_link_libraries(dmdutil_test_s PUBLIC vni)
            endif()
            if(ENABLE_PUP)
               target_link_libraries(dmdutil_test_s PUBLIC pupdmd)
            endif()
            if(ENABLE_PIN2DMD)
               target_link_libraries(dmdutil_test_s PUBLIC libusb-1.0)
            endif()
          endif()
      elseif(PLATFORM STREQUAL "win-mingw")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/build-libs/${PLATFORM}/${ARCH}
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs64 zedmd64 serialport64 sockpp64 ws2_32)
         if(ENABLE_SERUM)
            target_link_libraries(dmdutil_test_s PUBLIC serum64)
         endif()
         if(ENABLE_VNI)
            target_link_libraries(dmdutil_test_s PUBLIC vni64)
         endif()
         if(ENABLE_PUP)
            target_link_libraries(dmdutil_test_s PUBLIC pupdmd64)
         endif()
         if(ENABLE_PIN2DMD)
            target_link_libraries(dmdutil_test_s PUBLIC usb64-1.0)
         endif()
      elseif(PLATFORM STREQUAL "macos")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs zedmd serialport sockpp)
         if(ENABLE_SERUM)
            target_link_libraries(dmdutil_test_s PUBLIC serum)
         endif()
         if(ENABLE_VNI)
            target_link_libraries(dmdutil_test_s PUBLIC vni)
         endif()
         if(ENABLE_PUP)
            target_link_libraries(dmdutil_test_s PUBLIC pupdmd)
         endif()
         if(ENABLE_PIN2DMD)
            target_link_libraries(dmdutil_test_s PUBLIC usb-1.0)
         endif()
      elseif(PLATFORM STREQUAL "linux")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static cargs zedmd serialport sockpp)
         if(ENABLE_SERUM)
            target_link_libraries(dmdutil_test_s PUBLIC serum)
         endif()
         if(ENABLE_VNI)
            target_link_libraries(dmdutil_test_s PUBLIC vni)
         endif()
         if(ENABLE_PUP)
            target_link_libraries(dmdutil_test_s PUBLIC pupdmd)
         endif()
         if(ENABLE_PIN2DMD)
            target_link_libraries(dmdutil_test_s PUBLIC usb-1.0)
         endif()
      endif()

      if(POST_BUILD_COPY_EXT_LIBS)
         add_dependencies(dmdutil_test_s copy_ext_libs)
      endif()
   endif()
endif()

if(POST_BUILD_COPY_EXT_LIBS AND (PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux"))
   add_custom_target(copy_ext_libs ALL)

   if(PLATFORM STREQUAL "win")
      add_dependencies(copy_ext_libs dmdutil_shared)
      if(ARCH STREQUAL "x64")
         add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/cargs64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/cargs64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport64-0.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/sockpp64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/sockpp64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libgcc_s_seh-1.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libstdc++-6.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libwinpthread-1.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         if(ENABLE_PIN2DMD)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libusb64-1.0.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libusb64-1.0.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
         if(ENABLE_SERUM)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
         if(ENABLE_PUP)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/pupdmd64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/pupdmd64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
         if(ENABLE_VNI)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/vni64.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/vni64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
      else()
         add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/cargs.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/cargs.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport-0.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/sockpp.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/sockpp.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libgcc_s_dw2-1.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libstdc++-6.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libwinpthread-1.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         if(ENABLE_PIN2DMD)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libusb-1.0.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libusb-1.0.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
         if(ENABLE_SERUM)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
         if(ENABLE_PUP)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/pupdmd.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/pupdmd.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
         if(ENABLE_VNI)
            add_custom_command(TARGET copy_ext_libs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/vni.lib" "$<TARGET_FILE_DIR:dmdutil_shared>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/vni.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         )
         endif()
      endif()
   elseif(PLATFORM STREQUAL "win-mingw")
      add_dependencies(copy_ext_libs dmdutil_shared)
      add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libcargs64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport64-0.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libsockpp64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libgcc_s_seh-1.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libstdc++-6.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libwinpthread-1.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
      )
      if(ENABLE_PIN2DMD)
         add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libusb64-1.0.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
      )
      endif()
      if(ENABLE_SERUM)
         add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
      )
      endif()
      if(ENABLE_PUP)
         add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/pupdmd64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
      )
      endif()
      if(ENABLE_VNI)
         add_custom_command(TARGET copy_ext_libs POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/vni64.dll" "$<TARGET_FILE_DIR:dmdutil_shared>"
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
