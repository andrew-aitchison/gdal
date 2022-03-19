#

add_gdal_driver(
  TARGET gdal_VRC
  # SOURCES png_crc.c png_crc.h VRC.cpp VRC.h VRCthirtysix.cpp VRCutils.cpp VRCutils.h VRHV.cpp
  SOURCES png_crc.c
          png_crc.h
          VRC.cpp
          VRC.h
          VRCutils.cpp
          PLUGIN_CAPABLE_IF
          "NOT GDAL_USE_PNG_INTERNAL;NOT GDAL_USE_ZLIB_INTERNAL")

gdal_standard_includes(gdal_VRC)

target_compile_definitions(gdal_VRC PRIVATE -DHAVE_ZLIB -DZIP_SUPPORT)

if (GDAL_USE_PNG_INTERNAL)
  gdal_add_vendored_lib(gdal_PNG libpng)
else ()
  gdal_target_link_libraries(gdal_VRC PRIVATE PNG::PNG)
endif ()

if (GDAL_USE_ZLIB_INTERNAL)
  gdal_add_vendored_lib(gdal_PNG libz)
else ()
  gdal_target_link_libraries(gdal_VRC PRIVATE ZLIB::ZLIB)
endif ()
