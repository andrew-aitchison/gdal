/******************************************************************************
 *
 *
 * Author:  Andrew C Aitchison
 *
 ******************************************************************************
 * Copyright (c) 2019-24, Andrew C Aitchison
 ******************************************************************************
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

#pragma once

#ifndef VRC_H_INCLUDED
#define VRC_H_INCLUDED

#define FRMT_viewranger

#include <gdal_pam.h>
#include <ogr_spatialref.h>

// #include <cpl_string.h>

// These are only enabled by -Weverything
// ... which is not appropriate for production.
#include <cpl_vsi_virtual.h>  // for HasPRead() and PRead()

// We have not fully deciphered the data format
// of VRC files with magic=0x01ce6336.
// Set *one* of these definitions (to 1)
// VRC36_PIXEL_IS_PIXEL is to be assumed if none are set.
// #define VRC36_PIXEL_IS_PIXEL 1

#include <cinttypes>
#include <cstdio>

// printf("%zu", ...) is not supported on mingw
// eg build-windows-msys2-mingw
#ifdef _WIN32
#ifdef _WIN64
#define PRI_SIZET PRIu64
#define PRI_SIZETx PRIx64
#else
#define PRI_SIZET PRIu32
#define PRI_SIZETx PRIx32
#endif
#else
#define PRI_SIZET "zu"
#define PRI_SIZETx "zx"
#endif

static const unsigned int vrc_magic = 0x002e1f7e;    // 0x7e1f2e00; //
static const unsigned int vrc_magic36 = 0x01ce6336;  // 0x3663ce01; //

// static const unsigned int nVRCNoData = 0xffffffff;
// static const unsigned int nVRCNoData = 255;
// static const unsigned int nVRCNoData = 0;
// Many maps have 0, black, or 255, white, as important colours.
// Unless we use alpha or a mask layer (good idea)
// we are stuck with 1 value in red green and blue
// (any one will trigger nodata).
// Try:
static const unsigned int nVRCNoData = 1;
// 1 doesn't work for Map ID 3038 :-(
// ... or DE_50_W943960_S5441040_E1061720_N5451280.VRC

class VRCRasterBand;

extern void dumpTileHeaderData(VSILFILE *fp, unsigned int nTileIndex,
                               unsigned int nOverviewCount,
                               const unsigned int anTileOverviewIndex[],
                               int tile_xx, int tile_yy);

extern short VRGetShort(const void *base, int byteOffset);
extern signed int VRGetInt(const void *base, unsigned int byteOffset);
extern unsigned int VRGetUInt(const void *base, unsigned int byteOffset);

extern int VRReadChar(VSILFILE *fp);
extern int VRReadShort(VSILFILE *fp);
extern int VRReadInt(VSILFILE *fp);
extern int VRReadInt(VSILFILE *fp, unsigned int byteOffset);
extern unsigned int VRReadUInt(VSILFILE *fp);
extern unsigned int VRReadUInt(VSILFILE *fp, unsigned int byteOffset);
void VRC_file_strerror_r(int nFileErr, char *buf, size_t buflen);

enum VRCinterleave : uint8_t
{
    band,
    pixel
};

void dumpPPM(unsigned int width, unsigned int height, const unsigned char *data,
             unsigned int rowlength, CPLString osBaseLabel,
             VRCinterleave eInterleave, unsigned int nMaxPPM);

extern OGRSpatialReference *CRSfromCountry(int16_t nCountry, int32_t nMapID,
                                           const char *szCountry);
extern const char *CharsetFromCountry(int16_t nCountry);

/************************************************************************/
/* ==================================================================== */
/*                         VRCDataset                                   */
/* ==================================================================== */
/************************************************************************/

// class VRCDataset : public GDALPamDataset
class VRCDataset : public GDALDataset
{
    friend class VRCRasterBand;

    VSILFILE *fp = nullptr;
    GDALColorTable *poColorTable = nullptr;
    GByte abyHeader[0x5a0]{};

    unsigned int *anColumnIndex = nullptr;
    unsigned int *anTileIndex = nullptr;
    unsigned int nMagic = 0;
    double dfPixelMetres = 0.0;
    int32_t nMapID = -1;
    signed int nLeft = INT_MAX, nRight = INT_MAX;
    signed int nTop = INT_MIN, nBottom = INT_MIN;
    signed int nTopSkipPix = 0, nRightSkipPix = 0;
    unsigned int nScale = 0;
    // unsigned int nMaxOverviewCount=7;
    int16_t nCountry = -1;
    OGRSpatialReference *poSRS = nullptr;

    std::string sFileName = "";
    std::string sLongTitle = "";
    std::string sCopyright = "";

    unsigned int tileSizeMax = 0, tileSizeMin = INT_MAX;
    unsigned int tileXcount = 0, tileYcount = 0;

    unsigned int *VRCGetTileIndex(unsigned int nTileIndexStart);
    unsigned int *VRCBuildTileIndex(unsigned int nTileIndexAddr,
                                    unsigned int nTileIndexStart);

    //VSIStatBufL oStatBufL;
    off_t st_size = 0;

  private:
    CPL_DISALLOW_COPY_ASSIGN(VRCDataset)
    // VRCDataset &operator=(const VRCDataset &) = delete;

  public:
    VRCDataset() = default;  // This does not initialize abyHeader
#ifdef EXPLICIT_DELETE
    ~VRCDataset() override;
#endif

    static GDALDataset *Open(GDALOpenInfo *poOpenInfo);
    static int Identify(GDALOpenInfo *poOpenInfo);

    // Gdal <3 uses proj.4, Gdal>=3 uses proj.6, see eg:
    // https://trac.osgeo.org/gdal/wiki/rfc73_proj6_wkt2_srsbarn
    // https://gdal.org/development/rfc/rfc73_proj6_wkt2_srsbarn.html
    const OGRSpatialReference *GetSpatialRef() const override
    {
        return poSRS;
    }

    // const char *_GetProjectionRef

    CPLErr GetGeoTransform(double *padfTransform) override;

    static char *VRCGetString(VSILFILE *fp, size_t byteaddr);
};

/************************************************************************/
/* ==================================================================== */
/*                            VRCRasterBand                             */
/* ==================================================================== */
/************************************************************************/

// class VRCRasterBand : public GDALPamRasterBand
class VRCRasterBand : public GDALRasterBand
{
    friend class VRCDataset;

    GDALColorInterp eBandInterp{GCI_Undefined};
    int nThisOverview;  // -1 for base ?
    int nOverviewCount;
    VRCRasterBand **papoOverviewBands;

    void read_VRC_Tile_36(VSILFILE *fp, int block_xx, int block_yy,
                          void *pImage);
    void read_VRC_Tile_PNG(VSILFILE *fp, int block_xx, int block_yy,
                           void *pImage);
    GByte *read_PNG(VSILFILE *fp,
                    // void *pImage,
                    unsigned int *pPNGwidth, unsigned int *pPNGheight,
                    unsigned int nVRCHeader, vsi_l_offset nPalette,
                    unsigned int nVRCDataLen,
                    // int nPNGXcount,  int nPNGYcount,
                    int nGDtile_xx, int nGDtile_yy, unsigned int nVRtile_xx,
                    unsigned int nVRtile_yy);

    int Copy_Tile_into_Block(GByte *pbyPNGbuffer, int nPNGwidth, int nPNGheight,
                             int nLeftCol, int nRightCol, int nTopRow,
                             int nBottomRow, void *pImage);

    int Shrink_Tile_into_Block(GByte *pbyPNGbuffer, int nPNGwidth,
                               int nPNGheight, int nLeftCol, int nRightCol,
                               int nTopRow, int nBottomRow, void *pImage);

    int verifySubTileFile(VSILFILE *fp, unsigned long start,
                          unsigned long finish, int nGDtile_xx, int nGDtile_yy,
                          unsigned int nVRtile_xx, unsigned int nVRtile_yy);
    int verifySubTileMem(GByte abyRawStartData[], unsigned long start,
                         unsigned long finish, int nGDtile_xx, int nGDtile_yy,
                         unsigned int nVRtile_xx, unsigned int nVRtile_yy);

  private:
    CPL_DISALLOW_COPY_ASSIGN(VRCRasterBand)
    // VRCRasterBand &operator=(const VRCRasterBand &) = delete;

  public:
    VRCRasterBand(VRCDataset *poDSIn, int nBandIn, int nThisOverviewIn,
                  int nOverviewCountIn, VRCRasterBand **papoOverviewBandsIn);

#ifdef EXPLICIT_DELETE
    ~VRCRasterBand() override;
#endif

    GDALColorInterp GetColorInterpretation() override;
    CPLErr SetColorInterpretation(GDALColorInterp eColorInterp) override;
    GDALColorTable *GetColorTable() override;

    double GetNoDataValue(int *pbSuccess) override;
    // CPLErr SetNoDataValue(double) override;
    CPLErr SetNoDataValue(double dfNoDataValue) final;

    int GetOverviewCount() override;
    GDALRasterBand *GetOverview(int iOverviewIn) override;
    CPLErr IReadBlock(int nBlockXOff, int nBlockYOff, void *pImage) override;

    int IGetDataCoverageStatus(int nXOff, int nYOff, int nXSize, int nYSize,
                               int nMaskFlagStop, double *pdfDataPct) override;
};

#endif
