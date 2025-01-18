.. _raster.vrhv:

================================================================================
VRHV -- ViewRanger Height and "Vendor" Image File Formats
================================================================================

.. versionadded:: not before 3.10

.. shortname:: ViewRangerVRHV

.. built_in_by_default:: ???
 
The VRHV driver currently supports reading .VRH and .VRV files.
Some .VRH have first four bytes (magic) hex
4F-80-C6-FA, but others do not.
Height files with geolocation 17 (bytes 8-9: 11-00) are mis-located.
These include some US/BE/ES/GR and many Discovery maps.

The following metadata items may be reported:
"VRC ViewRanger MapID" - should this change to IMAGE_ID ?
LICENSE_POLICY:  NONRECIPROCAL
GDALMD_AREA_OR_POINT: GDALMD_AOP_AREA  - please check

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

NOTE: Implemented as :source_file:`VRVH.cpp`.

