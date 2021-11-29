#

add_gdal_driver(
  TARGET
  gdal_VRC
  # SOURCES png_crc.c png_crc.h VRC.cpp VRC.h VRCthirtysix.cpp VRCutils.cpp
  # VRCutils.h VRHV.cpp
  SOURCES
  png_crc.c
  png_crc.h
  VRC.cpp
  VRC.h
  VRCutils.cpp
  PLUGIN_CAPABLE_IF
  "NOT GDAL_USE_LIBPNG_INTERNAL;NOT GDAL_USE_LIBZ_INTERNAL")

gdal_standard_includes(gdal_VRC)

target_compile_definitions(gdal_VRC PRIVATE -DHAVE_LIBZ -DZIP_SUPPORT)

if(GDAL_USE_LIBPNG_INTERNAL)
  target_include_directories(gdal_VRC
                             PRIVATE $<TARGET_PROPERTY:libpng,SOURCE_DIR>)
else()
  gdal_target_link_libraries(TARGET gdal_VRC LIBRARIES PNG::PNG)
endif()

if(GDAL_USE_LIBZ_INTERNAL)
  target_include_directories(gdal_VRC
                             PRIVATE $<TARGET_PROPERTY:libz,SOURCE_DIR>)
else()
  target_compile_definitions(gdal_VRC PRIVATE -DHAVE_ZLIB_H -DHAVE_ZLIB)
  gdal_target_link_libraries(TARGET gdal_VRC LIBRARIES ZLIB::ZLIB)
  if(MSVC)
    target_compile_definitions(gdal_VRC PRIVATE -DZLIB_DLL)
  endif()
endif()
