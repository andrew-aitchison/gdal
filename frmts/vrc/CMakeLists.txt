#
# The following may or may not help:
# /home/werdna/gdal/git/cmake/helpers/GdalDriverHelper.cmake 
# /home/werdna/gdal/git/frmts/gif/CMakeLists.txt
# 

set(CMAKE_VERBOSE_MAKEFILE ON)

message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
message(STATUS "GDAL_SOURCE_HERE: ${GDAL_SOURCE_HERE}")
message(STATUS "GDAL_INSTALLED_HERE: ${GDAL_INSTALLED_HERE}")
message(STATUS "PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}")
message(STATUS "PKG_CONFIG_LIBDIR: ${PKG_CONFIG_LIBDIR}")


if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(GDAL_CXX_WARNING_FLAGS ${GDAL_CXX_WARNING_FLAGS} -Wno-unknown-pragmas)
endif()


if(NOT DEFINED PROJECT_SOURCE_DIR)
  # Standalone plugin building
  set(STANDALONE ON)

  #cmake_minimum_required(VERSION 3.16...3.31)
  # ... a possible alternative from GDAL v3.10 onwards:
  include("${GDAL_SOURCE_HERE}/cmake/helpers/GdalCMakeMinimumRequired.cmake")
  cmake_minimum_required(VERSION ${GDAL_CMAKE_VERSION_MIN}...${GDAL_CMAKE_VERSION_MAX})
  message(STATUS "CGDAL_CMAKE_VERSION_MIN: ${GDAL_CMAKE_VERSION_MIN}")
  message(STATUS "CGDAL_CMAKE_VERSION_MAX: ${GDAL_CMAKE_VERSION_MAX}")

  project(gdal_VRC)

  # required
  find_package(PROJ REQUIRED)
  message(STATUS "Found Proj: ${PROJ_VERSION} ${PROJ_DIR}")

  find_package(GEOS REQUIRED)
  message(STATUS "Found Geos: ${GEOS_VERSION} ${GEOS_DIR}")

  message(STATUS "LIBRARY_PATH ${LIBRARY_PATH}")
  message(STATUS "LD_LIBRARY_PATH ${LD_LIBRARY_PATH}")
  # find_package(GDAL REQUIRED)
  find_package(GDAL ${GDAL_WANTED})
  message(STATUS "Found GDAL: ${GDAL_VERSION} ${GDAL_DIR}")
  message(STATUS "Found GDAL: ${GDAL_INCLUDE_DIRS} ${GDAL_LIBRARIES}")

  # set(IGNORE_GDAL_VERSION_MISMATCH on)

  include("${GDAL_SOURCE_HERE}/cmake/helpers/SetupStandalonePlugin.cmake")

  #include(CheckDependentLibrariesVRC)
  #gdal_check_package(VRC "Enable VRC driver" CAN_DISABLE)

  set(GDAL_ENABLE_PLUGINS ON)

  set(GDAL_ENABLE_DRIVER_VRHV ON)
  
  # set(GDAL_USE_VRC ON)

  enable_testing()
  add_subdirectory(test)

  standalone_driver_finalize(GDAL_ENABLE_DRIVER_VRC)

endif()


if (GDAL_USE_PNG AND GDAL_USE_ZLIB)
  set(GDAL_ENABLE_DRIVER_VRC ON)
endif()

# message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR_VERSION}")


if (GDAL_ENABLE_DRIVER_VRC)
 
  add_gdal_driver(
    TARGET gdal_VRC
    # SOURCES png_crc.c png_crc.h VRC.cpp VRC.h VRCthirty.cpp VRCutils.cpp VRCutils.h VRHV.cpp
    SOURCES
    png_crc.h png_crc.c
    VRC.h VRC.cpp
    VRCutils.cpp
    VRHV.h VRHV.cpp
    VRCthirty.cpp
    # PLUGIN_CAPABLE
    PLUGIN_CAPABLE_IF
      "NOT GDAL_USE_PNG_INTERNAL\\\;NOT GDAL_USE_ZLIB_INTERNAL"
  )

  if (UNIX)
    find_package(PNG REQUIRED)
    find_package(ZLIB REQUIRED) # or does PNG do this for us ?
  endif()
  
  gdal_standard_includes(gdal_VRC)

  target_compile_definitions(gdal_VRC
    PRIVATE -DFRMT_vrc -DFRMT_vrcThirty -DFRMT_vrhv
    -DUSE_PNG -DHAVE_ZLIB -DZIP_SUPPORT)

  gdal_target_link_libraries(gdal_VRC PRIVATE PNG::PNG ZLIB::ZLIB)
  # gdal_target_link_libraries(gdal_VRC PRIVATE ZLIB::ZLIB)

endif () #  (GDAL_ENABLE_DRIVER_VRC)
