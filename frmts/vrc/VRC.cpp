/******************************************************************************
 *
 *
 * Project:  GDAL
 * Purpose:  Viewranger GDAL Driver
 * Authors:  Andrew C Aitchison
 *
 ******************************************************************************
 * Copyright (c) 2015-24, Andrew C Aitchison
 ******************************************************************************
 * Portions taken from gdal-2.2.3/frmts/png/pngdataset.cpp and other GDAL code
 * Copyright (c) 2000, Frank Warmerdam
 * Copyright (c) 2007-2014, Even Rouault <even dot rouault at spatialys.com>
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

// #ifdef FRMT_vrc

#include "VRC.h"

#include "png_crc.h"  // for crc pngcrc_for_VRC, used in: PNGCRCcheck

#include <algorithm>  // for std::max, std::min and std::copy
#include <array>
#include <cinttypes>

template <class T, class U> void static vector_append(T &a, U &b)
{
    // a.insert(std::end(a),std::begin(b),std::end(b));
    std::copy(b.begin(), b.end(), std::back_inserter(a));
}

void VRC_file_strerror_r(int nFileErr, char *const buf, size_t buflen)
{
    if (buf == nullptr || buflen < 1)
    {
        return;
    }
#define STRERR_DEBUG(...)

    STRERR_DEBUG("Viewranger", "%s", VSIStrerror(nFileErr));
    (void)CPLsnprintf(buf, buflen, "%s", VSIStrerror(nFileErr));
#undef STRERR_DEBUG
}

typedef struct
{
    size_t nCurrent;
    std::vector<png_byte> vData;
} VRCpng_callback_t;

// PNG values are opposite-endian from other values in the .VRC file.

static unsigned int PNGGetUInt(const void *base, const size_t byteOffset)
{
    if (nullptr == base)
    {
        return 0;
    }

    // const unsigned char
    auto *buf = &(static_cast<const unsigned char *>(base)[byteOffset]);
    unsigned int vv = buf[3];
    vv |= static_cast<unsigned int>(buf[2]) << 8;
    vv |= static_cast<unsigned int>(buf[1]) << 16;
    vv |= static_cast<unsigned int>(buf[0]) << 24;

    return (vv);
}

static bool isNullTileIndex(unsigned int nIndex)
{
    // This looks promising on DE_50 tiles
    // see how good it is in general
    return ((nIndex % 100) == 0 && nIndex < 10000);
}

static unsigned int PNGReadUInt(VSILFILE *fp)
{
    std::array<unsigned char, 4> buf;
    // int ret =
    VSIFReadL(&buf, 1, 4, fp);
    return PNGGetUInt(&buf, 0);
}

static void PNGAPI VRC_png_read_data_fn(png_structp png_read_ptr,
                                        png_bytep data, png_size_t length)
{
    if (png_read_ptr == nullptr)
    {
        CPLDebug("Viewranger PNG", "VRC_png_read_data_fn given null io ptr");
        // png_warning(png_read_ptr,
        //             "VRC_png_read_data_fn given null io ptr\n");
        return;
    }
    if (length < 1)
    {
        CPLDebug("Viewranger PNG",
                 "VRC_png_read_data_fn() requested length %ld < 1",
                 static_cast<long>(length));
        return;
    }

    auto *pVRCpng_callback =
        static_cast<VRCpng_callback_t *>(png_get_io_ptr(png_read_ptr));

    // Sanity checks on our data pointer
    if (pVRCpng_callback == nullptr)
    {
        return;
    }

    // Sanity check the function args
    if (length > pVRCpng_callback->vData.size())
    {

        const size_t nSpare =
            pVRCpng_callback->vData.size() - pVRCpng_callback->nCurrent;
        // Copy the data we have ...
        if (nSpare > 0)
        {  // pVRCpng_callback->nCurrent < pVRCpng_callback.size())
            memcpy(data,
                   pVRCpng_callback->vData.data() + pVRCpng_callback->nCurrent,
                   static_cast<size_t>(nSpare));
        }
        pVRCpng_callback->nCurrent = pVRCpng_callback->vData.size();
        return;
    }

    memcpy(data, pVRCpng_callback->vData.data() + pVRCpng_callback->nCurrent,
           length);
    pVRCpng_callback->nCurrent += length;

    if (pVRCpng_callback->nCurrent > pVRCpng_callback->vData.size())
    {
        CPLDebug("Viewranger PNG",
                 "VRC_png_read_data_fn(%p %p %" PRI_SIZET
                 ") reached end of data",
                 png_read_ptr, data, length);
    }
}

static int PNGCRCcheck(const std::vector<png_byte> &vData, uint32_t nGiven)
{
    if (vData.size() < 8)
    {
        CPLDebug("Viewranger PNG",
                 "PNGCRCcheck: only %" PRI_SIZET " bytes - need at least 8",
                 vData.size());
        return -1;
    }
    const unsigned char *pBuf = &(vData.back()) - 3;
    const uint32_t nLen = PNGGetUInt(pBuf - 4, 0);

    if (sizeof(size_t) != sizeof(std::vector<png_byte>::size_type))
    {
        //      #warning "sizeof(size_t) != sizeof(std::vector<png_byte>::size_type)"
        CPLDebug("Viewranger",
                 "sizeof(size_t) = %" PRI_SIZET " != %" PRI_SIZET
                 "sizeof(std::vector<png_byte>::size_type)",
                 sizeof(size_t), sizeof(std::vector<png_byte>::size_type));
    }

    if (nLen > vData.size() /* || nLen > 1L << 31U */)
    {
        // from PNG spec nLen <= 2^31
        CPLDebug("Viewranger PNG",
                 "PNGCRCcheck: nLen %u > buffer length %" PRI_SIZET, nLen,
                 vData.size());
        return -1;
    }

    //CPLDebug("Viewranger PNG", "PNGCRCcheck((%p, %lu) %u, x%08lx)", pBuf,
    //             vVRCpng_callback->current, nLen, nGiven);

    const uint32_t nFileCRC = PNGGetUInt(vData.data(), vData.size() + nLen);
    if (nGiven == nFileCRC)
    {
        CPLDebug("Viewranger PNG",
                 "PNGCRCcheck(x%08" PRIu32 ") given CRC matches CRC from file",
                 nFileCRC);
    }
    else
    {
        CPLDebug("Viewranger PNG",
                 "PNGCRCcheck(x%08" PRIu32
                 ") CRC given does not match x%08" PRIu32 " from file",
                 nGiven, nFileCRC);
        return -1;
    }

    const uint32_t nComputed =
        pngcrc_for_VRC(pBuf, static_cast<unsigned int>(nLen) + 4U);
    const int ret = (nGiven == nComputed);
    if (ret == 0)
    {
        CPLDebug("Viewranger PNG",
                 "PNG file: CRC given x%08" PRIu32 ", calculated x%08" PRIu32
                 "x",
                 nGiven, nComputed);
    }

    return ret;
}

// -------------------------------------------------------------------------
// Returns a (null-terminated) string allocated from VSIMalloc.
// The 32 bit length of the string is stored in file fp at byteaddr.
// The string itself is stored immediately after its length;
// it is *not* null-terminated in the file.
// If index pointer is nul then an empty string is returned
// (rather than a null pointer).
//
char *VRCDataset::VRCGetString(VSILFILE *fp, size_t byteaddr)
{
    if (byteaddr == 0)
    {
        return (VSIStrdup(""));
    }

    // const
    int32_t string_length = 0;

    if ((fp)->HasPRead())
    {
        if (fp->PRead(&string_length, sizeof(string_length), byteaddr) <
            sizeof(string_length))
        {
            CPLError(CE_Failure, CPLE_FileIO,
                     "error reading length of VRC string");
            return (VSIStrdup(""));
        }
    }
    else
    {
        const int nSeekResult = VSIFSeekL(fp, byteaddr, SEEK_SET);
        if (nSeekResult)
        {
            CPLError(CE_Failure, CPLE_AppDefined, "cannot seek to VRC string");
            return (VSIStrdup(""));
        }
        string_length = VRReadInt(fp);
    }

    if (string_length <= 0)
    {
        if (string_length < 0)
        {
            CPLDebug("Viewranger",
                     "odd length for string %012" PRI_SIZETx
                     " - length %" PRId32,
                     byteaddr, string_length);
        }
        return (VSIStrdup(""));
    }
    const size_t ustring_length = static_cast<unsigned>(string_length);

    char *pszNewString = static_cast<char *>(CPLMalloc(1 + ustring_length));

    const size_t bytesread =
        ((fp)->HasPRead()) ? fp->PRead(pszNewString, ustring_length,
                                       byteaddr + sizeof(string_length))
                           : VSIFReadL(pszNewString, 1, ustring_length, fp);

    if (bytesread < ustring_length)
    {
        VSIFree(pszNewString);
        CPLError(CE_Failure, CPLE_AppDefined, "problem reading string\n");
        return (VSIStrdup(""));
    }

    pszNewString[ustring_length] = 0;
    // CPLDebug("Viewranger", "read string %s at %08x - length %d",
    //         pszNewString, byteaddr, ustring_length);
    return pszNewString;
}

/************************************************************************/
/*                           VRCRasterBand()                            */
/************************************************************************/

VRCRasterBand::VRCRasterBand(VRCDataset *poDSIn, int nBandIn,
                             int nThisOverviewIn, int nOverviewCountIn,
                             VRCRasterBand **papoOverviewBandsIn)
    : eBandInterp(GCI_Undefined), nThisOverview(nThisOverviewIn),
      nOverviewCount(nOverviewCountIn), papoOverviewBands(papoOverviewBandsIn)
{
    VRCDataset *poVRCDS = poDSIn;
    poDS = static_cast<GDALDataset *>(poVRCDS);
    nBand = nBandIn;
    CPLDebug("Viewranger", "%s %p->VRCRasterBand(%p, %d, %d, %d, %p)",
             poVRCDS->sFileName.c_str(),
             // poVRCDS->sLongTitle.c_str(),
             this, poVRCDS, nBand, nThisOverview, nOverviewCount,
             papoOverviewBands);

    if (nOverviewCount >= 32)
    {
        // This is unnecessarily big;
        // the scale factor will not fit in an int, and
        // a 1cm / pixel map of the world will have a one pixel overview.
        // nOverviewCount=32;
        CPLError(CE_Failure, CPLE_AppDefined, "%d overviews is not practical",
                 nOverviewCount);
        nOverviewCount = 0;
        return;  // Can I return failure ?
    }
    if (nOverviewCount >= 0 && nThisOverview >= nOverviewCount)
    {
        if (nOverviewCount > 0)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "failed: cannot set overview %d of %d\n", nThisOverview,
                     nOverviewCount);
        }
        return;  // CE_Failure;  // nullptr; ?
    }

    auto nOverviewScale = static_cast<signed int>(
        1U << static_cast<unsigned int>(nThisOverview + 1));
    nRasterXSize =
        poVRCDS->nRasterXSize / nOverviewScale;  // >> nOverviewShift;
    nRasterYSize =
        poVRCDS->nRasterYSize / nOverviewScale;  // >> nOverviewShift;

    // int tileXcount = poVRCDS->tileXcount;
    // int tileYcount = poVRCDS->tileYcount;

    CPLDebug("Viewranger", "nRasterXSize %d nRasterYSize %d", nRasterXSize,
             nRasterYSize);

    // Image Structure Metadata:  INTERLEAVE=PIXEL would be good
    GDALRasterBand::SetMetadataItem("INTERLEAVE", "PIXEL", "IMAGE_STRUCTURE");

    if (poVRCDS->nMagic == vrc_magic)
    {
        eDataType = GDT_Byte;  // GDT_UInt32;
        // GCI_Undefined;  // GCI_GrayIndex;  // GCI_PaletteIndex;
        // GCI_RedBand;  // GCI_GreenBand;  // GCI_BlueBand;  //GCI_AlphaBand;

        switch (nBand)
        {
            case 1:
                eBandInterp = GCI_RedBand;
                break;
            case 2:
                eBandInterp = GCI_GreenBand;
                break;
            case 3:
                eBandInterp = GCI_BlueBand;
                break;
            case 4:
                eBandInterp = GCI_AlphaBand;
                break;
            default:
                CPLDebug("Viewranger",
                         "vrc_pixel_is_pixel band %d unexpected !", nBand);
        }

        CPLDebug("Viewranger", "vrc_pixel_is_pixel nThisOverview=%d",
                 nThisOverview);
        if (nThisOverview < -1)
        {
            CPLDebug("Viewranger", "\toverview %d invalid", nThisOverview);
            nThisOverview = -1;  //  main view
        }
        else if (nThisOverview > 7)
        {
            CPLDebug("Viewranger", "\toverview %d unexpected", nThisOverview);
        }

        nBlockXSize =
            static_cast<signed int>(poVRCDS->tileSizeMax) / nOverviewScale;
        nBlockYSize = nBlockXSize;
        if (nBlockXSize < 1)
        {
            CPLDebug("Viewranger", "overview %d block %d x %d too small",
                     nThisOverview, nBlockXSize, nBlockYSize);
            nBlockYSize = nBlockXSize = 1;
        }
        CPLDebug("Viewranger", "overview %d block %d x %d", nThisOverview,
                 nBlockXSize, nBlockYSize);
    }
    else if (poVRCDS->nMagic == vrc_magic36)
    {
        CPLError(CE_Warning, CPLE_AppDefined,
                 "Sorry, .VRC files with magic %08x not yet understood\n",
                 vrc_magic36);
    }

    VRCRasterBand::SetColorInterpretation(eBandInterp);

    /* -------------------------------------------------------------------- */
    /*      If this is the base layer, create the overview layers.          */
    /* -------------------------------------------------------------------- */

    if (nOverviewCount >= 0 && nThisOverview == -1)
    {
        if (papoOverviewBands != nullptr)
        {
            CPLDebug("Viewranger OVRV",
                     "%s nThisOverview==-1 but %d papoOverviewBands already "
                     "set at %p",
                     poVRCDS->sFileName.c_str(),
                     // poVRCDS->sLongTitle.c_str(),
                     nOverviewCount + 1, papoOverviewBands);
        }
        else
        {
            if (nOverviewCount != 6)
            {
                CPLDebug("Viewranger OVRV",
                         "nThisOverview==-1 expected 6 overviews but given %d",
                         nOverviewCount);
                // nOverviewCount=6;  // Hack. FIXME
            }
            if (nOverviewCount >= 32)
            {
                // This is unnecessarily big;
                // the scale factor will not fit in an int, and
                // a 1cm / pixel map of the world will have a one pixel
                // overview. nOverviewCount=32;
                CPLDebug("Viewranger OVRV",
                         "%s Reducing nOverviewCount from %d to 6",
                         poVRCDS->sFileName.c_str(),
                         // poVRCDS->sLongTitle.c_str(),
                         nOverviewCount);
                nOverviewCount = 6;
            }
            if (nOverviewCount >= 0)
            {
                papoOverviewBands = static_cast<VRCRasterBand **>(CPLCalloc(
                    sizeof(void *), 1 + static_cast<size_t>(nOverviewCount)));
            }
            CPLDebug("Viewranger OVRV",
                     "%s this = %p VRCRasterBand(%p, %d, %d, %d, %p)",
                     poVRCDS->sFileName.c_str(),
                     // poVRCDS->sLongTitle.c_str(),
                     this, poVRCDS, nBandIn, nThisOverview, nOverviewCount,
                     papoOverviewBands);
            // #pragma unroll
            for (int i = 0; i < nOverviewCount; i++)
            {
                if (papoOverviewBands[i])
                {
                    CPLError(CE_Warning, CPLE_AppDefined,
                             "\toverview %p[%d] already set to %p",
                             papoOverviewBands, i, papoOverviewBands[i]);
                }
                else
                {
                    papoOverviewBands[i] = new  // 8 Feb 2021 leaks memory
                        VRCRasterBand(poVRCDS, nBand, i,
                                      // the overview has no overviews, so
                                      0, nullptr
                                      // not: nOverviewCount, papoOverviewBands
                        );
                }
            }
        }
    }
    else
    {  // !(nOverviewCount>=0 && nThisOverview == -1)

        if (nThisOverview < -1 || nThisOverview > nOverviewCount)
        // Off-by-one somewhere ?
        {
            CPLDebug("ViewrangerOverview",
                     "%s %p nThisOverview==%d out of range [-1,%d]",
                     poVRCDS->sFileName.c_str(),
                     // poVRCDS->sLongTitle.c_str(),
                     this, nThisOverview, nOverviewCount);
        }
    }

    CPLDebug("Viewranger", "%s %p->VRCRasterBand(%p, %d, %d, %d, %p) finished",
             poVRCDS->sFileName.c_str(),
             // poVRCDS->sLongTitle.c_str(),
             this, poVRCDS, nBand, nThisOverview, nOverviewCount,
             papoOverviewBands);
}

/************************************************************************/
/*                          ~VRCRasterBand()                            */
/************************************************************************/
#ifdef EXPLICIT_DELETE
VRCRasterBand::~VRCRasterBand()
{
    CPLDebug("Viewranger", "deleting %p->VRCRasterBand(%p, %d, %d, %d, %p)",
             this, poDS, nBand, nThisOverview, nOverviewCount,
             papoOverviewBands);

    if (papoOverviewBands)
    {
        if (nThisOverview >= 0)
        {
            CPLDebug("Viewranger",
                     "Did not expect an overview %p to have overviews %p", this,
                     papoOverviewBands);
        }

        CPLDebug("Viewranger", "deleting papoOverviewBands %p",
                 papoOverviewBands);
        VRCRasterBand **papo = papoOverviewBands;
        if (nOverviewCount > 0)
        {
            const int nC = nOverviewCount;
            nOverviewCount = 0;
            // #pragma unroll
            for (int i = 0; i < nC; i++)
            {
                if (papo[i])
                {
                    papo[i]->nOverviewCount = 0;
                    CPLDebug("Viewranger", "deleting papoOverviewBands[%d]=%p",
                             i, papo[i]);
                    delete papo[i];
                    papo[i] = nullptr;
                }
            }
        }
        CPLFree(papoOverviewBands);
        papoOverviewBands = nullptr;
    }
}
#endif

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr VRCRasterBand::IReadBlock(int nBlockXOff, int nBlockYOff, void *pImage)
{
    auto *poGDS = static_cast<VRCDataset *>(poDS);

    CPLDebug("Viewranger", "IReadBlock(%d,%d,%p) %d", nBlockXOff, nBlockYOff,
             pImage, nThisOverview);
    CPLDebug("Viewranger",
             "Block (%d,%d) %d x %d band %d (%d x %d) overview %d", nBlockXOff,
             nBlockYOff, nBlockXSize, nBlockYSize, nBand, nRasterXSize,
             nRasterXSize, nThisOverview);

    if (poGDS->nMagic == vrc_magic)
    {
        read_VRC_Tile_PNG(poGDS->fp, nBlockXOff, nBlockYOff, pImage);
        // return CE_None;  // I cannot yet confirm no errors
    }

    return CE_None;
}

/************************************************************************/
/*                           GetNoDataValue()                           */
/************************************************************************/

double VRCRasterBand::GetNoDataValue(int *pbSuccess)
{
    if (pbSuccess)
    {
        *pbSuccess = TRUE;
    }
    return nVRCNoData;
}

/************************************************************************/
/*                           SetNoDataValue()                           */
/************************************************************************/
CPLErr VRCRasterBand::SetNoDataValue(double dfNoDataValue)
{
    (void)dfNoDataValue;
    // Users cannot set NoDataValue; this is read-only data.
    return CE_Failure;
}

/************************************************************************/
/*                          IGetDataCoverageStatus()                    */
/************************************************************************/

// See https://trac.osgeo.org/gdal/wiki/rfc63_sparse_datasets_improvements
// and
// https://github.com/rouault/gdal2/blob/sparse_datasets/gdal/frmts/gtiff/geotiff.cpp
/* Most of this function
 * Copyright (c) 1998, 2002, Frank Warmerdam <warmerdam@pobox.com>
 * Copyright (c) 2007-2015, Even Rouault <even dot rouault at spatialys dot com>
 */
int VRCRasterBand::IGetDataCoverageStatus(int nXOff, int nYOff, int nXSize,
                                          int nYSize, int nMaskFlagStop,
                                          double *pdfDataPct)
{
    int nStatus = 0;
    auto *poGDS = static_cast<VRCDataset *>(poDS);
    if (poGDS->anTileIndex == nullptr)
    {
        nStatus = GDAL_DATA_COVERAGE_STATUS_UNIMPLEMENTED |
                  GDAL_DATA_COVERAGE_STATUS_DATA;
        CPLDebug("Viewranger",
                 "IGetDataCoverageStatus(%d, %d, %d, %d, %d, %p) not yet "
                 "available - Tile Index not yet read",
                 nXOff, nYOff, nXSize, nYSize, nMaskFlagStop, pdfDataPct);
        if (pdfDataPct)
        {
            *pdfDataPct = -1.0;
        }
        return nStatus;
    }

    CPLDebug("Viewranger",
             "IGetDataCoverageStatus(%d, %d, %d, %d, %d, %p) top skip %d right "
             "skip %d",
             nXOff, nYOff, nXSize, nYSize, nMaskFlagStop, pdfDataPct,
             poGDS->nTopSkipPix, poGDS->nRightSkipPix);

    const int iXBlockStart = nXOff / nBlockXSize;
    const int iXBlockEnd = (nXOff + nXSize - 1) / nBlockXSize;
    const int iYBlockStart = nYOff / nBlockYSize;
    const int iYBlockEnd = (nYOff + nYSize - 1) / nBlockYSize;

    GIntBig nPixelsData = 0;
    const int nTopEdge = MAX(nYOff, poGDS->nTopSkipPix);
    const int nRightEdge =  // nXOff + nXSize;  //
        MIN(nXOff + nXSize, poGDS->nRasterXSize - poGDS->nRightSkipPix);
    for (int iY = iYBlockStart; iY <= iYBlockEnd; ++iY)
    {
        for (int iX = iXBlockStart; iX <= iXBlockEnd; ++iX)
        {
            const int nBlockId = iX + (iY * nBlocksPerRow);
            bool bHasData = false;
            if (poGDS->anTileIndex[nBlockId] == 0)
            {
                nStatus |= GDAL_DATA_COVERAGE_STATUS_EMPTY;
            }
            else
            {
                bHasData = true;
            }
            if (bHasData)
            {
                // We could be more accurate by looking at the png sub-tiles.
                // We should also discount any strip we added for short (or
                // narrow?) tiles.
                nPixelsData += static_cast<GIntBig>(
                                   MIN((iX + 1) * nBlockXSize, nRightEdge) -
                                   MAX(iX * nBlockXSize, nXOff)) *
                               static_cast<GIntBig>(
                                   MIN((iY + 1) * nBlockYSize, nYOff + nYSize) -
                                   MAX(iY * nBlockYSize, nTopEdge));
                nStatus |= GDAL_DATA_COVERAGE_STATUS_DATA;
            }
            if (nMaskFlagStop != 0 &&
                (nMaskFlagStop & nStatus) == nMaskFlagStop)
            {
                if (pdfDataPct)
                {
                    *pdfDataPct = -1.0;
                }
                return nStatus;
            }
        }
    }

    const double dfDataPct =
        100.0 * static_cast<double>(nPixelsData) /
        (static_cast<double>(nXSize) * static_cast<double>(nYSize));
    if (pdfDataPct)
    {
        *pdfDataPct = dfDataPct;
    }

    CPLDebug("Viewranger",
             "IGetDataCoverageStatus(%d, %d, %d, %d, %d, %p) returns %d with "
             "%f%% coverage",
             nXOff, nYOff, nXSize, nYSize, nMaskFlagStop, pdfDataPct, nStatus,
             dfDataPct);

    return nStatus;
}

/************************************************************************/
/*                       GetColorInterpretation()                       */
/************************************************************************/

GDALColorInterp VRCRasterBand::GetColorInterpretation()

{
    const auto *poGDS = static_cast<VRCDataset *>(poDS);
    if (poGDS->nMagic == vrc_magic)
    {
        CPLDebug("Viewranger",
                 "VRCRasterBand::GetColorInterpretation vrc "
                 "GetColorInterpretation %08x %d",
                 poGDS->nMagic, this->eBandInterp);
        return this->eBandInterp;
    }

    if (poGDS->nMagic == vrc_magic36)
    {
        CPLDebug("Viewranger",
                 "VRCRasterBand::GetColorInterpretation vrc36 "
                 "GetColorInterpretation %08x %d",
                 poGDS->nMagic, this->eBandInterp);
        return this->eBandInterp;
    }

    CPLDebug("Viewranger",
             "VRCRasterBand::GetColorInterpretation unexpected magic %08x "
             "- GetColorInterpretation %d -but returning GrayIndex",
             poGDS->nMagic, this->eBandInterp);
    return GCI_GrayIndex;
}

/************************************************************************/
/*                       SetColorInterpretation()                       */
/************************************************************************/

CPLErr VRCRasterBand::SetColorInterpretation(GDALColorInterp eColorInterp)
{
    (void)eColorInterp;
    // return CPLE_NotSupported;
    return static_cast<CPLErr>(CPLE_None);
}

/************************************************************************/
/*                           GetColorTable()                            */
/************************************************************************/

GDALColorTable *VRCRasterBand::GetColorTable()

{
    return nullptr;
}

/************************************************************************/
/*                           ~VRCDataset()                             */
/************************************************************************/
#ifdef EXPLICIT_DELETE
VRCDataset::~VRCDataset()
{
    GDALDataset::FlushCache(TRUE);

    if (fp != nullptr)
        VSIFCloseL(fp);

    delete (poColorTable);

    if (anColumnIndex != nullptr)
    {
        VSIFree(anColumnIndex);
        anColumnIndex = nullptr;
    }
    if (anTileIndex != nullptr)
    {
        VSIFree(anTileIndex);
        anTileIndex = nullptr;
    }
    if (poSRS)
    {
        poSRS->Release();
        poSRS = nullptr;
    }
    if (pszSRS != nullptr)
    {
        VSIFree(pszSRS);
        pszSRS = nullptr;
    }
}
#endif

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/
CPLErr VRCDataset::GetGeoTransform(double *padfTransform)
{
    const double tenMillion = 10.0 * 1000 * 1000;

    double dLeft = nLeft;
    double dRight = nRight;
    double dTop = nTop;
    double dBottom = nBottom;

    if (nCountry == 17)
    {
        // This is unlikely to be correct.
        // USA, Discovery (Spain, Greece) and some Belgium (VRH height) maps
        // have coordinate unit which is not metres.
        // It might be some part of a degree, eg 1 degree/ten million.
        CPLDebug("Viewranger",
                 "MapID %d country/srs 17 USA?Discovery(Spain, Greece)?Belgium "
                 "grid is "
                 "unknown. Current guess is unlikely to be correct.",
                 nMapID);
        CPLDebug("Viewranger",
                 "raw corner positions: TL: %.10g %.10g BR: %.10g %.10g", dTop,
                 dLeft, dBottom, dRight);
        const double factor = 9.0 * 1000 * 1000;
        dLeft /= factor;
        dRight /= factor;
        dTop /= factor;
        dBottom /= factor;
        CPLDebug("ViewrangerHV", "scaling by %g TL: %g %g BR: %g %g", factor,
                 dTop, dLeft, dBottom, dRight);
    }
    else if (nCountry == 155)
    {
        // New South Wales, Australia uses GDA94/MGA55 EPSG:28355
        // but without the 10million metre false_northing
        dLeft = 1.0 * nLeft;
        dRight = 1.0 * nRight;
        dTop = 1.0 * nTop + tenMillion;
        dBottom = 1.0 * nBottom + tenMillion;

        CPLDebug("Viewranger", "shifting by 10 million: TL: %g %g BR: %g %g",
                 dTop, dLeft, dBottom, dRight);
    }

    // Xgeo = padfTransform[0] + pixel*padfTransform[1] + line*padfTransform[2];
    // Ygeo = padfTransform[3] + pixel*padfTransform[4] + line*padfTransform[5];

    padfTransform[0] = dLeft;
    padfTransform[1] = 1.0 * dRight - dLeft;
    padfTransform[2] = 0.0;
    padfTransform[3] = dTop;
    padfTransform[4] = 0.0;
    padfTransform[5] = 1.0 * dBottom - dTop;

    {
        padfTransform[1] /= (GetRasterXSize());
        padfTransform[5] /= (GetRasterYSize());
    }

    if (nMagic != vrc_magic && nMagic != vrc_magic36)
    {
        CPLDebug("Viewranger", "nMagic x%08x unknown", nMagic);
    }

    CPLDebug("Viewranger", "padfTransform raster %d x %d", GetRasterXSize(),
             GetRasterYSize());
    CPLDebug("Viewranger", "padfTransform %g %g %g", padfTransform[0],
             padfTransform[1], padfTransform[2]);
    CPLDebug("Viewranger", "padfTransform %g %g %g", padfTransform[3],
             padfTransform[4], padfTransform[5]);
    return CE_None;
}

/************************************************************************/
/*                              Identify()                              */
/************************************************************************/

int VRCDataset::Identify(GDALOpenInfo *poOpenInfo)

{
    if (poOpenInfo == nullptr)
    {
        return GDAL_IDENTIFY_FALSE;
    }
    const char *pszFileName = CPLGetFilename(poOpenInfo->pszFilename);
    if (pszFileName == nullptr)  //-V547
    {
        return GDAL_IDENTIFY_FALSE;
    }
#if HAS_SAFE311
    if (poOpenInfo->IsExtensionEqualToCI("VRC"))
#else
    if (!EQUAL(CPLGetExtension(pszFileName), "VRC"))
#endif
    {
        return GDAL_IDENTIFY_FALSE;
    }

    if (poOpenInfo->nHeaderBytes < 12)
    {
        return GDAL_IDENTIFY_UNKNOWN;
    }

    const unsigned int nMagic = VRGetUInt(poOpenInfo->pabyHeader, 0);
    // const unsigned int version = VRGetUInt(poOpenInfo->pabyHeader, 4);

    const unsigned int nb64k1 = VRGetUInt(poOpenInfo->pabyHeader, 8);
    const bool b64k1 = (nb64k1 == 0x00010001);
    if (nMagic == vrc_magic)
    {
        CPLDebug("Viewranger", "VRC file %s supported",
                 poOpenInfo->pszFilename);

        if (!b64k1)
        {
            CPLDebug("Viewranger",
                     "VRC file %s - limited support for unusual third long "
                     "0x%08x - expected 0x00010001",
                     poOpenInfo->pszFilename, nb64k1);
        }
        return GDAL_IDENTIFY_TRUE;
    }

    if (nMagic == vrc_magic36)
    {
        CPLError(
            CE_Warning, CPLE_AppDefined,
            "%s: image data for .VRC magic 0x3663ce01 files not yet understood",
            poOpenInfo->pszFilename);

        if (!b64k1)
        {
            CPLDebug("Viewranger",
                     "VRC file %s - limited support for unusual third long "
                     "0x%08x - expected 0x00010001",
                     poOpenInfo->pszFilename, nb64k1);
        }

        return GDAL_IDENTIFY_FALSE;
    }

    return GDAL_IDENTIFY_FALSE;
}

/************************************************************************/
/*                              VRCGetTileIndex()                       */
/************************************************************************/

unsigned int *VRCDataset::VRCGetTileIndex(unsigned int nTileIndexStart)
{
    // We were reading from abyHeader;
    // the next bit may be too big for that,
    // so we need to start reading directly from the file.

    // int nTileStart = -1;
    if (VSIFSeekL(fp, static_cast<size_t>(nTileIndexStart), SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined, "cannot seek to VRC tile index");
        return nullptr;
    }

    auto *anNewTileIndex = static_cast<unsigned int *>(
        VSIMalloc3(sizeof(unsigned int), static_cast<size_t>(tileXcount),
                   static_cast<size_t>(tileYcount)));
    if (anNewTileIndex == nullptr)
    {
        CPLError(CE_Failure, CPLE_OutOfMemory,
                 "Cannot allocate memory for tile index");
        return nullptr;
    }

    // Read Tile Index into memory
    // rotating it as we read it,
    // since viewranger files start by going up the left column
    // whilst gdal expects to go left to right across the top row.
    for (unsigned int i = 0; i < tileXcount; i++)
    {
        unsigned int q = (tileXcount * (tileYcount - 1)) + i;
        for (unsigned int j = 0; j < tileYcount; j++)
        {
            unsigned int nValue = VRReadUInt(fp);
            // Ignore the index if it points
            // outside the limits of the file
            if (/* nValue <= 0 || */ nValue >= oStatBufL.st_size)
            {
                CPLDebug("Viewranger",
                         "anNewTileIndex[%u] (%u %u) addr x%08x not in file", q,
                         i, j, nValue);
                nValue = 0;  // nVRCNoData ? ;
            }
            CPLDebug("Viewranger",
                     "setting anNewTileIndex[%u] (%u %u) to %u=x%08x", q, i, j,
                     nValue, nValue);
            anNewTileIndex[q] = nValue;
            q -= tileXcount;
        }
    }

    // Separate loop, since the previous loop has sequential reads
    // and this loop has random reads.
    for (unsigned int q = 0; q < tileXcount * tileYcount; q++)
    {
        const unsigned int nIndex = anNewTileIndex[q];
        if (nIndex < 16)
        {
            CPLDebug("Viewranger",
                     "anNewTileIndex[%u]=x%08x=%u - points into file header", q,
                     nIndex, nIndex);
            anNewTileIndex[q] = 0;
            continue;
        }

        // This looks promising on DE_50 tiles
        // see how good it is in general
        if (isNullTileIndex(nIndex))
        {
            CPLDebug(
                "Viewranger",
                "anNewTileIndex[%u]=x%08x=%u - ignore small multiples of 100",
                q, nIndex, nIndex);
            anNewTileIndex[q] = 0;
            continue;
        }
        const uint32_t nValue = VRReadUInt(fp, nIndex);
        if (/*nIndex>0 && */ nValue != 7)
        {
            CPLDebug(
                "Viewranger",
                "anNewTileIndex[%u]=%08x points to %u=x%08x - expected seven.",
                q, nIndex, nValue, nValue);
        }
    }
    return anNewTileIndex;
}

// MapId==8 files may have more than one tile.
// When this is so there is no tile index (that I can find),
// so we have to wander through the tile overview indices to build it.
//
// This may be a bit hacky.
//
// ToDo: These files have *two* tile indexes;
// the names used in this code need to be clearer,
// both inside and outside this function.
unsigned int *VRCDataset::VRCBuildTileIndex(unsigned int nTileIndexAddr,
                                            unsigned int nTileIndexStart)
{
    if (nMapID != 8)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRCBuildTileIndex called for a map with mapID %d", nMapID);
    }
    // Is this limit (eg 64k x 64k tiles) reasonable ?
    if (tileXcount * tileYcount >= UINT_MAX)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRCBuildTileIndex(x%x) called for oversized (%u x %u) image",
                 nTileIndexStart, tileXcount, tileYcount);
        return nullptr;
    }
    if (VSIFSeekL(fp, static_cast<size_t>(nTileIndexStart), SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "cannot seek to VRC tile index start 0x%xu", nTileIndexStart);
        return nullptr;
    }

    auto *anFirstTileIndex = static_cast<unsigned int *>(
        VSIMalloc3(sizeof(unsigned int), static_cast<size_t>(tileXcount),
                   static_cast<size_t>(tileYcount)));
    if (anFirstTileIndex == nullptr)
    {
        CPLError(CE_Failure, CPLE_OutOfMemory,
                 "Cannot allocate memory for first tile index");
        return nullptr;
    }
    auto *anNewTileIndex = static_cast<unsigned int *>(
        VSIMalloc3(sizeof(unsigned int), static_cast<size_t>(tileXcount),
                   static_cast<size_t>(tileYcount)));
    if (anNewTileIndex == nullptr)
    {
        VSIFree(anFirstTileIndex);
        CPLError(CE_Failure, CPLE_OutOfMemory,
                 "Cannot allocate memory for second tile index");
        return nullptr;
    }

    for (unsigned int ii = 0U; ii < tileXcount * tileYcount; ii++)
    {
        anFirstTileIndex[ii] =
            VRReadUInt(fp, nTileIndexAddr + (ii * sizeof(unsigned int)));
        anNewTileIndex[ii] = 0;
    }
    unsigned int nTileFound = 0;
    unsigned int nLastTileFound = anNewTileIndex[nTileFound++] =
        nTileIndexStart;

    while (nTileFound < tileXcount * tileYcount)
    {
        if (isNullTileIndex(anFirstTileIndex[nTileFound]))
        {
            anNewTileIndex[nTileFound++] = 0;
            continue;
        }

        // GDAL tiles start at the top left and count across then down.
        // VR tiles start at the bottom left and count up then right;
        // but the PNG tiles within each VR tile count right and down !
        const unsigned int nVRow = nTileFound % tileYcount;
        // const int nGRow = tileYcount-1 - nVRow;
        // int nVCol = (nTileFound-nVRow) / tileYcount;
        // int nGCol = nVCol;
        // int nGdalTile = nGCol + nGRow * tileXcount;
        // int nGdalTile = (nTileFound-nTileFound % tileYcount) / tileYcount
        //    + (tileYcount-1 - nTileFound % tileYcount) * tileXcount;
        const unsigned int nGdalTile =
            ((nTileFound - nVRow) / tileYcount) + (nVRow * tileXcount);

        // Ignore the index if it points
        // outside the limits of the file
        if (/* nLastTileFound <= 0 || */ nLastTileFound >= oStatBufL.st_size)
        {
            anNewTileIndex[nTileFound] = 0;
            nTileFound++;
            continue;  // Hack. rename nLastFound to ..count.. ?
        }

        const int nOverviewCount = VRReadInt(fp, nLastTileFound);

        const uint32_t nVRCmaxOverviews = 7;
        if (nOverviewCount != nVRCmaxOverviews)
        {
            CPLDebug("Viewranger",
                     "VRCBuildTileIndex(0x%08x) tile %u 0x%08x: expected "
                     "OverviewIndex with %u entries - got %d",
                     nVRCmaxOverviews, nTileIndexStart, nTileFound,
                     nLastTileFound, nOverviewCount);
            // VSIFree(anNewTileIndex);
            // return nullptr;
            break;
        }
        unsigned int anOverviewIndex[nVRCmaxOverviews] = {};
        for (const unsigned int &i : anOverviewIndex)
        {
            anOverviewIndex[i] = VRReadUInt(fp);
        }
        int nLastOI = nOverviewCount;
        int nFound = 0;
        while (nLastOI > 0)
        {
            nLastOI--;
            if (anOverviewIndex[nLastOI])
            {
                const unsigned int x = VRReadUInt(fp, anOverviewIndex[nLastOI]);
                const unsigned int y = VRReadUInt(fp);
                anNewTileIndex[nGdalTile] = VRReadUInt(
                    fp,
                    anOverviewIndex[nLastOI] +
                        ((2 + 2      // tile count and size
                          + (x * y)  // ignore x by y matrix
                          // and read the "pointer to end of last tile"
                          ) *
                         sizeof(unsigned int)));
                nLastTileFound = anNewTileIndex[nGdalTile];
                CPLDebug("Viewranger", "\tanNewTileIndex[%u] = 0x%08x=%u",
                         nGdalTile, nLastTileFound, nLastTileFound);
                nFound = 1;
                break;
            }
        }
        if (nFound == 0)
        {
            CPLDebug("Viewranger", "\tnGdalTile %u nTileFound %u not found",
                     nGdalTile, nTileFound);
        }
        nTileFound++;
    }

    for (unsigned int y = 0; y < tileYcount; y++)
    {
        for (unsigned int x = 0; x < tileXcount; x++)
        {
            CPLDebug("Viewranger", "anFirstTileIndex[%u,%u] = 0x%08x", x, y,
                     anFirstTileIndex[x + (y * tileXcount)]);
        }
    }
    for (unsigned int y = 0; y < tileYcount; y++)
    {
        for (unsigned int x = 0; x < tileXcount; x++)
        {
            CPLDebug("Viewranger", "anNewTileIndex[%u,%u] = 0x%08x", x, y,
                     anNewTileIndex[x + (y * tileXcount)]);
        }
    }

    VSIFree(anFirstTileIndex);

    return anNewTileIndex;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *VRCDataset::Open(GDALOpenInfo *poOpenInfo)
{
    CPLDebug("Viewranger", "VRCDataset::Open( %p )", poOpenInfo);

    if (poOpenInfo == nullptr || !Identify(poOpenInfo))
        return nullptr;

    /* -------------------------------------------------------------------- */
    /*      Confirm the requested access is supported.                      */
    /* -------------------------------------------------------------------- */
    if (poOpenInfo->eAccess == GA_Update)
    {
        ReportUpdateNotSupportedByDriver("VRC");
        return nullptr;
    }

    if (poOpenInfo->pszFilename == nullptr ||
        *poOpenInfo->pszFilename == '\000')
    {
        CPLError(CE_Warning, CPLE_NotSupported,
                 "VRC driver asked to open a file with no name");
        return nullptr;
    }

    /* Check that the file pointer from GDALOpenInfo* is available */
    if (poOpenInfo->fpL == nullptr)
    {
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Create a corresponding GDALDataset.                             */
    /* -------------------------------------------------------------------- */
    // Evan Rouault suggests std::unique_ptr here:
    // https://github.com/OSGeo/gdal/pull/4092
    // std::unique_ptr<VRCDataset> poDS = std::make_unique<VRCDataset>();  //
    // make_unique requires C++14 std::unique_ptr<VRCDataset> poDS =
    // std::unique_ptr<VRCDataset>(new VRCDataset());  // -Wreturn-stack-address
    // auto poDS = std::unique_ptr<VRCDataset>();
    auto *poDS = new VRCDataset();
    if (poDS == nullptr)  //-V668
    {
        return nullptr;
    }

    /* Borrow the file pointer from GDALOpenInfo* */
    poDS->fp = poOpenInfo->fpL;
    poOpenInfo->fpL = nullptr;

#if HAS_SAFE311
    poDS->sFileName = CPLGetBasenameSafe(poOpenInfo->pszFilename);
#else
    poDS->sFileName = CPLGetBasename(poOpenInfo->pszFilename);
#endif

    /* -------------------------------------------------------------------- */
    /*      Read the header.                                                */
    /* -------------------------------------------------------------------- */
    VSIFReadL(poDS->abyHeader, 1, sizeof(poDS->abyHeader), poDS->fp);

    poDS->nMagic = VRGetUInt(poOpenInfo->pabyHeader, 0);

    poDS->nCountry = VRGetShort(poDS->abyHeader, 6);
    const char *szInCharset = CharsetFromCountry(poDS->nCountry);

    CPLDebug("ViewRanger", "Country %hd has charset %s", poDS->nCountry,
             szInCharset);

    poDS->nMapID = VRGetInt(poDS->abyHeader, 14);
    if (poDS->nMapID != -10 && poDS->nMapID != 0     // overviews and some demos
        && poDS->nMapID != 8                         // pay-by-tile
        && poDS->nMapID != 16 && poDS->nMapID != 22  // Finland1M.VRC
        && poDS->nMapID != 255                       // Valle Antrona.VRC
        && poDS->nMapID != 293   // SouthTyrol50k/SouthTyro50k.VRC
        && poDS->nMapID != 294   // TrentinoGarda50k.VRC
        && poDS->nMapID != 588   // Danmark50k-*.VRC
        && poDS->nMapID != 618   // Corfu (Tour & Trail)
        && poDS->nMapID != 3038  // 4LAND200AlpSouth
        && poDS->nMapID != 3334  // Zakynthos.VRC
    )
    {
        CPLError(CE_Warning, CPLE_NotSupported,
                 "VRC file %s unexpected Map ID %d", poOpenInfo->pszFilename,
                 poDS->nMapID);
    }

    {
        constexpr size_t VRCpszMapIDlen = 11;
        char pszMapID[VRCpszMapIDlen] = "";
        const int ret =
            CPLsnprintf(pszMapID, VRCpszMapIDlen, "%d", poDS->nMapID);
        if (ret == VRCpszMapIDlen - 1)
        {
            poDS->SetMetadataItem("VRC ViewRanger MapID", pszMapID, "");
        }
        else
        {
            CPLDebug("Viewranger",
                     "Could not set MapID Metadata - CPLsnprintf( , "
                     "VRCpszMapIDlen, %d) returned %d - expected %" PRI_SIZET,
                     poDS->nMapID, ret, VRCpszMapIDlen - 1);
        }
    }

    unsigned int nStringCount = VRGetUInt(poDS->abyHeader, 18);
    unsigned int nNextString = 22;
    if (nStringCount == 0 && poDS->nMapID == 8)
    {
        // seems to be needed for pay-by-tile files
        nStringCount = VRGetUInt(poDS->abyHeader, 22);
        nNextString += 4;
    }
    CPLDebug("Viewranger", "VRC Map ID %d with %u strings", poDS->nMapID,
             nStringCount);

    char **paszStrings = nullptr;
    if (nStringCount > 0)
    {
        paszStrings =
            static_cast<char **>(VSIMalloc2(nStringCount, sizeof(char *)));
        if (paszStrings == nullptr)
        {
            CPLError(CE_Failure, CPLE_OutOfMemory,
                     "Cannot allocate memory for array strings");
            delete poDS;
            return nullptr;
        }

        const char *const szOutCharset = "UTF-8";

        for (unsigned int ii = 0; ii < nStringCount; ++ii)
        {
            paszStrings[ii] = VRCGetString(poDS->fp, nNextString);
            // Need to check that this is within abyHeader ... or within the
            // file ? werdna Sept 2021
            nNextString += 4 + VRGetUInt(poDS->abyHeader, nNextString);
            CPLDebug("Viewranger", "string %u %s", ii, paszStrings[ii]);

            if (paszStrings[ii] != nullptr && *paszStrings[ii])
            {
                // Save the string as a MetadataItem.
                const int VRCpszTAGlen = 18;
                char pszTag[VRCpszTAGlen + 1] = "";
                const int ret =
                    CPLsnprintf(pszTag, VRCpszTAGlen, "String%u", ii);
                pszTag[VRCpszTAGlen] = '\000';
                if (VRCpszTAGlen >= ret && ret > 0)
                {
                    // CPLRecode() may call CPLError
                    // Do we wish to override the error handling ?
                    // CPLErrorReset();
                    // CPLPushErrorHandler(CPLQuietErrorHandler);

                    char *pszTmpName =
                        CPLRecode(paszStrings[ii], szInCharset, szOutCharset);
                    poDS->SetMetadataItem(pszTag, pszTmpName);
                    CPLFree(pszTmpName);
                    // CPLPopErrorHandler();
                }
                else
                {
                    CPLDebug("Viewranger",
                             "Could not set String%u Metadata - "
                             "CPLsnprintf(..., VRCpszTAGlen %s) returned %d",
                             ii, paszStrings[ii], ret);
                }
            }

            // CPLRecode() may call CPLError
            // Do we wish to override the error handling ?
            // CPLErrorReset();
            // CPLPushErrorHandler(CPLQuietErrorHandler);

            poDS->sLongTitle =
                CPLRecode(paszStrings[0], szInCharset, szOutCharset);
            poDS->SetMetadataItem("TIFFTAG_IMAGEDESCRIPTION",
                                  poDS->sLongTitle.c_str(), "");
            // CPLPopErrorHandler();
        }

        if (nStringCount > 1)
        {
            // CPLRecode() may call CPLError
            // Do we wish to override the error handling ?
            // CPLErrorReset();
            // CPLPushErrorHandler(CPLQuietErrorHandler);

            poDS->sCopyright =
                CPLRecode(paszStrings[1], szInCharset, szOutCharset);
            poDS->SetMetadataItem("TIFFTAG_COPYRIGHT", poDS->sCopyright.c_str(),
                                  "");
            // CPLPopErrorHandler();

            // This is Digital Right Management (DRM), but not encryption.
            // Explicitly put the file's DeviceID into the metadata so that
            // it can be preserved if the data is saved in another format.
            // We are *not* filing off the serial numbers.
            if (nStringCount > 5 && *paszStrings[5])
            {
                poDS->SetMetadataItem("VRC ViewRanger Device ID",
                                      CPLString(paszStrings[5]).c_str(), "");
            }
        }
    }

    poDS->nLeft = VRGetInt(poDS->abyHeader, nNextString);
    poDS->nTop = VRGetInt(poDS->abyHeader, nNextString + 4);
    poDS->nRight = VRGetInt(poDS->abyHeader, nNextString + 8);
    poDS->nBottom = VRGetInt(poDS->abyHeader, nNextString + 12);
    poDS->nScale = VRGetUInt(poDS->abyHeader, nNextString + 16);
    if (poDS->nScale == 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Cannot locate a VRC map with zero scale");
        delete poDS;
        return nullptr;
    }

    // based on 10 pixels/millimetre (254 dpi)
    poDS->dfPixelMetres = poDS->nScale / 10000.0;
    if (static_cast<unsigned long>(lround(10000.0 * poDS->dfPixelMetres)) !=
        poDS->nScale)
    {
        CPLDebug("Viewranger", "VRC %f metre pixels is not exactly 1:%u",
                 poDS->dfPixelMetres, poDS->nScale);
    }

    if (poDS->dfPixelMetres < 0.5)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Map with %g metre pixels is too large scale (detailed) for "
                 "the current VRC driver",
                 poDS->dfPixelMetres);
        delete poDS;
        return nullptr;
    }

    {  // Block to calculate size of raster
        const double dfRasterXSize =
            ((10000.0) * (poDS->nRight - poDS->nLeft)) / poDS->nScale;
        poDS->nRasterXSize = static_cast<int>(dfRasterXSize);
        const double dfRasterYSize =
            ((10000.0) * (poDS->nTop - poDS->nBottom)) / poDS->nScale;
        poDS->nRasterYSize = static_cast<int>(dfRasterYSize);

        // cast to double to avoid overflow and loss of precision
        // eg  (10000*503316480)/327680000 = 15360
        //             but                 = 11 with 32bit ints.
        //
        // ... but could still overflow when casting from df... to n... FixMe

        CPLDebug("Viewranger", "%d=%f x %d=%f pixels", poDS->nRasterXSize,
                 dfRasterXSize, poDS->nRasterYSize, dfRasterYSize);

        if (dfRasterXSize >= INT_MAX || dfRasterYSize >= INT_MAX)
        {
            CPLError(CE_Failure, CPLE_NotSupported,
                     "Invalid dimensions : %f x %f", dfRasterXSize,
                     dfRasterYSize);
            GDALClose(poDS);
            // poDS = nullptr; // Was I being paranoid ?
            return nullptr;
        }
        if (poDS->nRasterXSize <= 0 || poDS->nRasterYSize <= 0)
        {
            CPLError(CE_Failure, CPLE_NotSupported,
                     "Invalid dimensions : %d x %d", poDS->nRasterXSize,
                     poDS->nRasterYSize);
            GDALClose(poDS);
            // poDS = nullptr; // Was I being paranoid ?
            return nullptr;
        }
    }

    {  // Unnamed block two
        poDS->tileSizeMax = VRGetUInt(poDS->abyHeader, nNextString + 20);
        poDS->tileSizeMin = VRGetUInt(poDS->abyHeader, nNextString + 24);
        if (poDS->tileSizeMax == 0)
        {
            CPLError(CE_Failure, CPLE_NotSupported,
                     "tileSizeMax is zero and invalid");
            GDALClose(poDS);
            // poDS = nullptr; // Was I being paranoid ?
            return nullptr;
        }
        if (poDS->tileSizeMin == 0)
        {
            poDS->tileSizeMin = poDS->tileSizeMax;
            CPLDebug("Viewranger", "tileSizeMin is zero. Using tileSizeMax %u",
                     poDS->tileSizeMax);
        }

        // seven is not really used yet
        const unsigned int seven = VRGetUInt(poDS->abyHeader, nNextString + 28);
        if (seven != 7)
        {
            CPLDebug("Viewranger", "expected seven; got %u", seven);
        }

        // I don't really know what chksum is but am curious about the value
        const unsigned int chksum =
            VRGetUInt(poDS->abyHeader, nNextString + 32);
        // Record it in the metadata (TIFF tags or similar) in case it is
        // important.
        const unsigned int VRCpszSumLen = 11;
        char pszChkSum[VRCpszSumLen] = "";
        const int ret = CPLsnprintf(pszChkSum, VRCpszSumLen, "0x%08x", chksum);
        pszChkSum[VRCpszSumLen - 1] = '\000';
        if (ret == VRCpszSumLen - 1)
        {
            poDS->SetMetadataItem("VRCchecksum", pszChkSum, "");
        }
        else
        {
            CPLDebug("Viewranger", "Could not set VRCchecksum to 0x%08x",
                     chksum);
        }

        poDS->tileXcount = VRGetUInt(poDS->abyHeader, nNextString + 36);
        poDS->tileYcount = VRGetUInt(poDS->abyHeader, nNextString + 40);
        const long long nTileXYcount =
            static_cast<long long>(poDS->tileXcount) *
            static_cast<long long>(poDS->tileYcount);
        if (nTileXYcount > INT_MAX)
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Too many tiles: %u x %u",
                     poDS->tileXcount, poDS->tileYcount);
            return nullptr;
        }

        CPLDebug("Viewranger", "tileSizeMax %u\ttileSizeMin %u",
                 poDS->tileSizeMax, poDS->tileSizeMin);
        CPLDebug("Viewranger", "chksum 0x%08x", chksum);
        CPLDebug("Viewranger", "tile count %u x %u", poDS->tileXcount,
                 poDS->tileYcount);

        // Sets        VSIStatBufL oStatBufL;
        // Find out how big the file is.
        // Used in VRCGetTileIndex to recognize noData values
        // and several other places.
        if (VSIStatL(poOpenInfo->pszFilename, &poDS->oStatBufL))
        {
            CPLError(CE_Failure, CPLE_AppDefined, "cannot stat file %s\n",
                     poOpenInfo->pszFilename);
            return nullptr;
        }

        const unsigned int nTileIndexAddr = nNextString + 44;

        if (nTileIndexAddr >= poDS->oStatBufL.st_size)
        {
            CPLDebug("Viewranger",
                     "Tile index %u=0x%08x points outside the file. Ignored\n",
                     nTileIndexAddr, nTileIndexAddr);
        }
        else
        {

            if (poDS->nMapID != 8)
            {
                // Read the index of tile addresses

                poDS->anTileIndex = poDS->VRCGetTileIndex(nTileIndexAddr);
                if (poDS->anTileIndex == nullptr)
                {
                    CPLDebug("Viewranger", "VRCGetTileIndex(%u=0x%08x) failed",
                             nTileIndexAddr, nTileIndexAddr);
                }
            }
            else  // So poDS->nMapID == 8 - Pay-by-tile
            {
                // Pay-by-tile files have two (maybe even three?) tile indexes.

                // Report but otherwise ignore the index at nTileIndexAddr
                if (VSIFSeekL(poDS->fp, nTileIndexAddr, SEEK_SET))
                {
                    CPLError(CE_Failure, CPLE_AppDefined,
                             "cannot seek to nTileIndexAddr %u=x%08x",
                             nTileIndexAddr, nTileIndexAddr);
                    return nullptr;
                }
                CPLDebug(
                    "Viewranger",
                    "Pay-by-tile: skipping %u x %u values after tile count:",
                    poDS->tileXcount, poDS->tileYcount);
                for (unsigned int ii = 0; ii < poDS->tileXcount; ii++)
                {
                    for (unsigned int jj = 0; jj < poDS->tileYcount; jj++)
                    {
                        const unsigned int nValue = VRReadUInt(poDS->fp);
                        CPLDebug("Viewranger", "\t(%u,%u) = 0x%08x=%u", ii, jj,
                                 nValue, nValue);
                        (void)
                            nValue;  // CPLDebug doesn't count as "using" a variable
                    }
                }
            }
        }

        // Verify 07 00 00 00 01 00 01 00 01 00 01
        const unsigned int nSecondSevenPtr =
            nTileIndexAddr + (4 * poDS->tileXcount * poDS->tileYcount);

        if (VSIFSeekL(poDS->fp, nSecondSevenPtr, SEEK_SET))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "cannot seek to nSecondSevenPtr %u=x%08x", nSecondSevenPtr,
                     nSecondSevenPtr);
            return nullptr;
        }

        const unsigned int nCornerPtr = nSecondSevenPtr + 11;
        // ... +11 skips over 07 00 00 00 01 00 01 00 01 00 01
        if (VSIFSeekL(poDS->fp, nCornerPtr, SEEK_SET))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "cannot seek to VRC tile corners");
            return nullptr;
        }

        // Tile corners here
        signed int anCorners[4] = {};
        anCorners[0] = VRReadInt(poDS->fp);
        anCorners[1] = VRReadInt(poDS->fp);
        anCorners[2] = VRReadInt(poDS->fp);
        anCorners[3] = VRReadInt(poDS->fp);
        CPLDebug("Viewranger", "x%08x LTRB (outer) %d %d %d %d", nCornerPtr,
                 poDS->nLeft, poDS->nTop, poDS->nRight, poDS->nBottom);
        CPLDebug("Viewranger", "x%08x LTRB (inner) %d %d %d %d", nCornerPtr,
                 anCorners[0], anCorners[3], anCorners[2], anCorners[1]);

        if (poDS->nTop != anCorners[3])
        {
            CPLDebug("Viewranger", "mismatch original Top %d %d", poDS->nTop,
                     anCorners[3]);
        }

        //   We have some short (underheight) tiles.
        // GDAL expects these at the top of the bottom tile,
        // but VRC puts these at the bottom of the top tile.
        //   We need to add a blank strip at the top of the
        // file up to compensate.
        const double dfHeightPix =
            (poDS->nTop - poDS->nBottom) / poDS->dfPixelMetres;

        int nFullHeightPix = 0;
        if (poDS->tileSizeMax < 1)
        {
            CPLError(CE_Failure, CPLE_NotSupported,
                     "tileSizeMax has changed to zero and is now invalid");
            GDALClose(poDS);
            // poDS = nullptr; // Was I being paranoid ?
            return nullptr;
        }
        nFullHeightPix =
            static_cast<signed int>(poDS->tileSizeMax) *
            static_cast<signed int>(dfHeightPix / poDS->tileSizeMax);

        if ((poDS->nTop - poDS->nBottom) != (anCorners[3] - anCorners[1]) ||
            static_cast<signed long long>(poDS->nTop - poDS->nBottom) !=
                static_cast<signed long long>(poDS->nRasterYSize *
                                              poDS->dfPixelMetres))
        {
            // Equivalent to
            // if (dfHeightPix!=dfheight2 || dfHeightPix!=poDS->nRasterYSize)
            // {
            // but without the division and floating-point equality test.

            // Appease cppcheck.
            // It ignores CPLDebug then deduces that dfheight2 is not used.
            // double dfheight2 =
            //    (anCorners[3]-anCorners[1]) / poDS->dfPixelMetres;
            CPLDebug("Viewranger", "height either %d %g or %g pixels",
                     poDS->nRasterYSize, dfHeightPix,
                     (anCorners[3] - anCorners[1]) /
                         poDS->dfPixelMetres  // dfheight2
            );
        }

        if (nFullHeightPix < dfHeightPix)
        {
            nFullHeightPix += poDS->tileSizeMax;
            const int nNewTop =
                poDS->nBottom +
                static_cast<signed int>(nFullHeightPix * poDS->dfPixelMetres);
            poDS->nTopSkipPix =
                nFullHeightPix - static_cast<signed int>(dfHeightPix);
            CPLDebug("Viewranger",
                     "Adding %d pixels at top edge - from %d to %d - "
                     "height was %d now %d",
                     poDS->nTopSkipPix, poDS->nTop, nNewTop, poDS->nRasterYSize,
                     nFullHeightPix);
            poDS->nTop = nNewTop;
            if (poDS->nTop != anCorners[3])
            {
                CPLDebug("Viewranger", "mismatch new Top %d %d", poDS->nTop,
                         anCorners[3]);
            }
            poDS->nRasterYSize = nFullHeightPix;
        }

        if (poDS->nLeft != anCorners[0])
        {
            CPLDebug("Viewranger", "Unexpected mismatch Left %d %d",
                     poDS->nLeft, anCorners[0]);
        }
        if (poDS->nBottom != anCorners[1])
        {
            CPLDebug("Viewranger", "Unexpected mismatch Bottom %d %d",
                     poDS->nBottom, anCorners[1]);
        }
        if (poDS->nRight != anCorners[2])
        {
            //   Unlike the top edge, GDAL and VRC agree that
            // narrow tiles are at the left edge of the right-most tile.
            //   We don't need to adjust anything for this case...
            CPLDebug("Viewranger", "mismatch Right %d %d", poDS->nRight,
                     anCorners[2]);
        }

        const unsigned int nTileIndexStart =
            nCornerPtr + 16;  // Skip the corners
        const unsigned int nTileIndexSize = VRReadUInt(poDS->fp);

        CPLDebug("Viewranger", "nTileIndexAddr %u=x%08x\n", nTileIndexAddr,
                 nTileIndexAddr);
        if (nTileIndexSize == 7)
        {
            // CPLDebug does not support m$ in format strings
            CPLDebug("Viewranger",
                     "nTileIndexStart %u=x%08x points to seven as expected",
                     nTileIndexStart, nTileIndexStart);
        }
        else
        {
            CPLDebug("Viewranger",
                     "nTileIndexStart %u=x%08x points to %08x is not seven",
                     nTileIndexStart, nTileIndexStart, nTileIndexSize);
        }

        if (poDS->nMapID == 8)
        {
            // Read the index of tile addresses
            if (poDS->anTileIndex == nullptr)
            {
                poDS->anTileIndex =
                    poDS->VRCBuildTileIndex(nTileIndexAddr, nTileIndexStart);
                if (poDS->anTileIndex == nullptr)
                {
                    return nullptr;
                }
            }
        }

        if (poDS->nMagic == vrc_magic)
        {
            // nRasterXSize,nRasterYSize are fine
            // (perhaps except for short tiles ?)
            // but we need to get tileSizeMax/Min and/or tile[XY]count
            // into the band
        }
        else if (poDS->nMagic == vrc_magic36)
        {
            // VRC36_PIXEL_IS_PIXEL
            // this will be the default
            // nRasterXSize,nRasterYSize are fine
            // but we need to get tileSizeMax/Min and/or tile[XY]count
            // into the band
            CPLDebug("Viewranger", "each pixel represents a 36-based pixel");
        }
        else
        {
            CPLDebug("Viewranger", "nMagic x%08x unknown", poDS->nMagic);
        }
    }

    /********************************************************************/
    /*                              Set CRS                             */
    /********************************************************************/
    if (!poDS->poSRS)
    {
        const char *szCountry = nullptr;
        if (nStringCount > 8 && paszStrings && paszStrings[8])
        {
            szCountry = paszStrings[8];
        }
        poDS->poSRS = CRSfromCountry(poDS->nCountry, poDS->nMapID, szCountry);
    }

    for (unsigned int ii = 0; ii < nStringCount; ++ii)
    {
        if (paszStrings[ii])
        {
            VSIFree(paszStrings[ii]);
            paszStrings[ii] = nullptr;
        }
    }
    VSIFree(paszStrings);

    /********************************************************************/
    /*             Report some strings found in the file                */
    /********************************************************************/
    CPLDebug("Viewranger", "Filename: %s", poDS->sFileName.c_str());
    CPLDebug("Viewranger", "Long Title: %s", poDS->sLongTitle.c_str());
    CPLDebug("Viewranger", "Copyright: %s", poDS->sCopyright.c_str());
    CPLDebug("Viewranger", "%g metre pixels", poDS->dfPixelMetres);

    // --------------------------------------------------------------------
    //
    //      Create band information objects.
    //
    // --------------------------------------------------------------------

    // Until we support overviews, large files are very slow.
    // This environment variable allows users to skip them.
    int fSlowFile = FALSE;
    const char *szVRCmaxSize = CPLGetConfigOption("VRC_MAX_SIZE", "");
    if (szVRCmaxSize != nullptr && *szVRCmaxSize != 0)
    {
        const long long nMaxSize = strtoll(szVRCmaxSize, nullptr, 10);
        // Should support KMGTP... suffixes.
        if (nMaxSize > poDS->oStatBufL.st_size)
        {
            fSlowFile = TRUE;
        }
    }
    if (!fSlowFile)
    {
        constexpr int nMyBandCount = 4;
        for (int i = 1; i <= nMyBandCount; i++)
        {
            auto *poBand = new VRCRasterBand(poDS, i, -1, 6, nullptr);
            poDS->SetBand(i, poBand);

            if (i == 4)
            {
                // Alpha band. Do we need to set a no data value ?
                poBand->SetNoDataValue(nVRCNoData);
            }
        }

        // More metadata.
        if (poDS->nBands > 1)
        {
            poDS->SetMetadataItem("INTERLEAVE", "PIXEL", "IMAGE_STRUCTURE");
        }
    }

    poDS->SetDescription(poOpenInfo->pszFilename);

    return (poDS);
}

void dumpPPM(unsigned int width, unsigned int height,
             const unsigned char *const data, unsigned int rowlength,
             CPLString osBaseLabel, VRCinterleave eInterleave,
             unsigned int nMaxPPM)
{
    // is static the best way to count the PPMs ?
    static unsigned int nPPMcount = 0;

    CPLDebug("Viewranger PPM",
             "dumpPPM(%u %u %p %u %s %s-interleaved) count %u", width, height,
             data, rowlength, osBaseLabel.c_str(),
             (eInterleave == pixel) ? "pixel" : "band", nPPMcount);
    if (osBaseLabel == nullptr)
    {
        CPLDebug("Viewranger PPM", "dumpPPM: null osBaseLabel\n");
        return;
    }

    // At least on unix, spaces make filenames harder to work with.
    osBaseLabel.replaceAll(' ', '_');

    if (rowlength == 0)
    {
        rowlength = width;
        CPLDebug("Viewranger PPM",
                 "dumpPPM(... 0 %s) no rowlength, setting to width = %u",
                 osBaseLabel.c_str(), rowlength);
    }

    const CPLString osPPMname =
        CPLString().Printf("%s.%05u.%s", osBaseLabel.c_str(), nPPMcount,
                           (eInterleave == pixel) ? "ppm" : "pgm");
    if (osPPMname == nullptr)
    {
        CPLDebug("Viewranger PPM", "osPPMname truncated %s %u",
                 osBaseLabel.c_str(), nPPMcount);
    }
    char const *pszPPMname = osPPMname.c_str();

    if (nMaxPPM > 10 && nPPMcount > nMaxPPM)
    {
        CPLDebug("Viewranger PPM", "... too many PPM files; skipping  %s",
                 pszPPMname);
        nPPMcount++;
        return;
    }

    CPLDebug("Viewranger PPM", "About to dump PPM file %s", pszPPMname);

    char errstr[256] = "";
    VSILFILE *fpPPM = VSIFOpenL(pszPPMname, "w");
    if (fpPPM == nullptr)
    {
        const int nFileErr = errno;
        VRC_file_strerror_r(nFileErr, errstr, 255);
        CPLDebug("Viewranger PPM", "PPM data dump file %s failed; errno=%d %s",
                 pszPPMname, nFileErr, errstr);
        return;
    }

    const size_t nHeaderBufSize = 40;
    char acHeaderBuf[nHeaderBufSize] = "";
    size_t nHeaderSize = 0;
    switch (eInterleave)
    {
        case pixel:
            nHeaderSize = static_cast<size_t>(
                CPLsnprintf(acHeaderBuf, nHeaderBufSize, "P6\n%u %u\n255\n",
                            width, height));
            break;
        case band:
            nHeaderSize = static_cast<size_t>(
                CPLsnprintf(acHeaderBuf, nHeaderBufSize, "P5\n%u %u\n255\n",
                            width, height));
            break;
    }

    // CPLsnprintf may return negative values;
    // the cast to size_t converts these to large positive
    // values, so we only need one test.
    if (nHeaderSize >= nHeaderBufSize)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "dumpPPM error generating header for %s\n", pszPPMname);
        VSIFCloseL(fpPPM);
        return;
    }

    const size_t nHeaderWriteResult =
        VSIFWriteL(acHeaderBuf, 1, nHeaderSize, fpPPM);
    if (nHeaderSize == nHeaderWriteResult)
    {
        const unsigned char *pRow = data;
        for (unsigned int r = 0; r < height; r++)
        {
            if (eInterleave == pixel)
            {
                if (width != VSIFWriteL(pRow, 3, width, fpPPM))
                {
                    const int nWriteErr = errno;
                    VRC_file_strerror_r(nWriteErr, errstr, 255);
                    CPLError(CE_Failure, CPLE_AppDefined,
                             "dumpPPM error writing %s row %u errno=%d %s\n",
                             pszPPMname, r, nWriteErr, errstr);
                    break;
                }

                pRow += 3 * static_cast<size_t>(rowlength);
            }
            else  // must be band interleaved
            {

                const size_t rowwriteresult = VSIFWriteL(pRow, 1, width, fpPPM);
                if (width != rowwriteresult)
                {
                    const int nWriteErr = errno;
                    VRC_file_strerror_r(nWriteErr, errstr, 255);
                    CPLError(CE_Failure, CPLE_AppDefined,
                             "dumpPPM error writing %s row %u: errno=%d %s",
                             pszPPMname, r, nWriteErr, errstr);
                    break;
                }
                pRow += rowlength;
            }
        }
    }
    else
    {  // nHeaderSize!=nHeaderWriteResult
        const int nWriteErr = errno;
        VRC_file_strerror_r(nWriteErr, errstr, 255);
        // CPLError(CE_Failure, CPLE_AppDefined,
        CPLDebug("Viewranger PPM",
                 "dumpPPM error writing header for %s errno=%d %s", pszPPMname,
                 nWriteErr, errstr);
    }

    if (0 != VSIFCloseL(fpPPM))
    {
        CPLDebug("Viewranger PPM",
                 "Failed to close PPM data dump file %s; errno=%d", pszPPMname,
                 errno);
    }

    nPPMcount++;

    // return;
}

static void dumpWLD(char const *pszWLDname, const CPLString osWLDparams)
{

    char pszErrStr[256] = "";
    VSILFILE *fpWLD = VSIFOpenL(pszWLDname, "w");
    if (fpWLD == nullptr)
    {
        const int nFileErr = errno;
        VRC_file_strerror_r(nFileErr, pszErrStr, 255);
        CPLDebug("Viewranger PNG", "WLD data dump file %s failed; errno=%d %s",
                 pszWLDname, nFileErr, pszErrStr);
    }
    else
    {
        const size_t nWriteResult =
            VSIFWriteL(osWLDparams.c_str(), 1, osWLDparams.size(), fpWLD);
        if (osWLDparams.size() != nWriteResult)
        {
            const int nFileErr = errno;
            VRC_file_strerror_r(nFileErr, pszErrStr, 255);
            return;
        }
        if (0 != VSIFCloseL(fpWLD))
        {
            const int nFileErr = errno;
            VRC_file_strerror_r(nFileErr, pszErrStr, 255);
            CPLDebug("Viewranger PNG",
                     "Failed to close WLD data dump file %s; errno=%d %s",
                     pszWLDname, nFileErr, pszErrStr);
        }
        else
        {
            CPLDebug("Viewranger PNG", "WLD data dumped to file %s",
                     pszWLDname);
        }
    }
}

static void dumpPNG(
    const unsigned char *const data,  // pre-prepared PNG data, *not* raw image.
    int nDataLen, CPLString osBaseLabel, CPLString osWLDparams,
    unsigned int nMaxPNG)
{
    // Is static the best way to count the PNGs ?
    static unsigned int nPNGcount = 0;

    CPLDebug("Viewranger PNG", "dumpPNG(%p %d %s\n%s) count %u", data, nDataLen,
             osBaseLabel.c_str(), osWLDparams.c_str(), nPNGcount);
    if (osBaseLabel == nullptr)
    {
        CPLDebug("Viewranger PNG", "dumpPNG: null osBaseLabel\n");
        return;
    }

    // At least on unix, spaces make filenames harder to work with.
    osBaseLabel.replaceAll(' ', '_');

    const CPLString osPNGname =
        CPLString().Printf("%s.%05u.png", osBaseLabel.c_str(), nPNGcount);
    if (osPNGname == nullptr)
    {
        CPLDebug("Viewranger PNG", "osPNGname truncated %s %u",
                 osBaseLabel.c_str(), nPNGcount);
    }
    char const *pszPNGname = osPNGname.c_str();

    const CPLString osWLDname =
        CPLString().Printf("%s.%05u.wld", osBaseLabel.c_str(), nPNGcount);
    if (osWLDname == nullptr)
    {
        CPLDebug("Viewranger PNG", "osWLDname truncated %s %u",
                 osBaseLabel.c_str(), nPNGcount);
    }
    char const *pszWLDname = osWLDname.c_str();

    if (nPNGcount > 10 && nPNGcount > nMaxPNG)
    {
        CPLDebug("Viewranger PNG", "... too many PNG files; skipping %s",
                 pszPNGname);
        nPNGcount++;
        return;
    }

    CPLDebug("Viewranger PNG", "About to dump PNG file %s", pszPNGname);

    char pszErrStr[256] = "";
    VSILFILE *fpPNG = VSIFOpenL(pszPNGname, "w");
    if (fpPNG == nullptr)
    {
        const int nFileErr = errno;
        VRC_file_strerror_r(nFileErr, pszErrStr, 255);
        CPLDebug("Viewranger PNG", "PNG data dump file %s failed; errno=%d %s",
                 pszPNGname, nFileErr, pszErrStr);
    }
    else
    {
        const size_t nWriteResult =
            VSIFWriteL(data, 1, static_cast<size_t>(nDataLen), fpPNG);
        if (static_cast<size_t>(nDataLen) != nWriteResult)
        {
            const int nFileErr = errno;
            VRC_file_strerror_r(nFileErr, pszErrStr, 255);
            return;
        }
        if (0 != VSIFCloseL(fpPNG))
        {
            const int nFileErr = errno;
            VRC_file_strerror_r(nFileErr, pszErrStr, 255);
            CPLDebug("Viewranger PNG",
                     "Failed to close PNG data dump file %s; errno=%d %s",
                     pszPNGname, nFileErr, pszErrStr);
        }
        else
        {
            CPLDebug("Viewranger PNG", "PNG data\n%sdumped to file %s",
                     osWLDparams.c_str(), pszPNGname);
            dumpWLD(pszWLDname, osWLDparams);
        }
    }

    nPNGcount++;
}

png_byte *
VRCRasterBand::read_PNG(VSILFILE *fp,
                        // void *pImage,
                        unsigned int *pPNGwidth, unsigned int *pPNGheight,
                        unsigned int nVRCHeader, vsi_l_offset nPalette,
                        unsigned int nVRCDataLen,
                        // int nPNGXcount, int nPNGYcount,
                        int nGDtile_xx, int nGDtile_yy, unsigned int nVRtile_xx,
                        unsigned int nVRtile_yy)
{
    const unsigned int nVRCData = nVRCHeader + 0x12;

    if (fp == nullptr)
    {
        CPLDebug("Viewranger PNG", "read_PNG given null file pointer");
        return nullptr;
    }
    if (pPNGwidth == nullptr || pPNGheight == nullptr)
    {
        CPLDebug("Viewranger PNG", "read_PNG needs space to return image size");
        return nullptr;
    }

    if (nVRCHeader == 0)
    {
        CPLDebug("Viewranger PNG",
                 "block (%d,%d) tile (%u,%u) nVRCHeader is nullptr", nGDtile_xx,
                 nGDtile_yy, nVRtile_xx, nVRtile_yy);
        return nullptr;
    }
    if (nVRCDataLen < 12)
    {
        CPLDebug("Viewranger PNG",
                 "block (%d,%d) tile (%u,%u) nVRCData is too small %u < 12",
                 nGDtile_xx, nGDtile_yy, nVRtile_xx, nVRtile_yy, nVRCDataLen);
        return nullptr;
    }
    if (nVRCDataLen >= static_cast<VRCDataset *>(poDS)->oStatBufL.st_size)
    {
        return nullptr;
    }

    png_voidp user_error_ptr = nullptr;
    // Initialize PNG structures
    png_structp png_ptr =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, user_error_ptr,
                               // user_error_fn, user_warning_fn
                               nullptr, nullptr);
    if (png_ptr == nullptr)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRCRasterBand::read_PNG png_create_read_struct error %p\n",
                 user_error_ptr);
        return nullptr;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr)
    {
        png_destroy_read_struct(&png_ptr, static_cast<png_infopp>(nullptr),
                                static_cast<png_infopp>(nullptr));
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRCRasterBand::read_PNG png_create_info_struct error %p\n",
                 user_error_ptr);
        return nullptr;
    }

    png_infop end_info = png_create_info_struct(png_ptr);
    if (!end_info)
    {
        png_destroy_read_struct(&png_ptr, &info_ptr,
                                static_cast<png_infopp>(nullptr));
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRCRasterBand::read_PNG end_info png_create_info_struct "
                 "error %p\n",
                 user_error_ptr);
        return nullptr;
    }

    // ********************************************************************
    //
    // This is where we create the PNG file from the VRC data.
    //
    // I wish I could find a *simple* way to avoid reading it in to memory
    // only to pass it to the iterator VRC_png_read_data_fn.
    //
    // ********************************************************************
    // clang-format off
    const std::array<const unsigned char, 8> PNG_sig =
        {0x89, 'P',  'N',  'G',  0x0d, 0x0a, 0x1a, 0x0a};
    const std::array<const unsigned char, 8> IHDR_head =
        {0x00, 0x00, 0x00, 0x0d,  'I',  'H',  'D',  'R'};
    const std::array<const unsigned char, 12> IEND_chunk =
        {0x00, 0x00, 0x00, 0x00,  'I',  'E',  'N',  'D',
         0xae, 0x42, 0x60, 0x82};
    // clang-format on

    VRCpng_callback_t VRCpng_callback = {0UL, std::vector<png_byte>{}};

    VRCpng_callback.vData.reserve(
        sizeof(PNG_sig) + sizeof(IHDR_head) + 13 + 4 +
        (3L * 256) +   // enough for 256x3 entry palette
        (3L * 4) +     //  length, "PLTE" and checksum
        nVRCDataLen +  // IDAT chunks
        sizeof(IEND_chunk));
    vector_append(VRCpng_callback.vData, PNG_sig);

    // IHDR starts here
    vector_append(VRCpng_callback.vData, IHDR_head);

    // IHDR_data here

    if (VSIFSeekL(fp, nVRCHeader, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "cannot seek to nVRCHeader %u=x%08x", nVRCHeader, nVRCHeader);
        return nullptr;
    }
    if (const auto n = static_cast<unsigned int>(VRReadChar(fp)))
    {
        (void)n;  // Appease static analysers
        CPLDebug("Viewranger PNG",
                 "%u=x%08x: First PNG header byte is x%02x - expected x00",
                 nVRCHeader, nVRCHeader, n);
    }
    else
    {
        CPLDebug("Viewranger PNG",
                 "%u=x%08x: First PNG header byte is x00 as expected",
                 nVRCHeader, nVRCHeader);
    }
    std::array<char, 17> aVRCHeader = {};
    const size_t count = VSIFReadL(&aVRCHeader, 1, 17, fp);
    if (17 > count)
    {
        CPLError(CE_Failure, CPLE_FileIO,
                 "only read %d of 17 bytes for PNG header\n",
                 static_cast<int>(count));
        return nullptr;
    }
    vector_append(VRCpng_callback.vData, aVRCHeader);

    const unsigned int nPNGwidth = PNGGetUInt(&aVRCHeader, 0);
    *pPNGwidth = nPNGwidth;
    const unsigned int nPNGheight = PNGGetUInt(&aVRCHeader, 4);
    *pPNGheight = nPNGheight;

    if (nPNGwidth == 0 || nPNGheight == 0)
    {
        CPLDebug("Viewranger PNG", "empty PNG tile %u x %d (VRC tile %u,%u)",
                 nPNGwidth, nRasterXSize, nVRtile_xx, nVRtile_yy);
        return nullptr;
    }

#if defined UseCountFull
    double dfPNGYcountFull = nBlockYSize / nPNGheight;
    int nPNGYcountFull = (int)(dfPNGYcountFull + .5);
    if (nPNGheight * nPNGYcount == nBlockYSize)
    {  // nRasterYSize ? - nBlockYSize probably OK
        CPLDebug("Viewranger PNG",
                 "PNG height: %d * PNG count %d == block height %d - G=%d V=%d",
                 nPNGheight, nPNGYcount, nBlockYSize, nGDtile_yy, nVRtile_yy);
    }
    else
    {
        bool bCase1 = false;
        bool bCase2 = false;
        if (nPNGYcount < nPNGYcountFull)
        {
            bCase1 = true;
            CPLDebug("Viewranger PNG",
                     "PNG height %d: %d pixel block G=%d V=%d has fewer "
                     "PNGS (%d<%d) than other block rows",
                     nPNGheight, nBlockYSize, nGDtile_yy, nVRtile_yy,
                     nPNGYcount, nPNGYcountFull);
        }

        if (nPNGheight * nPNGYcountFull != nBlockYSize)
        {
            bCase2 = true;
            CPLDebug("Viewranger PNG",
                     "PNG height %d does not divide block height %d - "
                     "counts %d %d - G=%d V=%d",
                     nPNGheight, nBlockYSize, nPNGYcount, nPNGYcountFull,
                     nGDtile_yy, nVRtile_yy);
        }

        if (bCase1 && bCase2)
        {
            CPLDebug("Viewranger PNG", "PNG height: both cases");
        }
        else if (!bCase1 && !bCase2)
        {
            CPLDebug("Viewranger PNG",
                     "PNG height %d - PNG counts %d %d - block height %d - "
                     "G=%d V=%d",
                     nPNGheight, nPNGYcountFull, nPNGYcount, nBlockYSize,
                     nGDtile_yy, nVRtile_yy);
            CPLDebug("Viewranger PNG", "PNG height: neither case");
        }
    }
#endif

    // pbyPNGbuffer needs freeing in lots of places, before we return
    // nullptr
    auto *pbyPNGbuffer = static_cast<png_byte *>(VSIMalloc3(
        3, static_cast<size_t>(nPNGwidth), static_cast<size_t>(nPNGheight)));

    if (pbyPNGbuffer == nullptr)
    {
        CPLError(CE_Failure, CPLE_OutOfMemory,
                 "Cannot allocate memory for PNG buffer");
        return nullptr;
    }
    // Do we need to zero the buffer ?
    for (unsigned int ii = 0; ii < 3 * nPNGwidth * nPNGheight; ii++)
    {
        // apc_row_pointers[ii][jj] = 0;
        pbyPNGbuffer[ii] = nVRCNoData;  // werdna July 14 2020
    }

    std::vector<unsigned char *> apc_row_pointers;
    apc_row_pointers.reserve(static_cast<size_t>(nPNGheight));
    for (unsigned int ii = 0; ii < nPNGheight; ii++)
    {
        apc_row_pointers.push_back(&(pbyPNGbuffer[3L * nPNGwidth * ii]));
    }
    png_set_rows(png_ptr, info_ptr, apc_row_pointers.data());

    auto nPNGdepth = static_cast<unsigned char>(aVRCHeader[8]);
    auto nPNGcolour = static_cast<unsigned char>(aVRCHeader[9]);
    auto nPNGcompress = static_cast<unsigned char>(aVRCHeader[10]);
    auto nPNGfilter = static_cast<unsigned char>(aVRCHeader[11]);
    auto nPNGinterlace = static_cast<unsigned char>(aVRCHeader[12]);
    const uint32_t nPNGcrc = PNGGetUInt(&aVRCHeader, 13);

    CPLDebug("Viewranger PNG",
             "PNG file: %u x %u depth %d colour %d, compress=%d, "
             "filter=%d, interlace=%d crc=x%08x",
             nPNGwidth, nPNGheight, nPNGdepth, nPNGcolour, nPNGcompress,
             nPNGfilter, nPNGinterlace, nPNGcrc);

    switch (nPNGdepth)
    {
        case 1:
        case 2:
        case 4:
        case 8:
            break;
            // case 16:
        default:
            CPLDebug("Viewranger PNG", "PNG file: Depth %d depth unsupported",
                     nPNGdepth);
            VSIFree(pbyPNGbuffer);
            return nullptr;
    }
    switch (nPNGcolour)
    {
        case 0:  // Gray
            break;
        case 2:  // RGB
            if (nPNGdepth == 8)
            {
                break;
            }
            if (nPNGdepth == 16)  //-V547
            {
                CPLError(CE_Warning, CPLE_AppDefined,
                         "16/48bit RGB unexpected");
                break;
            }
#if __cplusplus > 201402L
            [[fallthrough]];
#elif defined(CPL_FALLTHROUGH)
            CPL_FALLTHROUGH
#else
            /* FALLTHROUGH */
#endif
        case 3:                 // Palette
            if (nPNGdepth < 16  //-V560
                && nPNGcolour == 3)
            {
                break;
            }
            CPLDebug("Viewranger PNG",
                     "PNG file: colour %d depth %d combination unsupported",
                     nPNGcolour, nPNGdepth);
            VSIFree(pbyPNGbuffer);
            return nullptr;
            // case 4:  // Gray + Alpha
            // case 6:  // RGBA
        default:
            CPLDebug("Viewranger PNG", "PNG file: colour %d unsupported",
                     nPNGcolour);
            VSIFree(pbyPNGbuffer);
            return nullptr;
    }
    switch (nPNGcompress)
    {
        case 0:
            break;
        default:
            CPLDebug("Viewranger PNG", "PNG file: compress %d unsupported",
                     nPNGcompress);
            VSIFree(pbyPNGbuffer);
            return nullptr;
    }
    switch (nPNGfilter)
    {
        case 0:
            break;
        default:
            CPLDebug("Viewranger PNG", "PNG file: filter %d unsupported",
                     nPNGfilter);
            VSIFree(pbyPNGbuffer);
            return nullptr;
    }
    switch (nPNGinterlace)
    {
        case 0:  // None
        case 1:  // Adam7
            break;
        default:
            CPLDebug("Viewranger PNG", "PNG file: interlace %d unsupported",
                     nPNGinterlace);
            VSIFree(pbyPNGbuffer);
            return nullptr;
    }

    const int check = PNGCRCcheck(VRCpng_callback.vData, nPNGcrc);

    if (1 != check)
    {
        // VSIFree(pbyPNGbuffer);
        // return nullptr;
    }

    // PLTE chunk here (no "PLTE" type string in VRC data)
    if (nPalette != 0)
    {
        if (VSIFSeekL(fp, nPalette, SEEK_SET))
        {
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }

        const unsigned int maxPlteLen = 0x300 + (2UL * sizeof(uint32_t));
        const unsigned int nVRCPlteLen = VRReadUInt(fp);
        if (nVRCPlteLen > static_cast<VRCDataset *>(poDS)->oStatBufL.st_size)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "implausible palette length %u=x%08x", nVRCPlteLen,
                     nVRCPlteLen);
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }
        if (nVRCPlteLen > maxPlteLen)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "unsupported palette length %u=x%08x > x%08x", nVRCPlteLen,
                     nVRCPlteLen, maxPlteLen);
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }
        auto aVRCpalette = std::array<char, maxPlteLen>{};

        const size_t nBytesRead =
            VSIFReadL(aVRCpalette.data(), 1, nVRCPlteLen, fp);
        if (nVRCPlteLen != nBytesRead)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "tried to read %lu=0x%lx bytes of PNG palette data - "
                     "got %lu=0x%0lx",
                     static_cast<unsigned long>(nVRCPlteLen),
                     static_cast<unsigned long>(nVRCPlteLen),
                     static_cast<unsigned long>(nBytesRead),
                     static_cast<unsigned long>(nBytesRead));
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }

        const unsigned int nPNGPlteLen = PNGGetUInt(aVRCpalette.data(), 0);
        if (nVRCPlteLen != nPNGPlteLen + 8)
        {
            CPLDebug("Viewranger PNG",
                     "Palette lengths mismatch: VRC %u != PNG %u +8",
                     nVRCPlteLen, nPNGPlteLen);
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }
        if (nPNGPlteLen > static_cast<VRCDataset *>(poDS)->oStatBufL.st_size)
        {
            CPLDebug("Viewranger PNG",
                     "PNGPalette length %u=x%08x bigger than file !",
                     nPNGPlteLen, nPNGPlteLen);
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }
        if (nPNGPlteLen % 3)
        {
            CPLDebug("Viewranger PNG",
                     "palette size %u=x%08x not a multiple of 3", nPNGPlteLen,
                     nPNGPlteLen);
            VSIFree(pbyPNGbuffer);
            return nullptr;
        }

        CPLDebug("Viewranger PNG", "palette %u=x%08x bytes, %uentries",
                 nPNGPlteLen, nPNGPlteLen, nPNGPlteLen / 3);

        // vector_append_uint32(VRCpng_callback.vData, &nVRCPlteLen); // wrong
        VRCpng_callback.vData.push_back((nPNGPlteLen >> 24) & 0xff);
        VRCpng_callback.vData.push_back((nPNGPlteLen >> 16) & 0xff);
        VRCpng_callback.vData.push_back((nPNGPlteLen >> 8) & 0xff);
        VRCpng_callback.vData.push_back(nPNGPlteLen & 0xff);

        const std::string PLTE = "PLTE";

        vector_append(VRCpng_callback.vData, PLTE);

        std::copy(aVRCpalette.data() + 4, aVRCpalette.data() + nVRCPlteLen,
                  std::back_inserter(VRCpng_callback.vData));
        CPLDebug("Viewranger PNG", "PLTE %llu, VRClen %" PRI_SIZET, nPalette,
                 VRCpng_callback.vData.size());
    }
    else
    {  // if (nVRCpalette!=0)
        if (nPNGcolour == 3)
        {
            CPLDebug("Viewranger PNG",
                     "Colour type 3 PNG: needs a PLTE. Assuming Greyscale.");
            // Next four bytes are 3*256 in PNGendian
            VRCpng_callback.vData.push_back(0);  // -V525
            VRCpng_callback.vData.push_back(0);  // -V525
            VRCpng_callback.vData.push_back(3);  // -V525
            VRCpng_callback.vData.push_back(0);  // -V525
            const std::string PLTE = "PLTE";
            vector_append(VRCpng_callback.vData, PLTE);
            for (int i = 0; i < 256; i++)
            {
                // for (unsigned char i=0; i<256; i++) gives
                //  i<256 is always true.
                // "i & 255" is shorter than "static_cast<char>(i)"
                VRCpng_callback.vData.push_back(i & 255);
                VRCpng_callback.vData.push_back(i & 255);
                VRCpng_callback.vData.push_back(i & 255);
            }

            // The checksum 0xe2b05d7d (not 0xa5d99fdd) of the greyscale
            // palette.
            VRCpng_callback.vData.push_back(0xe2);
            VRCpng_callback.vData.push_back(0xb0);
            VRCpng_callback.vData.push_back(0x5d);
            VRCpng_callback.vData.push_back(0x7d);
        }
        CPLDebug("Viewranger PNG", "PLTE finishes at %" PRI_SIZET,
                 VRCpng_callback.vData.size());
    }

    // Jump to VRCData
    if (VSIFSeekL(fp, nVRCData, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "cannot seek to nVRCData %u=x%08x", nVRCData, nVRCData);
        VSIFree(pbyPNGbuffer);
        return nullptr;
    }

    char *pVRCpngData = static_cast<char *>(VSIMalloc(nVRCDataLen));
    if (pVRCpngData == nullptr)
    {
        CPLDebug("Viewranger PNG", "could not allocate memory for VRCpngData");
        VSIFree(pbyPNGbuffer);
        return nullptr;
    }
    auto nBytesRead =
        static_cast<unsigned>(VSIFReadL(pVRCpngData, 1, nVRCDataLen, fp));
    if (nVRCDataLen != nBytesRead)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "only read %u=x%08x bytes of PNG data out of %u=x%08x",
                 nBytesRead, nBytesRead, nVRCDataLen, nVRCDataLen);
        VSIFree(pbyPNGbuffer);
        return nullptr;
    }
    CPLDebug("Viewranger PNG", "   was %" PRI_SIZET,
             VRCpng_callback.vData.size());
    std::copy(pVRCpngData, pVRCpngData + nVRCDataLen,
              std::back_inserter(VRCpng_callback.vData));
    CPLDebug("Viewranger PNG", "   now %" PRI_SIZET,
             VRCpng_callback.vData.size());
    VSIFree(pVRCpngData);

    // IEND chunk is fixed and pre-canned.
    vector_append(VRCpng_callback.vData, IEND_chunk);

    const char *szDumpPNG = CPLGetConfigOption("VRC_DUMP_PNG", "");
    if (szDumpPNG != nullptr && *szDumpPNG != 0 && nBand == 1)
    {
        // The PNG data covers all bands, so only dump the first one.
        auto *poVRCDS = static_cast<VRCDataset *>(poDS);
        auto nEnvPNGDump =
            static_cast<unsigned int>(strtol(szDumpPNG, nullptr, 10));
        const CPLString osBaseLabel = CPLString().Printf(
            "/tmp/werdna/vrc2tif/%s.%01d.%03d.%03d.%03u.%03u.%02d.x%012x",
            // CPLGetBasenameSafe(poOpenInfo->pszFilename) doesn't quite work
            poVRCDS->sFileName.c_str(),
            // poVRCDS->sLongTitle.c_str(),
            nThisOverview, nGDtile_xx, nGDtile_yy, nVRtile_xx, nVRtile_yy,
            nBand, nVRCHeader);
        const double dTopHeightAdjust =
            (nGDtile_yy == 0) ? poVRCDS->nTopSkipPix : 0.0;
        if (nGDtile_yy == 0 || poVRCDS->nTopSkipPix != 0)
        {
            CPLDebug("Viewranger", "nGDtile_yy %d, dTopHeightAdjust %g",
                     nGDtile_yy, dTopHeightAdjust);
        }
        const CPLString osWLDparams = CPLString().Printf(
            "%8.8g\n%8.8g\n%8.8g\n%8.8g\n%8.8g\n%8.8g\n",
            // multiply next four values by overview scale
            poVRCDS->dfPixelMetres, 0.0, 0.0, -poVRCDS->dfPixelMetres,
            // not these two ?
            poVRCDS->nLeft + (poVRCDS->dfPixelMetres *
                              ((static_cast<double>(nGDtile_xx) * nBlockXSize) +
                               (static_cast<double>(nVRtile_xx) * nPNGwidth))),
            poVRCDS->nTop -
                (
                    // Not sure this is right - 2024-05-22
                    // - in DE_25_W867170_E882510_S5635610_N5645830.VRC
                    // we need to swap the rows.
                    poVRCDS->dfPixelMetres *
                    ((static_cast<double>(nGDtile_yy) * nBlockYSize) +
                     (static_cast<double>(nVRtile_yy) * nPNGheight) +
                     dTopHeightAdjust)));
        dumpPNG(VRCpng_callback.vData.data(),
                static_cast<int>(VRCpng_callback.vData.size()), osBaseLabel,
                osWLDparams, nEnvPNGDump);
    }

    //******************************************************************/
    // if (color_type == PNG_COLOR_TYPE_PALETTE)
    //     png_set_palette_to_rgb(png_ptr);
    //
    // if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    //     png_set_expand_gray_1_2_4_to_8(png_ptr);
    //

    // png_set_read_fn( png_ptr, info_ptr, VRC_png_read_data_fn );
    png_set_read_fn(png_ptr, &VRCpng_callback, VRC_png_read_data_fn);

    CPLDebug("Viewranger PNG",
             "before png_read_png\nVRCpng_callback.vData %p (%p %" PRI_SIZET
             " %" PRI_SIZET ")",
             &VRCpng_callback, VRCpng_callback.vData.data(),
             VRCpng_callback.vData.size(), VRCpng_callback.nCurrent);

    // May wish to change this so that the result is RGBA (currently RGB)
    png_read_png(png_ptr, info_ptr,
#if PNG_LIBPNG_VER >= 10504
                 PNG_TRANSFORM_SCALE_16 |
#else
                 PNG_TRANSFORM_STRIP_16 |
#endif
#if PNG_LIBPNG_VER >= 10245
                     PNG_TRANSFORM_GRAY_TO_RGB |
#endif

                     PNG_TRANSFORM_STRIP_ALPHA | PNG_TRANSFORM_PACKING |
                     PNG_TRANSFORM_EXPAND,
                 nullptr);

    CPLDebug("Viewranger PNG",
             //"after png_read_png\nVRCpng_callback.vData %p (%p %u %u)",
             "after png_read_png\nVRCpng_callback.vData %p (%p %" PRI_SIZET
             " %" PRI_SIZET ")",
             &VRCpng_callback, VRCpng_callback.vData.data(),
             VRCpng_callback.vData.size(), VRCpng_callback.nCurrent);

    png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);

    return pbyPNGbuffer;
}

//***********************************************************************/
//                          GDALRegister_VRC()                          */
//***********************************************************************/

void CPL_DLL GDALRegister_VRC()
{
    if (!GDAL_CHECK_VERSION("ViewrangerVRC"))
        return;

    if (GDALGetDriverByName("ViewrangerVRC") != nullptr)
        return;

    auto *poDriver = new GDALDriver();
    if (poDriver == nullptr)  // -V668
    {
        CPLError(CE_Failure, CPLE_ObjectNull,
                 "Could not build a driver for VRC");
        return;
    }

    poDriver->SetDescription("ViewrangerVRC");

    poDriver->SetMetadataItem(GDAL_DCAP_RASTER, "YES");

    poDriver->SetMetadataItem(GDAL_DMD_LONGNAME, "ViewRanger (.VRC)");
    poDriver->SetMetadataItem(GDAL_DMD_HELPTOPIC, "drivers/raster/vrc.html");
    poDriver->SetMetadataItem(GDAL_DMD_EXTENSION, "VRC");

    // poDriver->SetMetadataItem( GDAL_DMD_CREATIONDATATYPES, "Byte Int16"
    // );
    poDriver->SetMetadataItem(GDAL_DMD_CREATIONDATATYPES, "");
    poDriver->SetMetadataItem(GDAL_DCAP_VIRTUALIO, "YES");

    // Which of these is correct ?
    // poDriver->SetMetadataItem(GDALMD_AREA_OR_POINT, GDALMD_AOP_POINT);
    poDriver->SetMetadataItem(GDALMD_AREA_OR_POINT, GDALMD_AOP_AREA);
    // GDALMD_AOP_AREA is the GDAL default.

    // See https://gdal.org/development/rfc/rfc34_license_policy.html
    poDriver->SetMetadataItem("LICENSE_POLICY", "NONRECIPROCAL");

    // poDriver->SetMetadataItem( "INTERLEAVE", "PIXEL", "IMAGE_STRUCTURE"
    // );

    poDriver->pfnOpen = VRCDataset::Open;
    poDriver->pfnIdentify = VRCDataset::Identify;

    GetGDALDriverManager()->RegisterDriver(poDriver);
}

// -------------------------------------------------------------------------

int VRCRasterBand::GetOverviewCount()
{
    auto *poVRCDS = static_cast<VRCDataset *>(poDS);
    if (poVRCDS == nullptr)
    {
        CPLDebug("VRC", "%p->GetOverviewCount() - band has no dataset", this);
        return 0;
    }

    auto *poFullBand =
        static_cast<VRCRasterBand *>(poVRCDS->GetRasterBand(nBand));
    if (nullptr == poFullBand)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "%s %p->GetOverviewCount() band %d but dataset %p has no "
                 "such band",
                 poVRCDS->sFileName.c_str(),
                 // poVRCDS->sLongTitle.c_str(),
                 this, nBand, poVRCDS);
        return 0;
    }
    if (this == poFullBand)
    {
        CPLDebug("Viewranger OVRV",
                 "%s band %p is a parent band with %d overviews at %p",
                 poVRCDS->sFileName.c_str(),
                 // poVRCDS->sLongTitle.c_str(),
                 this, poFullBand->nOverviewCount,
                 poFullBand->papoOverviewBands);
        if (nOverviewCount != poFullBand->nOverviewCount)
        {
            // This cannot happen ?
            CPLError(CE_Failure, CPLE_AppDefined,
                     "%s %p==%p but overview count %d != %d",
                     poVRCDS->sFileName.c_str(),
                     // poVRCDS->sLongTitle.c_str(),
                     this, poFullBand, nOverviewCount,
                     poFullBand->nOverviewCount);
        }
    }
    else
    {
        CPLDebug("Viewranger OVRV",
                 "%s band %p has %d overviews at %p; its parent %p has %d "
                 "overviews at %p",
                 poVRCDS->sFileName.c_str(),
                 // poVRCDS->sLongTitle.c_str(),
                 this, nOverviewCount, papoOverviewBands, poFullBand,
                 poFullBand->nOverviewCount, poFullBand->papoOverviewBands);
    }

    if (poFullBand->papoOverviewBands)
    {
        return poFullBand->nOverviewCount;
    }

    return 0;
}

//***********************************************************************/
//                            GetOverview()                             */
//***********************************************************************/

GDALRasterBand *VRCRasterBand::GetOverview(int iOverviewIn)
{
    auto *poVRCDS = static_cast<VRCDataset *>(poDS);
    if (poVRCDS == nullptr)
    {
        CPLDebug("VRC", "%p->GetOverview(%d) - band has no dataset", this,
                 iOverviewIn);
        return nullptr;
    }

    auto *poFullBand =
        static_cast<VRCRasterBand *>(poVRCDS->GetRasterBand(nBand));
    if (poFullBand == nullptr)
    {
        CPLDebug("VRC", "%p->GetOverview(%d) - dataset %p has no band %d", this,
                 iOverviewIn, poVRCDS, nBand);
        return nullptr;
    }

    // Short circuit the sanity checks in this case.
    if (iOverviewIn == poFullBand->nThisOverview)
    {
        CPLDebug("VRC", "%p->GetOverview(%d) is itself", poFullBand,
                 iOverviewIn);
        return poFullBand;
    }

    if (nOverviewCount > 32)
    {
        CPLDebug("Viewranger",
                 "nBand %d requested overview %d of %d: more than 32 is "
                 "silly - something has gone wrong",
                 nBand, iOverviewIn, nOverviewCount);
        // This *should* cause it to be regenerated if required.
        nOverviewCount = -1;
        return nullptr;
    }
    if (nOverviewCount < -1)
    {
        CPLDebug("Viewranger",
                 "nBand %d has %d overviews, but overview %d requested - "
                 "something has gone wrong",
                 nBand, nOverviewCount, iOverviewIn);
        nOverviewCount =
            -1;  // This should cause it to be regenerated if required.
        return nullptr;
    }
    if (iOverviewIn < 0 || iOverviewIn >= poFullBand->nOverviewCount)
    {
        CPLDebug("Viewranger",
                 "nBand %d expected 0<= iOverviewIn %d < nOverviewCount %d",
                 nBand, iOverviewIn, poFullBand->nOverviewCount);
        return nullptr;
    }
    if (iOverviewIn > 32)
    {
        // We should not get here
        CPLDebug("Viewranger",
                 "nBand %d overview %d requested: more than 32 is silly", nBand,
                 iOverviewIn);
        return nullptr;
    }
    if (poFullBand->papoOverviewBands == nullptr)
    {
        // CPLDebug("Viewranger",
        CPLError(CE_Failure, CPLE_AppDefined,
                 "%p->GetOverview(%d) nBand %d - no overviews but count is "
                 "%d :-(",
                 this, iOverviewIn, nBand, nOverviewCount);
        return nullptr;
    }

    VRCRasterBand *pThisOverview = poFullBand->papoOverviewBands[iOverviewIn];
    CPLDebug("Viewranger",
             "GetOverview(%d) nBand %d - returns %d x %d overview %p "
             "(overview count is %d)",
             iOverviewIn, nBand, pThisOverview->nRasterXSize,
             pThisOverview->nRasterYSize, pThisOverview, nOverviewCount);
    if (this == pThisOverview)
    {
        static int nCount = 0;
        nCount++;
        if (0 == (nCount & (nCount - 1)))
        {  // ie if nCount is a power of 2
            CPLDebug("VRC",
                     "%p->VRCRasterBand::GetOverview(%d) returns itself - "
                     "called %d times",
                     this, iOverviewIn, nCount);
        }
    }

    return pThisOverview;
}

extern void dumpTileHeaderData(VSILFILE *fp, unsigned int nTileIndex,
                               unsigned int nOverviewCount,
                               const unsigned int anTileOverviewIndex[],
                               const int tile_xx, const int tile_yy)
{
    if (fp == nullptr || anTileOverviewIndex == nullptr)
    {
        return;
    }

    const vsi_l_offset byteOffset = VSIFTellL(fp);
    if (nOverviewCount != 7)
    {
        CPLDebug("Viewranger", "tile (%d %d) header at x%x: %u - not seven",
                 tile_xx, tile_yy, nTileIndex, nOverviewCount);
        // CPLDebug does not "use" values
        (void)tile_xx;
        (void)tile_yy;
    }
    if (VSIFSeekL(fp, nTileIndex, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "dumpTileHeaderData cannot seek to nTileIndex %u=x%08ux",
                 nTileIndex, nTileIndex);
    }
    for (unsigned int i = 0; i < nOverviewCount; i++)
    {
        const unsigned int a = anTileOverviewIndex[i];
        if (0 == a)
        {
            CPLDebug("Viewranger", "\tanTileOverviewIndex[%u] =x%08x", i, a);
        }
        else
        {
            const int nXcount = VRReadInt(fp, a);
            const int nYcount = VRReadInt(fp, a + 4);
            const int nXsize = VRReadInt(fp, a + 8);
            const int nYsize = VRReadInt(fp, a + 12);
            CPLDebug("Viewranger",
                     "\ttile(%d,%d) anTileOverviewIndex[%u]=x%08x %dx%d "
                     "tiles each %dx%d pixels",
                     tile_xx, tile_yy, i, a, nXcount, nYcount, nXsize, nYsize);
            // CPLDebug does not "use" values
            (void)nXcount;
            (void)nYcount;
            (void)nXsize;
            (void)nYsize;
        }
    }
    if (VSIFSeekL(fp, byteOffset, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "dumpTileHeaderData cannot return file pointer to VRC "
                 "byteOffset %llu=x%08llx",
                 byteOffset, byteOffset);
    }
}

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
//                VRCRasterBand::read_VRC_Tile_PNG()
// -------------------------------------------------------------------------
// -------------------------------------------------------------------------

void VRCRasterBand::read_VRC_Tile_PNG(VSILFILE *fp, int block_xx, int block_yy,
                                      void *pImage)
{
    auto *const poVRCDS = static_cast<VRCDataset *>(poDS);

    if (block_xx < 0 || block_xx >= poVRCDS->nRasterXSize)
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "read_VRC_Tile_PNG invalid row %d", block_xx);
        return;
    }
    if (block_yy < 0 || block_yy >= poVRCDS->nRasterYSize)
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "read_VRC_Tile_PNG invalid column %d", block_yy);
        return;
    }
    if (pImage == nullptr)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "read_VRC_Tile_PNG passed no image");
        return;
    }
    if (poVRCDS->nMagic != vrc_magic)
    {
        // Second "if" will be temporary
        // if we can read "VRC36" file data at the subtile/block level.
        if (poVRCDS->nMagic != vrc_magic36)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "read_VRC_Tile_PNG called with wrong magic number x%08x",
                     poVRCDS->nMagic);
            return;
        }
    }

    CPLDebug("Viewranger",
             "read_VRC_Tile_PNG(%p, %d, %d, %p) band %d overview %d", fp,
             block_xx, block_yy, pImage, nBand, nThisOverview);

    // const int tilenum = poVRCDS->nRasterXSize * block_xx + block_yy;
    // const int tilenum = nBlockYSize * block_xx + block_yy;
    // const int tilenum = poVRCDS->tileYcount * block_xx + block_yy;
    const unsigned int tilenum =
        static_cast<unsigned int>(block_xx) +
        (poVRCDS->tileXcount * static_cast<unsigned int>(block_yy));

    const unsigned int nTileIndex = poVRCDS->anTileIndex[tilenum];
    CPLDebug("Viewranger",
             "\tblock %d x %d, (%d, %d) tilenum %u tileIndex x%08x",
             nBlockXSize, nBlockYSize, block_xx, block_yy, tilenum, nTileIndex);

    // Write nodata to the canvas before we start reading
    if (eDataType == GDT_Byte)
    {
        for (int j = 0; j < nBlockYSize; j++)
        {
            for (int i = 0; i < nBlockXSize; i++)
            {
                const int pixelnum = i + (j * nBlockXSize);
                if (nBand == 4)
                {
                    static_cast<GByte *>(pImage)[pixelnum] =
                        255;  // alpha: opaque
                }
                else
                {
                    static_cast<GByte *>(pImage)[pixelnum] = nVRCNoData;
                }
                // ((GByte *) pImage)[pixelnum] = nVRCNoData;
            }
        }
    }
    else
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRCRasterBand::read_VRC_Tile_PNG eDataType %d "
                 "unexpected for null tile",
                 eDataType);
    }

    if (nTileIndex == 0)
    {
        // No data for this tile
        CPLDebug("Viewranger",
                 "VRCRasterBand::read_VRC_Tile_PNG(.. %d %d ..) null tile",
                 block_xx, block_yy);

        return;
    }

    if (nTileIndex >= poVRCDS->oStatBufL.st_size)
    {
        // No data for this tile
        CPLDebug("Viewranger",
                 "VRCRasterBand::read_VRC_Tile_PNG(.. %d %d ..) "
                 "tileIndex %u %s end of file",
                 block_xx, block_yy, nTileIndex,
                 nTileIndex == poVRCDS->oStatBufL.st_size ? "at" : "beyond");
        return;
    }

    if (VSIFSeekL(fp, nTileIndex, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "cannot seek to tile header x%08x", nTileIndex);
        return;
    }

    nOverviewCount = VRReadInt(fp);

    if (nOverviewCount != 7)
    {
        CPLDebug("Viewranger OVRV",
                 "read_VRC_Tile_PNG: nOverviewCount is %d - expected "
                 "seven - MapID %d",
                 nOverviewCount, poVRCDS->nMapID);
        return;
    }

    unsigned int anTileOverviewIndex[7] = {};
    for (int ii = 0; ii < std::min(7, nOverviewCount); ii++)
    {
        anTileOverviewIndex[ii] = VRReadUInt(fp);
    }
    CPLDebug("Viewranger OVRV",
             "x%08x: %d  x%08x x%08x x%08x  x%08x x%08x x%08x x%08x",
             nTileIndex, nOverviewCount, anTileOverviewIndex[0],
             anTileOverviewIndex[1], anTileOverviewIndex[2],
             anTileOverviewIndex[3], anTileOverviewIndex[4],
             anTileOverviewIndex[5], anTileOverviewIndex[6]);

    // VRC counts main image plus 6 overviews.
    // GDAL just counts the 6 overview images.
    // anTileOverviewIndex[0] points to the full image
    // ..[1-6] are the overviews:
    nOverviewCount--;  // equals 6

    // If the smallest overviews do not exist, ignore them.
    // This saves this driver generating them from larger overviews,
    // they may need to be generated elsewhere ...
    while (nOverviewCount > 0 && 0 == anTileOverviewIndex[nOverviewCount])
    {
        nOverviewCount--;
    }
    if (nOverviewCount < 6)
    {
        CPLDebug("Viewranger OVRV", "Overviews %d-6 not available",
                 1 + nOverviewCount);
    }

    if (nOverviewCount < 1 || anTileOverviewIndex[0] == 0)
    {
        CPLDebug("Viewranger",
                 "VRCRasterBand::read_VRC_Tile_PNG(.. %d %d ..) empty tile",
                 block_xx, block_yy);
        return;
    }

    dumpTileHeaderData(fp, nTileIndex,
                       1 + static_cast<unsigned int>(nOverviewCount),
                       anTileOverviewIndex, block_xx, block_yy);

    if (nThisOverview < -1 || nThisOverview >= nOverviewCount)
    {
        CPLDebug("Viewranger OVRV",
                 "read_VRC_Tile_PNG: overview %d not in range [-1, %d)",
                 nThisOverview, nOverviewCount);
        return;
    }

    if (anTileOverviewIndex[nThisOverview + 1] >= poVRCDS->oStatBufL.st_size)
    {
        CPLDebug("Viewranger OVRV",
                 "\toverview level %d data at x%08x is beyond end of file",
                 nThisOverview, anTileOverviewIndex[nThisOverview + 1]);
        return;
    }
    CPLDebug("Viewranger OVRV", "\toverview level %d data at x%08x",
             nThisOverview, anTileOverviewIndex[nThisOverview + 1]);

    const bool bTileShrink = (0 == anTileOverviewIndex[nThisOverview + 1]);
    unsigned int nShrinkFactor = 1;
    // unsigned int nShrinkFactor= bTileShrink ? 2 : 1 ;
    if (bTileShrink == false)
    {
        nShrinkFactor = 1;  // -V1048 reassigning initialized value
        if (VSIFSeekL(fp, anTileOverviewIndex[nThisOverview + 1], SEEK_SET))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "cannot seek to overview level %d data at x%08x",
                     nThisOverview, anTileOverviewIndex[nThisOverview + 1]);
            return;
        }

        CPLDebug("Viewranger OVRV",
                 "\tblock %d x %d, max %u min %u overview %d", nBlockXSize,
                 nBlockYSize, poVRCDS->tileSizeMax, poVRCDS->tileSizeMin,
                 nThisOverview);
    }
    else
    {  // bTileShrink == true;
        // Data for this block is not available
        // so we need to rescale another overview.
        if (anTileOverviewIndex[nThisOverview] == 0)
        {
            CPLDebug("Viewranger OVRV",
                     "Band %d block %d,%d overviews %d and %d empty - "
                     "cannot shrink one to get other\n",
                     nBand, block_xx, block_yy, nThisOverview - 1,
                     nThisOverview);
            return;
        }

        nShrinkFactor = 2;

        CPLDebug("Viewranger OVRV",
                 "Band %d block %d,%d empty at overview %d\n", nBand, block_xx,
                 block_yy, nThisOverview);
        CPLDebug("Viewranger OVRV", "\t overview %d at x%08x\n",
                 nThisOverview - 1, anTileOverviewIndex[nThisOverview]);

        if (VSIFSeekL(fp, anTileOverviewIndex[nThisOverview], SEEK_SET))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "cannot seek to overview level %d data at x%08x",
                     nThisOverview - 1, anTileOverviewIndex[nThisOverview]);
            return;
        }

        CPLDebug("Viewranger OVRV",
                 "Band %d block %d,%d overview %d will be downsampled", nBand,
                 block_xx, block_yy, nThisOverview);
    }

    // We have reached the start of the tile
    // ... but it is split into (essentially .png file) subtiles
    const unsigned int nPNGXcount = VRReadUInt(fp);
    const unsigned int nPNGYcount = VRReadUInt(fp);
    const unsigned int pngXsize = VRReadUInt(fp);
    const unsigned int pngYsize = VRReadUInt(fp);

    if (nPNGXcount == 0 || nPNGYcount == 0)
    {
        CPLDebug("Viewranger", "tilenum %u contains no subtiles (%u x %u)",
                 tilenum, nPNGXcount, nPNGYcount);
        return;
    }
    if (pngXsize == 0 || pngYsize == 0)
    {
        CPLDebug("Viewranger", "empty (%u x %u) subtile in tilenum %u",
                 pngXsize, pngYsize, tilenum);
        return;
    }
    auto nFullBlockXSize = static_cast<unsigned>(nBlockXSize) * nShrinkFactor;
    if (nPNGXcount > nFullBlockXSize || pngXsize > nFullBlockXSize ||
        nPNGXcount * pngXsize > nFullBlockXSize)
    {
        CPLDebug("Viewranger",
                 "nPNGXcount %u x pngXsize %u too big > nBlockXSize %d * "
                 "nShrinkFactor %u",
                 nPNGXcount, pngXsize, nBlockXSize, nShrinkFactor);
        // return;
    }
    auto nFullBlockYSize = static_cast<unsigned>(nBlockYSize) * nShrinkFactor;
    if (nPNGYcount > nFullBlockYSize || pngYsize > nFullBlockYSize ||
        nPNGYcount * pngYsize > nFullBlockYSize)
    {
        CPLDebug("Viewranger",
                 "nPNGYcount %u x pngYsize %u too big > nBlockYSize %d * "
                 "nShrinkFactor %u",
                 nPNGYcount, pngYsize, nBlockYSize, nShrinkFactor);
        // return;
    }

    CPLDebug("Viewranger",
             "ovrvw %d nPNGXcount %u nPNGYcount %u pngXsize %u pngYsize %u "
             "nShrinkFactor %u",
             nThisOverview, nPNGXcount, nPNGYcount, pngXsize, pngYsize,
             nShrinkFactor);

    // Read in this tile's index to png sub-tiles.
    std::vector<unsigned int> anPngIndex;
    anPngIndex.reserve((static_cast<size_t>(nPNGXcount) * nPNGYcount) + 1);
    for (unsigned long loop = 0;
         loop <= static_cast<unsigned long>(nPNGXcount) * nPNGYcount;
         // <= because there is an extra entry
         //    pointing just passed the last png sub-tile.
         loop++)
    {
        // was anPngIndex[loop] = VRReadUInt(fp);
        anPngIndex.push_back(VRReadUInt(fp));
        if (anPngIndex.back() > poVRCDS->oStatBufL.st_size)
        {
            CPLDebug("Viewranger",
                     "Band %d ovrvw %d block [%d,%d] png image %lu at x%x "
                     "is beyond EOF - is file truncated ?",
                     nBand, nThisOverview, block_xx, block_yy, loop,
                     anPngIndex.back());
            anPngIndex.back() = 0;
        }
    }

    // unsigned int nPNGplteIndex = nTileIndex + 0x20 + 0x10 +8
    //    + 4*(nPNGXcount*nPNGYcount+1);
    vsi_l_offset nPNGplteIndex = VSIFTellL(fp);

    const unsigned int VRCplteSize = VRReadUInt(fp);
    const unsigned int PNGplteSize = PNGReadUInt(fp);
    if (VRCplteSize - PNGplteSize == 8)
    {
        if (PNGplteSize % 3)
        {
            CPLDebug("Viewranger",
                     "ignoring palette: size %u=x%08x not a multiple of 3",
                     PNGplteSize, PNGplteSize);
            nPNGplteIndex = 0;
        }
        else
        {
        }
    }
    else
    {
        nPNGplteIndex = 0;
    }

    int nLeftCol = 0;
    unsigned int nPrevPNGwidth = 0;
    const unsigned int nXlimit = MIN(nPNGXcount, nFullBlockXSize);
    const unsigned int nYlimit = MIN(nPNGYcount, nFullBlockYSize);
    for (unsigned int loopX = 0; loopX < nXlimit; loopX++)
    {
        int nRightCol = 0;
        unsigned int nPrevPNGheight = 0;
        int nBottomRow = nBlockYSize;
        // int nTopRow;

        // unsigned>=0 always true, so a standard for loop
        // for (unsigned int loopY=nYlimit; loopY >= 0; loopY--)
        // will not work:
        for (unsigned int loopY = nYlimit; loopY >= 1; /* see comments */)
        {
            // decrement at *start* of loop
            // so that we can have the last pass with loopY==0
            --loopY;

            const unsigned int loop =
                (nYlimit - 1 - loopY) + (loopX * nPNGYcount);

            const unsigned int nHeader =
                anPngIndex[static_cast<unsigned>(loop)];
            const unsigned int nextPngIndex =
                anPngIndex[static_cast<unsigned>(loop + 1)];
            const signed int nDataLen = static_cast<signed>(nextPngIndex) -
                                        static_cast<signed>(nHeader + 0x12);
            if (nHeader == 0)
            {
                CPLDebug("Viewranger", "block (%d,%d) tile (%u,%u) empty",
                         block_xx, block_yy, loopX, loopY);
                continue;
            }
            if (nDataLen < 1)
            {  // There should be a better/higher limit.
                CPLDebug("Viewranger PNG",
                         "block (%d,%d) tile (%u,%u) PNG data "
                         "overflows - length %d",
                         block_xx, block_yy, loopX, loopY, nDataLen);
                continue;
            }
            // unsigned int nPalette = nPNGplteIndex;

            switch (poVRCDS->nMagic)
            {
                case vrc_magic:
                {
                    unsigned int nPNGwidth = 0;
                    unsigned int nPNGheight = 0;

                    png_byte *pbyPNGbuffer = read_PNG(
                        fp, &nPNGwidth, &nPNGheight, nHeader, nPNGplteIndex,
                        static_cast<unsigned int>(nDataLen), block_xx, block_yy,
                        loopX, loopY);
                    if (pbyPNGbuffer)
                    {
                        CPLDebug("Viewranger",
                                 "read_PNG() returned %p: %u x %u tile",
                                 pbyPNGbuffer, nPNGwidth, nPNGheight);
                        const char *szDumpTile =
                            CPLGetConfigOption("VRC_DUMP_TILE", "");
                        if (szDumpTile != nullptr && *szDumpTile != 0)
                        {
                            auto nEnvTile = static_cast<unsigned int>(
                                strtol(szDumpTile, nullptr, 10));
                            // Dump pbyPNGbuffer as .ppm, one for each
                            // band, they should be full-colour and the
                            // same.
                            const CPLString osBaseLabel = CPLString().Printf(
                                "/tmp/werdna/vrc2tif/"
                                "%s.%01d.%03d.%03d.%03u.%03u.%02da."
                                "x%012x.rvtm_pngsize",
                                // CPLGetBasenameSafe(poOpenInfo->pszFilename)
                                // doesn't quite work
                                poVRCDS->sFileName.c_str(),
                                // poVRCDS->sLongTitle.c_str(),
                                nThisOverview, block_xx, block_yy, loopX, loopY,
                                nBand, nHeader);
                            dumpPPM(nPNGwidth, nPNGheight, pbyPNGbuffer,
                                    nPNGwidth, osBaseLabel, pixel, nEnvTile);
                        }

                        if (nPrevPNGwidth == 0)
                        {
                            nPrevPNGwidth = nPNGwidth;
                        }
                        else if (nPNGwidth != nPrevPNGwidth)
                        {
                            CPLDebug("Viewranger",
                                     "PNG width %u different from "
                                     "previous tile %u in same column",
                                     nPNGwidth, nPrevPNGwidth);
                        }

                        if (nPrevPNGheight == 0)
                        {
                            nPrevPNGheight = nPNGheight;
                        }
                        else if (nPrevPNGheight != nPNGheight)
                        {
                            CPLDebug("Viewranger",
                                     "PNG height %u different from "
                                     "previous tile %u in same row",
                                     nPNGheight, nPrevPNGheight);
                        }

                        nRightCol = nLeftCol;
                        int nTopRow = nBottomRow;
                        nRightCol += nPNGwidth / nShrinkFactor;
                        nTopRow -= nPNGheight / nShrinkFactor;

                        if (nPNGheight >= nFullBlockYSize)
                        {
                            // single tile block
                            if (nTopRow < 0)
                            {
                                CPLDebug("Viewranger",
                                         "Single PNG high band toprow "
                                         "%d set to 0",
                                         nTopRow);
                                nTopRow = 0;
                            }
                        }
                        if (nTopRow < 0)
                        {
                            CPLDebug("Viewranger",
                                     "%u tall PNG tile: top row %d "
                                     "above top of %d tall block",
                                     nPNGheight, nTopRow, nBlockYSize);
                        }

                        // Blank the top of the top tile if necessary
                        if (loopY == nYlimit - 1)
                        {
                            auto *pGImage = static_cast<GByte *>(pImage);
                            for (int ii = nBlockYSize; ii < nTopRow; ii++)
                            {
                                for (int jj = nLeftCol; jj < nRightCol; jj++)
                                {
                                    pGImage[jj] =  // (ii+jj)%255;
                                        nVRCNoData;
                                }
                                pGImage += nBlockXSize;
                            }
                        }

                        int nCopyResult = 0;
                        if (!bTileShrink)
                        {
                            CPLDebug("Viewranger",
                                     "Band %d: Copy_Tile_ (%u %u) "
                                     "into_Block (%d %d) [%d "
                                     "%d)x[%d %d)",
                                     nBand, loopX, loopY, block_xx, block_yy,
                                     nLeftCol, nRightCol, nTopRow, nBottomRow);
                            nCopyResult = Copy_Tile_into_Block(
                                static_cast<GByte *>(pbyPNGbuffer),
                                static_cast<int>(nPNGwidth),
                                static_cast<int>(nPNGheight), nLeftCol,
                                nRightCol, nTopRow, nBottomRow, pImage
                                // , nBlockXSize, nBlockYSize
                            );
                        }
                        else  //  if (!bTileShrink)
                        {
                            CPLDebug("Viewranger",
                                     "Band %d: Shrink_Tile_ (%u "
                                     "%u) into_Block (%d %d) [%d "
                                     "%d)x[%d %d)",
                                     nBand, loopX, loopY, block_xx, block_yy,
                                     nLeftCol, nRightCol, nTopRow, nBottomRow);

                            nCopyResult = Shrink_Tile_into_Block(
                                static_cast<GByte *>(pbyPNGbuffer),
                                static_cast<int>(nPNGwidth),
                                static_cast<int>(nPNGheight), nLeftCol,
                                nRightCol, nTopRow, nBottomRow, pImage
                                // , nBlockXSize, nBlockYSize
                            );
                            CPLDebug("Viewranger",
                                     "\tShrink_Tile (%u %u) _into_Block "
                                     "(%d %d) returned %d",
                                     loopX, loopY, block_xx, block_yy,
                                     nCopyResult);
                        }

                        nBottomRow = nTopRow;
                        VSIFree(pbyPNGbuffer);
                        pbyPNGbuffer = nullptr;
                        if (nCopyResult)
                        {
                            CPLDebug("Viewranger",
                                     "failed to copy/shrink tile to block");
                        }
                    }
                    else  // if (pbyPNGbuffer)
                    {
                        // read_PNG returned nullptr
                        CPLDebug("Viewranger",
                                 "empty %u x %u tile ... prev was %u x %u",
                                 nPNGwidth, nPNGheight, nPrevPNGwidth,
                                 nPrevPNGheight);
                    }
                    CPLDebug("Viewranger",
                             "... read PNG tile (%u %u) overview "
                             "%d block (%d %d) completed",
                             loopX, loopY, nThisOverview, block_xx, block_yy);
                }
                break;

                default:
                    CPLError(CE_Failure, CPLE_AppDefined,
                             "We should not be here with magic=x%08x",
                             poVRCDS->nMagic);
                    return;
            }
        }
        nLeftCol = nRightCol;
    }
}

int VRCRasterBand::Copy_Tile_into_Block(GByte *pbyPNGbuffer, int nPNGwidth,
                                        int nPNGheight, int nLeftCol,
                                        int nRightCol, int nTopRow,
                                        int nBottomRow, void *pImage)
{
    // Copy image data from buffer to band

    CPLDebug("Viewranger PNG",
             "Copy_Tile_into_Block(%p %d x %d -> [%d %d)x[%d %d) "
             "%p) band %d",
             pbyPNGbuffer, nPNGwidth, nPNGheight, nLeftCol, nRightCol, nTopRow,
             nBottomRow, pImage, nBand);

    const int rowStartPixel =
        (nTopRow * std::max(nPNGwidth, nBlockXSize)) + nLeftCol;
    // Need to adjust if we have a short (underheight) tile.
    // werdna, 2020 July 09 done ? No.
    // What about underwide tiles/blocks ?

    // GByte *pGImage = &(((GByte *)(pImage))[rowStartPixel]);
    GByte *pGImage = (static_cast<GByte *>(pImage)) + rowStartPixel;
    CPLDebug("Viewranger PNG",
             "VRC band %d ovrvw %d nTopRow %d rowStartPixel %d", nBand,
             nThisOverview, nTopRow, rowStartPixel);

    if (nPNGheight < nBlockYSize)
    {
        if (nTopRow + nPNGheight > nBlockYSize)
        {
            CPLDebug("Viewranger PNG",
                     "band %d overview %d nTopRow %d +nPNGheight "
                     "%d > nRasterYSize %d",
                     nBand, nThisOverview, nTopRow, nPNGheight, nRasterYSize);
            // VSIFree(pbyPNGbuffer);
            // continue;
        }
    }

    CPLDebug("Viewranger PNG",
             "band %d overview %d copying to [%d %d) x [%d %d)", nBand,
             nThisOverview, nLeftCol, nRightCol, nTopRow, nBottomRow);

    const int nCopyStopRow = std::min(nPNGheight, nBlockYSize - nTopRow);

    if (nBottomRow != nCopyStopRow)
    {
        CPLDebug(
            "Viewranger PNG",
            "band %d overview %d nTopRow %d - nBottomRow %d != %d nCopyStopRow",
            nBand, nThisOverview, nTopRow, nBottomRow, nCopyStopRow);
    }

    for (int ii = 0; ii < nCopyStopRow; ii++)
    {

        // If nBlockXSize is not divisible by a sufficiently large
        // power of two then nPNGwidth*2^k may be slightly bigger
        // than nBlockXSize
        const int nCopyStopCol = std::min(nPNGwidth, nBlockXSize - nLeftCol);
        if (nLeftCol + nCopyStopCol != nRightCol)
        {
            CPLDebug("Viewranger PNG", "stopping at col %d of %d (%d-%d)",
                     nCopyStopCol, nBlockXSize, nLeftCol, nRightCol);
        }
        if (nBand == 4)
        {
            for (int jj = 0; jj < nCopyStopCol; jj++)
            {
                // pGImage[jj] = 255;  // Opposite of nVRCNoData;
            }
        }
        else
        {
            for (int jj = 0, jjj = nBand - 1; jj < nCopyStopCol; jj++, jjj += 3)
            {
                const unsigned char temp =
                    (pbyPNGbuffer + (3L * nPNGwidth * ii))[jjj];
                pGImage[jj] = temp;
            }
        }

        pGImage += nBlockXSize;
    }

    CPLDebug("Viewranger PNG",
             "copied PNG buffer %p %d x %d into pImage %p %d x %d",
             pbyPNGbuffer, nPNGwidth, nPNGheight, pImage, nRasterXSize,
             nRasterYSize);

    return 0;
}

int VRCRasterBand::Shrink_Tile_into_Block(GByte *pbyPNGbuffer, int nPNGwidth,
                                          int nPNGheight, int nLeftCol,
                                          int nRightCol, int nTopRow,
                                          int nBottomRow, void *pImage)
{
    CPLDebug("Viewranger PNG",
             "Shrink_Tile_into_Block(%p %d x %d -> [%d %d)x[%d %d) "
             "%p [%d %d) )",
             pbyPNGbuffer, nPNGwidth, nPNGheight, nLeftCol, nRightCol, nTopRow,
             nBottomRow, pImage, nBlockXSize, nBlockYSize);

    if (nTopRow < 0 || nTopRow >= nBlockYSize)
    {
        CPLDebug("Viewranger PNG",
                 "Shrink_Tile_into_Block: nTopRow %d not in [0,%d)", nTopRow,
                 nBlockYSize);
        // return -1;
    }
    if (nBottomRow < nTopRow || nBottomRow > nBlockYSize)
    {
        CPLDebug("Viewranger PNG",
                 "Shrink_Tile_into_Block: nBottomRow %d not in [%d,%d)",
                 nBottomRow, nTopRow, nBlockYSize);
        // return -1;
    }

    if (nLeftCol < 0 || nLeftCol >= nBlockXSize)
    {
        CPLDebug("Viewranger PNG",
                 "Shrink_Tile_into_Block: nLeftCol %d not in [0,%d)", nLeftCol,
                 nBlockXSize);
        // return -1;
    }
    if (nRightCol < nLeftCol || nRightCol > nBlockXSize)
    {
        CPLDebug("Viewranger PNG",
                 "Shrink_Tile_into_Block: nRightCol %d not in [%d,%d)",
                 nRightCol, nLeftCol, nBlockXSize);
        // return -1;
    }
    const int nCopyStartCol = std::max(0, nLeftCol);
    const int nCopyStartRow = std::max(0, nTopRow);
    // If nBlockXYSize is not divisible by a sufficiently large
    // power of two then nPNGwidthheight*2^k may be slightly bigger
    // than nBlockXYSize
    const int nCopyStopCol =
        std::min({nLeftCol + ((nPNGwidth + 1) / 2), nRightCol, nBlockYSize});
    const int nCopyStopRow =
        std::min(nTopRow + ((nPNGheight + 1) / 2), nBottomRow);

    const int nOutRowStartPixel = nCopyStartRow * nBlockXSize;
    // std::max((1+nPNGwidth)/2, nBlockXSize)
    // + nCopyStartCol;
    // Need to adjust if we have a short (underheight) tile.
    // werdna, 2020 July 09 done ? No.
    // What about underwide tiles/blocks ?
    CPLDebug("Viewranger PNG", "nOutRowStartPixel %d == %d * %d + %d",
             nOutRowStartPixel, nCopyStartRow, nBlockXSize, nCopyStartCol);
    CPLDebug("Viewranger PNG",
             "Shrink_Tile_into_Block: nOutRowStartPixel %d ii loops "
             "[%d/%d,%d/%d/%d)",
             nOutRowStartPixel, nTopRow, nCopyStartRow, nCopyStopRow,
             nBottomRow, nBlockYSize);
    CPLDebug("Viewranger PNG",
             "Shrink_Tile_into_Block: loopX-tile-adj missing jj loops "
             "[%d/%d,%d/%d/%d)",
             // need adjustment for loopX'th tile,
             nLeftCol, nCopyStartCol, nCopyStopCol, nRightCol, nBlockXSize);

    GByte *pGImage = static_cast<GByte *>(pImage) +
                     nOutRowStartPixel;  // need to adjust for loopX'th tile

    {
        const int i1 = 3 * nPNGwidth * 2 * (nBottomRow - 1 - nCopyStartRow);
        // const int i2=i1+3*nPNGwidth;
        const int jjj = (nBand - 1) + ((nCopyStopCol - 1 - nCopyStartCol) * 6);
        if (i1 + jjj > 3 * nPNGwidth * nPNGheight - 16)
        {
            CPLDebug("Viewranger PNG", "Band %d: i1 %d = 3 * %d * 2 * %d",
                     nBand, i1, nPNGwidth, nBottomRow - 1 - nCopyStartRow);
            CPLDebug("Viewranger PNG", "Band %d: jjj %d = %d + %d * 6", nBand,
                     jjj, nBand - 1, nCopyStopCol - 1 - nCopyStartCol);
            CPLDebug("Viewranger PNG",
                     "Band %d: Shrink_Tile_into_Block: "
                     "(i1+jjj %d+%d=%d) - 6*%d*%d = %d",
                     nBand, i1, jjj, i1 + jjj, nPNGwidth, nPNGheight,
                     (i1 + jjj) - (6 * nPNGwidth * nPNGheight));
        }
    }

    for (int ii = nCopyStartRow; ii < nCopyStopRow;  // nBottomRow;
         ii++)
    {

        if (nBand == 4)
        {
            for (int jj = 0; jj < nCopyStopCol; jj++)
            {
                // pGImage[jj] = 255;  // Opposite of nVRCNoData;
            }
        }
        else
        {
            const int i1 = 3 * nPNGwidth * 2 * (ii - nCopyStartRow);
            const int i2 = i1 + (3 * nPNGwidth);
            for (int jj = nCopyStartCol, jjj = nBand - 1; jj < nCopyStopCol;
                 jj++, jjj += 6)
            {
                uint16_t temp = (pbyPNGbuffer + i1)[jjj];
                temp += (pbyPNGbuffer + i2)[jjj];
                temp += (pbyPNGbuffer + i1)[jjj + 3];
                temp += (pbyPNGbuffer + i2)[jjj + 3];

                pGImage[jj] = static_cast<GByte>(temp >> 2);
            }
        }
        pGImage += nBlockXSize;
    }

    CPLDebug("Viewranger PNG",
             "shrunk PNG buffer %p %d x %d into pImage %p %d x %d "
             "within %d x %d",
             pbyPNGbuffer, nPNGwidth, nPNGheight, pImage, nBlockXSize,
             nBlockYSize, nRasterXSize, nRasterYSize);

    return 0;
}

//  #endif
