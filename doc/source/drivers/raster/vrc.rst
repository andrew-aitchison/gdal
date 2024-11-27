.. _raster.vrc:

================================================================================
VRC -- ViewRanger Chart Image File Format
================================================================================

.. versionadded:: not before 3.10

.. shortname:: ViewRangerVRC

.. built_in_by_default:: ??? Uses the same png and zlib libraries as the PNG driver.
 
The VRC driver currently supports reading VRC files with magic
(first four bytes) 7E-1F-2E-00 but not 36-63-CE-01.
Files with geolocation 17 (bytes 6-7: 11-00) are mis-located.
These include some US/BE/ES/GR and many Discovery maps.

The following metadata items may be reported:
"VRC ViewRanger MapID" - should this change to IMAGE_ID ?
String0 ... StringN - could this be moved to COMMENTS, or is the structure too useful ?
TIFFTAG_COPYRIGHT
TIFFTAG_IMAGEDESCRIPTION
"VRC ViewRanger Device ID" - related to Digital Right Management (DRM), but not encryption
VRCchecksum
GDAL_DMD_LONGNAME
GDAL_DMD_HELPTOPIC
GDAL_DMD_EXTENSION
GDAL_DMD_CREATIONDATATYPES
LICENSE_POLICY:  NONRECIPROCAL
GDALMD_AREA_OR_POINT: GDALMD_AOP_AREA

# AUTHOR_NAME and COMMENTS.

The extended area is used to determine if the fourth band is an alpha
channel or not.

Driver capabilities
-------------------

.. supports_georeferencing::

   ??

.. supports_virtualio::

   ??

Georeferencing
--------------

Overviews
---------



Links
-----

NOTE: Implemented as VRC.cpp

PNG support is implemented based on the libpng reference library. More
information is available at http://www.libpng.org/pub/png.
