#!/usr/bin/env pytest
###############################################################################
#
#
# Project:  GDAL/OGR Test Suite
# Purpose:  VRC Testing.
# Author:   Andrew Aitchison <vrc at aitchison.me.uk>
#
###############################################################################
# Copyright (c) 2022, Andrew Aitchison <vrc at aitchison.me.uk>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
###############################################################################

"""This module tests the ViewRanger VRHV plugin."""

import os

from osgeo import gdal

# print('os.uname().sysname =',os.uname())

# import gdaltest


print("gdal imported")

import pytest

gdal.UseExceptions()

print("Start of environ\n")
for env in sorted((os.environ)):
    print(env, "=", os.environ.get(env))
print("End of environ\n")

print("GDAL_DRIVER_PATH =", os.environ.get("GDAL_DRIVER_PATH"))
print("PATH =", os.environ.get("PATH"))
print("PYTHONPATH =", os.environ.get("PYTHONPATH"))

# pip install msi-utils
# import msi-utils

###############################################################################
# Test reading a .VRH height file


def test_vrhv_online_1():
    """Function to test the ViewRanger VRH plugin."""
    print("VRV files not currently tested")

    testfile = "/home/maps/viewranger/altitude/IrelandAltitude/B.VRH"

    testsum = 21082

    try:
        os.stat(testfile)
    except OSError:
        print(testfile, "not found\n")
        assert False
        pytest.skip()

    print(gdal.VersionInfo("RELEASE_NAME"))

    print("Start of environ\n")
    for env in sorted((os.environ)):
        print(env, "=", os.environ.get(env))
    print("End of environ\n")

    print("GDAL_DRIVER_PATH =", os.environ.get("GDAL_DRIVER_PATH"))
    print("LD_LIBRARY_PATH =", os.environ.get("LD_LIBRARY_PATH"))
    print("LIBRARY_PATH =", os.environ.get("LIBRARY_PATH"))
    print("PATH =", os.environ.get("PATH"))
    print("PYTHONPATH =", os.environ.get("PYTHONPATH"))

    ds = gdal.Open(testfile)
    if ds:
        print("testfile opened\n")
    else:
        print("problem opening testfile ", testfile, "\n")

    assert ds is not None
    print("testfile loaded\n")

    cs = ds.GetRasterBand(1).Checksum()
    print("checksum ", cs, "\n")
    assert cs == testsum, "bad checksum"

    # print('checksum ',cs, ' ok\n')

    gt = ds.GetGeoTransform()
    print("gt ", gt, "\n")
    wkt = ds.GetProjectionRef()
    print("wkt ", wkt, "\n")

    expected_gt = (108000.0, 90.0, 0.0, 491940.0, 0.0, -90.0)
    for i in range(6):
        assert gt[i] == pytest.approx(expected_gt[i], abs=1e-7), "bad geotransform"

    # expected_wkt = 'PROJCS["OSNI 1952 / Irish National Grid",GEOGCS["OSNI 1952",DATUM["OSNI_1952",SPHEROID["Airy 1830",6377563.396,299.3249646,AUTHORITY["EPSG","7001"]],AUTHORITY["EPSG","6188"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4188"]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",53.5],PARAMETER["central_meridian",-8],PARAMETER["scale_factor",1],PARAMETER["false_easting",200000],PARAMETER["false_northing",250000],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Easting",EAST],AXIS["Northing",NORTH],AUTHORITY["EPSG","29901"]]'
    expected_wkt = 'PROJCS["TM75 / Irish Grid",GEOGCS["TM75",DATUM["Geodetic_Datum_of_1965",SPHEROID["Airy Modified 1849",6377340.189,299.3249646,AUTHORITY["EPSG","7002"]],AUTHORITY["EPSG","6300"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4300"]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",53.5],PARAMETER["central_meridian",-8],PARAMETER["scale_factor",1.000035],PARAMETER["false_easting",200000],PARAMETER["false_northing",250000],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Easting",EAST],AXIS["Northing",NORTH],AUTHORITY["EPSG","29903"]]'

    print("wkt ", wkt, "\n")
    print("expected_wkt ", expected_wkt, "\n")
    assert wkt == expected_wkt, wkt
