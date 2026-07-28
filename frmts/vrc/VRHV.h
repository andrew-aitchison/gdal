/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  Viewranger GDAL Driver
 * Authors:  Andrew C Aitchison
 *
 ******************************************************************************
 * Copyright (c) 2015-26, Andrew C Aitchison
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

/* -*- tab-width: 4 ; indent-tabs-mode: nil ; c-basic-offset 'tab-width -*- */

#ifndef VRHV_H_INCLUDED
#define VRHV_H_INCLUDED

#define FRMT_viewranger

#include "VRC.h"

class VRHRasterBand;
// class VRHVDataset : public GDALPamDataset;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"

class VRHVDataset : public GDALPamDataset
{
    friend class VRHRasterBand;

    VSILFILE *fp = nullptr;
    // GDALColorTable      *poColorTable;
    GByte abyHeader[0x5a0] = {};

    unsigned int nMagic = 0, nPixelMetres = 0;
    int nVRHVersion = 0;
    signed int nLeft = INT_MAX, nRight = INT_MAX;
    signed int nTop = INT_MIN, nBottom = INT_MIN;
    unsigned int nScale = 0;
    unsigned int *anColumnIndex = nullptr;
    // unsigned int *anTileIndex = nullptr;
    OGRSpatialReference *poSRS = nullptr;
    char *pszLongTitle = nullptr;
    char *pszCopyright = nullptr;

    std::string sDatum = CPLStrdup("");
    short nCountry = -1;

    bool bGeoTransformValid = FALSE;
    bool bHasTriedLoadWorldFile = FALSE;
    GDALGeoTransform m_gt{0.0, 1.0, 0.0, -1.0, 0.0, 0.0};
    void LoadWorldFile();
    CPLString osWldFilename = "";

#pragma clang diagnostic pop

  private:
    CPL_DISALLOW_COPY_ASSIGN(VRHVDataset)
    // VRHVDataset &operator=(const VRHVDataset &) = delete;

  public:
    VRHVDataset() = default;  // This does not initialize abyHeader ?
#ifdef EXPLICIT_DELETE
    ~VRHVDataset() override;
#endif

    static VRHVDataset *Open(GDALOpenInfo *poOpenInfo);

    static GDALDataset *OpenWrapper(GDALOpenInfo *poOpenInfo)
    {
        return Open(poOpenInfo);
    }

    static int Identify(GDALOpenInfo *poOpenInfo);

    // Gdal <3 uses proj.4, Gdal>=3 uses proj.6, see eg:
    // https://trac.osgeo.org/gdal/wiki/rfc73_proj6_wkt2_srsbarn
    // https://gdal.org/development/rfc/rfc73_proj6_wkt2_srsbarn.html
    const OGRSpatialReference *GetSpatialRef() const override
    {
        return poSRS;
    }

    CPLErr GetGeoTransform(GDALGeoTransform &gt) const override;

    char **GetFileList() override;

    static char *VRHGetString(VSILFILE *fp, size_t byteaddr);
};

#endif
