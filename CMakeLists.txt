cmake_minimum_required(VERSION 3.25)

set(PLATFORM "win" CACHE STRING "Platform")
set(ARCH "x64" CACHE STRING "Arch")

option(BUILD_SHARED "Option to build shared library" ON)
option(BUILD_STATIC "Option to build static library" ON)
option(POST_BUILD_COPY_EXT_LIBS "Option to copy external libraries to build directory" ON)

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "ARCH: ${ARCH}")

message(STATUS "BUILD_SHARED: ${BUILD_SHARED}")
message(STATUS "BUILD_STATIC: ${BUILD_STATIC}")
message(STATUS "POST_BUILD_COPY_EXT_LIBS: ${POST_BUILD_COPY_EXT_LIBS}")

if(PLATFORM STREQUAL "ios")
   set(CMAKE_SYSTEM_NAME iOS)
elseif(PLATFORM STREQUAL "tvos")
   set(CMAKE_SYSTEM_NAME tvOS)
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
   set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)
   if (ARCH STREQUAL "arm64")
      set(CMAKE_OSX_ARCHITECTURES arm64)
   elseif(ARCH STREQUAL "x64")
      set(CMAKE_OSX_ARCHITECTURES x86_64)
   endif()
   set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
   set(CMAKE_INSTALL_RPATH "@executable_path")
elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "tvos")
   set(CMAKE_OSX_DEPLOYMENT_TARGET 16.0)
   set(CMAKE_OSX_ARCHITECTURES arm64)
elseif(PLATFORM STREQUAL "linux" OR PLATFORM STREQUAL "android")
   set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
   set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

set(DMDUTIL_SOURCES
   src/Config.cpp
   src/DMD.cpp
   src/LevelDMD.cpp
   src/RGB24DMD.cpp
   src/ConsoleDMD.cpp
   src/Logger.cpp
   src/AlphaNumeric.cpp
   src/FrameUtil.cpp
   src/Serum.cpp
)

if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
   list(APPEND DMDUTIL_SOURCES
      src/PixelcadeDMD.cpp
   )
endif()

set(DMDUTIL_INCLUDE_DIRS
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   third-party/include
)

if(BUILD_SHARED)
   add_library(dmdutil_shared SHARED ${DMDUTIL_SOURCES})
    
   target_include_directories(dmdutil_shared PUBLIC ${DMDUTIL_INCLUDE_DIRS})
    
   if(PLATFORM STREQUAL "win")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      if(ARCH STREQUAL "x64")
         target_link_libraries(dmdutil_shared PUBLIC zedmd64 serum64 libserialport64 ws2_32)
      else()
        target_link_libraries(dmdutil_shared PUBLIC zedmd serum libserialport ws2_32)
      endif()
   elseif(PLATFORM STREQUAL "macos")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd.0.6.0 serum.1.6.2 serialport)
   elseif(PLATFORM STREQUAL "linux")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC -l:libzedmd.so.0.6.0 -l:libserum.so.1.6.2 -l:libserialport.so.0)
   elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "tvos")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd serum)
   elseif(PLATFORM STREQUAL "android")
      target_link_directories(dmdutil_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(dmdutil_shared PUBLIC zedmd serum)
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
      add_executable(dmdutil_test
         src/test.cpp
      )

      target_link_libraries(dmdutil_test PUBLIC dmdutil_shared)

      if(POST_BUILD_COPY_EXT_LIBS)
         if(PLATFORM STREQUAL "win")
            if(ARCH STREQUAL "x64")
               add_custom_command(TARGET dmdutil_test POST_BUILD
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd64.lib" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd64.dll" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum64.lib" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum64.dll" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport64.lib" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport64.dll" "$<TARGET_FILE_DIR:dmdutil_test>"
               )
            else()
               add_custom_command(TARGET dmdutil_test POST_BUILD
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd.lib" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd.dll" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum.lib" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum.dll" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport.lib" "$<TARGET_FILE_DIR:dmdutil_test>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.dll" "$<TARGET_FILE_DIR:dmdutil_test>"
               )
            endif()
         elseif(PLATFORM STREQUAL "macos")
            add_custom_command(TARGET dmdutil_test POST_BUILD
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libzedmd.0.6.0.dylib" "$<TARGET_FILE_DIR:dmdutil_test>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserum.1.6.2.dylib" "$<TARGET_FILE_DIR:dmdutil_test>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.dylib" "$<TARGET_FILE_DIR:dmdutil_test>"
            )
         elseif(PLATFORM STREQUAL "linux")
            add_custom_command(TARGET dmdutil_test POST_BUILD
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libzedmd.so.0.6.0" "$<TARGET_FILE_DIR:dmdutil_test>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserum.so.1.6.2" "$<TARGET_FILE_DIR:dmdutil_test>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.so.0" "$<TARGET_FILE_DIR:dmdutil_test>"
            )
         endif()
      endif()
   endif()
endif()
 
if(BUILD_STATIC)
   add_library(dmdutil_static STATIC ${DMDUTIL_SOURCES})

   target_include_directories(dmdutil_static PUBLIC ${DMDUTIL_INCLUDE_DIRS})

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
            target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static zedmd64 serum64 libserialport64 ws2_32)
         else()
            target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static zedmd serum libserialport ws2_32)
         endif()
      elseif(PLATFORM STREQUAL "macos")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static zedmd.0.6.0 serum.1.6.2 serialport)
      elseif(PLATFORM STREQUAL "linux")
         target_link_directories(dmdutil_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(dmdutil_test_s PUBLIC dmdutil_static -l:libzedmd.so.0.6.0 -l:libserum.so.1.6.2 -l:libserialport.so.0)
      endif()

      if(POST_BUILD_COPY_EXT_LIBS)
         if(PLATFORM STREQUAL "win")
            if(ARCH STREQUAL "x64")
               add_custom_command(TARGET dmdutil_test_s POST_BUILD
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd64.lib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd64.dll" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum64.lib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum64.dll" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport64.lib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport64.dll" "$<TARGET_FILE_DIR:dmdutil_test_s>"
               )
            else()
               add_custom_command(TARGET dmdutil_test_s POST_BUILD
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/zedmd.lib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/zedmd.dll" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/serum.lib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/serum.dll" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/libserialport.lib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
                  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.dll" "$<TARGET_FILE_DIR:dmdutil_test_s>"
               )
            endif()
         elseif(PLATFORM STREQUAL "macos")
            add_custom_command(TARGET dmdutil_test_s POST_BUILD
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libzedmd.0.6.0.dylib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserum.1.6.2.dylib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.dylib" "$<TARGET_FILE_DIR:dmdutil_test_s>"
            )
         else()
            add_custom_command(TARGET dmdutil_test_s POST_BUILD
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libzedmd.so.0.6.0" "$<TARGET_FILE_DIR:dmdutil_test_s>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserum.so.1.6.2" "$<TARGET_FILE_DIR:dmdutil_test_s>"
               COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/libserialport.so.0" "$<TARGET_FILE_DIR:dmdutil_test_s>"
            )
         endif()
      endif()
   endif()
endif()
