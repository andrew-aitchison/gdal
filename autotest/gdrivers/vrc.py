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

"""This module tests the ViewRanger VRC plugin."""

import os

import gdaltest

from osgeo import gdal

# print('os.uname().sysname =',os.uname())


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
# Test reading VRC file


def test_vrc_online_1():
    """Function to test the ViewRanger VRC plugin."""
    # https://www.the-thorns.org.uk/Valle%20Antrona.VRC
    # https://www.aitchison.me.uk/forJohnT/ParcoNazionaleDeiMontiSibillini.VRC
    # https://download.freedownloadmanager.org/Windows-PC/ViewRanger-Map-Chooser/FREE-1.9.14.html
    # http://web.archive.org/web/20170714072609/http://maps3.viewranger.com/mapchooser/VRMapChooser_1_9_14_all.msi
    # http://www.viewranger.com/mapchooser/VRMapChooser.msi
    # http://www.viewranger.com/mapchooser/VRMapChooser_1_9_4_FR_DE.msi
    # testfile='/home/maps/viewranger/GBOverview.VRC'
    # testfile='/home/maps/viewranger/Germany_2_5.VRC'  # misspelt
    testfile = "/home/maps/viewranger/Germany_2_5M.VRC"  # Good

    # testsum = 216025 # GBOverview.VRC
    # testsum = 2154   # GBOverview.VRC
    testsum = 57062  # Germany_2_5M.VRC

    MapChooserExeURL = "file://home/maps/viewranger"
    "/ViewRanger Map Chooser/"
    "VRMapChooser.exe"

    if False:
        if not gdaltest.download_file(MapChooserExeURL, "VRMapChooser.exe"):
            pytest.skip()

            try:
                os.stat(testfile)
            except OSError:
                try:
                    gdaltest.unzip("tmp/cache", "tmp/cache/VRMapChooser.exe")
                except:  # noqa
                    print("Failed to unzip ", testfile, "\n")
                    pytest.skip()

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

    # gb_expected_gt =
    # (-1841870.2731215316, 3310.9550245520159, -13.025246304875619,
    #   8375316.4662204208, -16.912440131236657, -3264.1162527118681)
    expected_gt = (219006.0, 250.0, 0.0, 6641109.0, 0.0, -250.0)
    for i in range(6):
        assert gt[i] == pytest.approx(expected_gt[i], abs=1e-7), "bad geotransform"

    # gb_expected_wkt = 'PROJCS["unnamed",GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4326"]],PROJECTION["Lambert_Conformal_Conic_2SP"],PARAMETER["latitude_of_origin",4],PARAMETER["central_meridian",10],PARAMETER["standard_parallel_1",40],PARAMETER["standard_parallel_2",56],PARAMETER["false_easting",0],PARAMETER["false_northing",0],UNIT["Meter",1],AXIS["Easting",EAST],AXIS["Northing",NORTH]]'
    expected_wkt = 'PROJCS["ETRS89 / UTM zone 32N",GEOGCS["ETRS89",DATUM["European_Terrestrial_Reference_System_1989",SPHEROID["GRS 1980",6378137,298.257222101,AUTHORITY["EPSG","7019"]],AUTHORITY["EPSG","6258"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4258"]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",0],PARAMETER["central_meridian",9],PARAMETER["scale_factor",0.9996],PARAMETER["false_easting",500000],PARAMETER["false_northing",0],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Easting",EAST],AXIS["Northing",NORTH],AUTHORITY["EPSG","25832"]]'
    # print('expected_wkt ', expected_wkt, '\n')
    assert wkt == expected_wkt, wkt
