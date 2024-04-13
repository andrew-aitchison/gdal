.. _raster.vrc:

================================================================================
VRC -- ViewRanger Chart Image File Format
================================================================================

.. versionadded:: not before 3.9

.. shortname:: VRC

.. built_in_by_default::

The VRC driver currently supports reading VRC files with magic
(first four bytes) 7e 1f 2e 00 but not 36 63 ce 01.
Files with geolocation (bytes 6 and 7) 17 00 are mis-located.
These include some US/BE/ES/GR and many Discovery maps.

The following metadata items may be reported:
"VRC ViewRanger MapID" - should this change to IMAGE_ID ?
String0 ... StringN
TIFFTAG_COPYRIGHT
TIFFTAG_IMAGEDESCRIPTION
"VRC ViewRanger Device ID" - related to Digital Right Management (DRM), but not encyption
VRCchecksum
GDAL_DMD_LONGNAME
GDAL_DMD_HELPTOPIC
GDAL_DMD_EXTENSION
GDAL_DMD_CREATIONDATATYPES
LICENSE_POLICY:  NONRECIPROCAL
GDALMD_AREA_OR_POINT: GDALMD_AOP_AREA

AUTHOR_NAME and
COMMENTS.

The extended area is used to determine if the fourth band is an alpha
channel or not.

Driver capabilities
-------------------

.. supports_virtualio::



Links
-----

