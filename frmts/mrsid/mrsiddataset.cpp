/******************************************************************************
 *
 * Project:  Multi-resolution Seamless Image Database (MrSID)
 * Purpose:  Read/write LizardTech's MrSID file format - Version 4+ SDK.
 * Author:   Andrey Kiselev, dron@ak4719.spb.edu
 *
 ******************************************************************************
 * Copyright (c) 2003, Andrey Kiselev <dron@ak4719.spb.edu>
 * Copyright (c) 2007-2013, Even Rouault <even dot rouault at spatialys.com>
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#define NO_DELETE

#include "cpl_string.h"
#include "gdal_frmts.h"
#include "gdaljp2abstractdataset.h"
#include "gdaljp2metadata.h"
#include "ogr_spatialref.h"
#include <string>

#include "mrsiddrivercore.h"

#include <geo_normalize.h>
#include <geovalues.h>

CPL_C_START
double GTIFAngleToDD(double dfAngle, int nUOMAngle);
void CPL_DLL LibgeotiffOneTimeInit();
CPL_C_END

#include "mrsiddataset_headers_include.h"

#ifdef MRSID_POST5
#define MRSID_HAVE_GETWKT
#endif

/* getTotalBandData is deprecated by getBandData, at least starting with 8.5 */
#if defined(LTI_SDK_MAJOR) &&                                                  \
    (LTI_SDK_MAJOR > 8 || (LTI_SDK_MAJOR >= 8 && LTI_SDK_MINOR >= 5))
#define myGetTotalBandData getBandData
#else
#define myGetTotalBandData getTotalBandData
#endif

#include "mrsidstream.h"

using namespace LizardTech;

/* -------------------------------------------------------------------- */
/*      Various wrapper templates used to force new/delete to happen    */
/*      in the same heap.  See bug 1213 and MSDN knowledge base         */
/*      article 122675.                                                 */
/* -------------------------------------------------------------------- */

template <class T> class LTIDLLPixel : public T
{
  public:
    LTIDLLPixel(LTIColorSpace colorSpace, lt_uint16 numBands,
                LTIDataType dataType)
        : T(colorSpace, numBands, dataType)
    {
    }

    virtual ~LTIDLLPixel()
    {
    }
};

template <class T> class LTIDLLReader : public T
{
  public:
    explicit LTIDLLReader(const LTFileSpec &fileSpec, bool useWorldFile = false)
        : T(fileSpec, useWorldFile)
    {
    }

    explicit LTIDLLReader(LTIOStreamInf &oStream, bool useWorldFile = false)
        : T(oStream, useWorldFile)
    {
    }

    explicit LTIDLLReader(LTIOStreamInf *poStream,
                          LTIOStreamInf *poWorldFile = nullptr)
        : T(poStream, poWorldFile)
    {
    }

    virtual ~LTIDLLReader()
    {
    }
};

template <class T> class LTIDLLNavigator : public T
{
  public:
    explicit LTIDLLNavigator(const LTIImage &image) : T(image)
    {
    }

    virtual ~LTIDLLNavigator()
    {
    }
};

template <class T> class LTIDLLBuffer : public T
{
  public:
    LTIDLLBuffer(const LTIPixel &pixelProps, lt_uint32 totalNumCols,
                 lt_uint32 totalNumRows, void **data)
        : T(pixelProps, totalNumCols, totalNumRows, data)
    {
    }

    virtual ~LTIDLLBuffer()
    {
    }
};

template <class T> class LTIDLLCopy : public T
{
  public:
    explicit LTIDLLCopy(const T &original) : T(original)
    {
    }

    virtual ~LTIDLLCopy()
    {
    }
};

template <class T> class LTIDLLWriter : public T
{
  public:
    explicit LTIDLLWriter(LTIImageStage *image) : T(image)
    {
    }

    virtual ~LTIDLLWriter()
    {
    }
};

template <class T> class LTIDLLDefault : public T
{
  public:
    LTIDLLDefault() : T()
    {
    }

    virtual ~LTIDLLDefault()
    {
    }
};

/* -------------------------------------------------------------------- */
/*      Interface to MrSID SDK progress reporting.                      */
/* -------------------------------------------------------------------- */

class MrSIDProgress : public LTIProgressDelegate
{
  public:
    MrSIDProgress(GDALProgressFunc f, void *arg) : m_f(f), m_arg(arg)
    {
    }

    virtual ~MrSIDProgress()
    {
    }

    virtual LT_STATUS setProgressStatus(float fraction) override
    {
        if (!m_f)
            return LT_STS_BadContext;
        if (!m_f(fraction, nullptr, m_arg))
            return LT_STS_Failure;
        return LT_STS_Success;
    }

  private:
    GDALProgressFunc m_f;
    void *m_arg;
};

/************************************************************************/
/* ==================================================================== */
/*                              MrSIDDataset                            */
/* ==================================================================== */
/************************************************************************/

class MrSIDDataset final : public GDALJP2AbstractDataset
{
    friend class MrSIDRasterBand;

    LTIOStreamInf *poStream;
    LTIOFileStream oLTIStream;
    LTIVSIStream oVSIStream;

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 7
    LTIImageFilter *poImageReader;
#else
    LTIImageReader *poImageReader;
#endif

#ifdef MRSID_ESDK
    LTIGeoFileImageWriter *poImageWriter;
#endif

    LTIDLLNavigator<LTINavigator> *poLTINav;
    LTIDLLCopy<LTIMetadataDatabase> *poMetadata;
    const LTIPixel *poNDPixel;

    LTIDLLBuffer<LTISceneBuffer> *poBuffer;
    int nBlockXSize;
    int nBlockYSize;
    int bPrevBlockRead;
    int nPrevBlockXOff, nPrevBlockYOff;

    LTIDataType eSampleType;
    GDALDataType eDataType;
    LTIColorSpace eColorSpace;

    double dfCurrentMag;

    GTIFDefn *psDefn;

    MrSIDDataset *poParentDS;
    int bIsOverview;
    int nOverviewCount;
    MrSIDDataset **papoOverviewDS;

    CPLString osMETFilename;

    CPLErr OpenZoomLevel(lt_int32 iZoom);
    int GetMetadataElement(const char *, void *, int = 0);
    void FetchProjParams();
    void GetGTIFDefn();
    char *GetOGISDefn(GTIFDefn *);

    int m_nInRasterIO = 0;  // Prevent infinite recursion in IRasterIO()

    virtual CPLErr IRasterIO(GDALRWFlag, int, int, int, int, void *, int, int,
                             GDALDataType, int, BANDMAP_TYPE,
                             GSpacing nPixelSpace, GSpacing nLineSpace,
                             GSpacing nBandSpace,
                             GDALRasterIOExtraArg *psExtraArg) override;

  protected:
    virtual int CloseDependentDatasets() override;

    virtual CPLErr IBuildOverviews(const char *, int, const int *, int,
                                   const int *, GDALProgressFunc, void *,
                                   CSLConstList papszOptions) override;

  public:
    explicit MrSIDDataset(int bIsJPEG2000);
    ~MrSIDDataset();

    static GDALDataset *Open(GDALOpenInfo *poOpenInfo, int bIsJP2);

    virtual char **GetFileList() override;

#ifdef MRSID_ESDK
    static GDALDataset *Create(const char *pszFilename, int nXSize, int nYSize,
                               int nBands, GDALDataType eType,
                               char **papszParamList);
#endif
};

/************************************************************************/
/* ==================================================================== */
/*                           MrSIDRasterBand                            */
/* ==================================================================== */
/************************************************************************/

class MrSIDRasterBand final : public GDALPamRasterBand
{
    friend class MrSIDDataset;

    LTIPixel *poPixel;

    int nBlockSize;

    int bNoDataSet;
    double dfNoDataValue;

    MrSIDDataset *poGDS;

    GDALColorInterp eBandInterp;

  public:
    MrSIDRasterBand(MrSIDDataset *, int);
    ~MrSIDRasterBand();

    virtual CPLErr IRasterIO(GDALRWFlag, int, int, int, int, void *, int, int,
                             GDALDataType, GSpacing nPixelSpace,
                             GSpacing nLineSpace,
                             GDALRasterIOExtraArg *psExtraArg) override;

    virtual CPLErr IReadBlock(int, int, void *) override;
    virtual GDALColorInterp GetColorInterpretation() override;
    CPLErr SetColorInterpretation(GDALColorInterp eNewInterp) override;
    virtual double GetNoDataValue(int *) override;
    virtual int GetOverviewCount() override;
    virtual GDALRasterBand *GetOverview(int) override;

    virtual CPLErr GetStatistics(int bApproxOK, int bForce, double *pdfMin,
                                 double *pdfMax, double *pdfMean,
                                 double *pdfStdDev) override;

#ifdef MRSID_ESDK
    virtual CPLErr IWriteBlock(int, int, void *) override;
#endif
};

/************************************************************************/
/*                           MrSIDRasterBand()                          */
/************************************************************************/

MrSIDRasterBand::MrSIDRasterBand(MrSIDDataset *poDSIn, int nBandIn)
{
    this->poDS = poDSIn;
    poGDS = poDSIn;
    this->nBand = nBandIn;
    this->eDataType = poDSIn->eDataType;

    /* -------------------------------------------------------------------- */
    /*      Set the block sizes and buffer parameters.                      */
    /* -------------------------------------------------------------------- */
    nBlockXSize = poDSIn->nBlockXSize;
    nBlockYSize = poDSIn->nBlockYSize;
    // #ifdef notdef
    if (poDS->GetRasterXSize() > 2048)
        nBlockXSize = 1024;
    if (poDS->GetRasterYSize() > 128)
        nBlockYSize = 128;
    else
        nBlockYSize = poDS->GetRasterYSize();
    // #endif

    nBlockSize = nBlockXSize * nBlockYSize;
    poPixel = new LTIDLLPixel<LTIPixel>(poDSIn->eColorSpace,
                                        static_cast<lt_uint16>(poDSIn->nBands),
                                        poDSIn->eSampleType);

/* -------------------------------------------------------------------- */
/*      Set NoData values.                                              */
/*                                                                      */
/*      This logic is disabled for now since the MrSID nodata           */
/*      semantics are different than GDAL.  In MrSID all bands must     */
/*      match the nodata value for that band in order for the pixel     */
/*      to be considered nodata, otherwise all values are valid.        */
/* -------------------------------------------------------------------- */
#ifdef notdef
    if (poDS->poNDPixel)
    {
        switch (poDS->eSampleType)
        {
            case LTI_DATATYPE_UINT8:
            case LTI_DATATYPE_SINT8:
                dfNoDataValue =
                    (double)poDS->poNDPixel->getSampleValueUint8(nBand - 1);
                break;
            case LTI_DATATYPE_UINT16:
                dfNoDataValue =
                    (double)poDS->poNDPixel->getSampleValueUint16(nBand - 1);
                break;
            case LTI_DATATYPE_FLOAT32:
                dfNoDataValue =
                    poDS->poNDPixel->getSampleValueFloat32(nBand - 1);
                break;
            case LTI_DATATYPE_SINT16:
                dfNoDataValue =
                    (double)*(GInt16 *)poDS->poNDPixel->getSampleValueAddr(
                        nBand - 1);
                break;
            case LTI_DATATYPE_UINT32:
                dfNoDataValue =
                    (double)*(GUInt32 *)poDS->poNDPixel->getSampleValueAddr(
                        nBand - 1);
                break;
            case LTI_DATATYPE_SINT32:
                dfNoDataValue =
                    (double)*(GInt32 *)poDS->poNDPixel->getSampleValueAddr(
                        nBand - 1);
                break;
            case LTI_DATATYPE_FLOAT64:
                dfNoDataValue =
                    *(double *)poDS->poNDPixel->getSampleValueAddr(nBand - 1);
                break;

            case LTI_DATATYPE_INVALID:
                CPLAssert(false);
                break;
        }
        bNoDataSet = TRUE;
    }
    else
#endif
    {
        dfNoDataValue = 0.0;
        bNoDataSet = FALSE;
    }

    switch (poGDS->eColorSpace)
    {
        case LTI_COLORSPACE_RGB:
            if (nBand == 1)
                eBandInterp = GCI_RedBand;
            else if (nBand == 2)
                eBandInterp = GCI_GreenBand;
            else if (nBand == 3)
                eBandInterp = GCI_BlueBand;
            else
                eBandInterp = GCI_Undefined;
            break;

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
        case LTI_COLORSPACE_RGBA:
            if (nBand == 1)
                eBandInterp = GCI_RedBand;
            else if (nBand == 2)
                eBandInterp = GCI_GreenBand;
            else if (nBand == 3)
                eBandInterp = GCI_BlueBand;
            else if (nBand == 4)
                eBandInterp = GCI_AlphaBand;
            else
                eBandInterp = GCI_Undefined;
            break;
#endif

        case LTI_COLORSPACE_CMYK:
            if (nBand == 1)
                eBandInterp = GCI_CyanBand;
            else if (nBand == 2)
                eBandInterp = GCI_MagentaBand;
            else if (nBand == 3)
                eBandInterp = GCI_YellowBand;
            else if (nBand == 4)
                eBandInterp = GCI_BlackBand;
            else
                eBandInterp = GCI_Undefined;
            break;

        case LTI_COLORSPACE_GRAYSCALE:
            eBandInterp = GCI_GrayIndex;
            break;

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
        case LTI_COLORSPACE_GRAYSCALEA:
            if (nBand == 1)
                eBandInterp = GCI_GrayIndex;
            else if (nBand == 2)
                eBandInterp = GCI_AlphaBand;
            else
                eBandInterp = GCI_Undefined;
            break;

        case LTI_COLORSPACE_GRAYSCALEA_PM:
            if (nBand == 1)
                eBandInterp = GCI_GrayIndex;
            else if (nBand == 2)
                eBandInterp = GCI_AlphaBand;
            else
                eBandInterp = GCI_Undefined;
            break;
#endif

        default:
            eBandInterp = GCI_Undefined;
            break;
    }
}

/************************************************************************/
/*                            ~MrSIDRasterBand()                        */
/************************************************************************/

MrSIDRasterBand::~MrSIDRasterBand()
{
    if (poPixel)
        delete poPixel;
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr MrSIDRasterBand::IReadBlock(int nBlockXOff, int nBlockYOff, void *pImage)
{
#ifdef MRSID_ESDK
    if (poGDS->eAccess == GA_Update)
    {
        CPLDebug("MrSID",
                 "IReadBlock() - DSDK - read on updatable file fails.");
        memset(pImage, 0,
               static_cast<size_t>(nBlockSize) *
                   GDALGetDataTypeSizeBytes(eDataType));
        return CE_None;
    }
#endif /* MRSID_ESDK */

    CPLDebug("MrSID", "IReadBlock(%d,%d)", nBlockXOff, nBlockYOff);

    if (!poGDS->bPrevBlockRead || poGDS->nPrevBlockXOff != nBlockXOff ||
        poGDS->nPrevBlockYOff != nBlockYOff)
    {
        GInt32 nLine = nBlockYOff * nBlockYSize;
        GInt32 nCol = nBlockXOff * nBlockXSize;

        // XXX: The scene, passed to LTIImageStage::read() call must be
        // inside the image boundaries. So we should detect the last strip and
        // form the scene properly.
        CPLDebug("MrSID", "IReadBlock - read() %dx%d block at %d,%d.",
                 nBlockXSize, nBlockYSize, nCol, nLine);

        if (!LT_SUCCESS(poGDS->poLTINav->setSceneAsULWH(
                nCol, nLine,
                (nCol + nBlockXSize > poGDS->GetRasterXSize())
                    ? (poGDS->GetRasterXSize() - nCol)
                    : nBlockXSize,
                (nLine + nBlockYSize > poGDS->GetRasterYSize())
                    ? (poGDS->GetRasterYSize() - nLine)
                    : nBlockYSize,
                poGDS->dfCurrentMag)))
        {
            CPLError(
                CE_Failure, CPLE_AppDefined,
                "MrSIDRasterBand::IReadBlock(): Failed to set scene position.");
            return CE_Failure;
        }

        if (!poGDS->poBuffer)
        {
            poGDS->poBuffer = new LTIDLLBuffer<LTISceneBuffer>(
                *poPixel, nBlockXSize, nBlockYSize, nullptr);
            //            poGDS->poBuffer =
            //                new LTISceneBuffer( *poPixel, nBlockXSize,
            //                nBlockYSize, nullptr );
        }

        if (!LT_SUCCESS(poGDS->poImageReader->read(poGDS->poLTINav->getScene(),
                                                   *poGDS->poBuffer)))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "MrSIDRasterBand::IReadBlock(): Failed to load image.");
            return CE_Failure;
        }

        poGDS->bPrevBlockRead = TRUE;
        poGDS->nPrevBlockXOff = nBlockXOff;
        poGDS->nPrevBlockYOff = nBlockYOff;
    }

    memcpy(
        pImage,
        poGDS->poBuffer->myGetTotalBandData(static_cast<lt_uint16>(nBand - 1)),
        nBlockSize * GDALGetDataTypeSizeBytes(poGDS->eDataType));

    return CE_None;
}

#ifdef MRSID_ESDK

/************************************************************************/
/*                            IWriteBlock()                             */
/************************************************************************/

CPLErr MrSIDRasterBand::IWriteBlock(int nBlockXOff, int nBlockYOff,
                                    void *pImage)
{
    CPLAssert(poGDS != nullptr && nBlockXOff >= 0 && nBlockYOff >= 0 &&
              pImage != nullptr);

#ifdef DEBUG
    CPLDebug("MrSID", "IWriteBlock(): nBlockXOff=%d, nBlockYOff=%d", nBlockXOff,
             nBlockYOff);
#endif

    LTIScene oScene(nBlockXOff * nBlockXSize, nBlockYOff * nBlockYSize,
                    nBlockXSize, nBlockYSize, 1.0);
    LTISceneBuffer oSceneBuf(*poPixel, poGDS->nBlockXSize, poGDS->nBlockYSize,
                             &pImage);

    if (!LT_SUCCESS(poGDS->poImageWriter->writeBegin(oScene)))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDRasterBand::IWriteBlock(): writeBegin failed.");
        return CE_Failure;
    }

    if (!LT_SUCCESS(poGDS->poImageWriter->writeStrip(oSceneBuf, oScene)))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDRasterBand::IWriteBlock(): writeStrip failed.");
        return CE_Failure;
    }

    if (!LT_SUCCESS(poGDS->poImageWriter->writeEnd()))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDRasterBand::IWriteBlock(): writeEnd failed.");
        return CE_Failure;
    }

    return CE_None;
}

#endif /* MRSID_ESDK */

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr MrSIDRasterBand::IRasterIO(GDALRWFlag eRWFlag, int nXOff, int nYOff,
                                  int nXSize, int nYSize, void *pData,
                                  int nBufXSize, int nBufYSize,
                                  GDALDataType eBufType, GSpacing nPixelSpace,
                                  GSpacing nLineSpace,
                                  GDALRasterIOExtraArg *psExtraArg)

{
    /* -------------------------------------------------------------------- */
    /*      Fallback to default implementation if the whole scanline        */
    /*      without subsampling requested.                                  */
    /* -------------------------------------------------------------------- */
    if (nXSize == poGDS->GetRasterXSize() && nXSize == nBufXSize &&
        nYSize == nBufYSize)
    {
        return GDALRasterBand::IRasterIO(eRWFlag, nXOff, nYOff, nXSize, nYSize,
                                         pData, nBufXSize, nBufYSize, eBufType,
                                         nPixelSpace, nLineSpace, psExtraArg);
    }

    /* -------------------------------------------------------------------- */
    /*      Handle via the dataset level IRasterIO()                        */
    /* -------------------------------------------------------------------- */
    return poGDS->IRasterIO(eRWFlag, nXOff, nYOff, nXSize, nYSize, pData,
                            nBufXSize, nBufYSize, eBufType, 1, &nBand,
                            nPixelSpace, nLineSpace, 0, psExtraArg);
}

/************************************************************************/
/*                       GetColorInterpretation()                       */
/************************************************************************/

GDALColorInterp MrSIDRasterBand::GetColorInterpretation()

{
    return eBandInterp;
}

/************************************************************************/
/*                       SetColorInterpretation()                       */
/*                                                                      */
/*      This would normally just be used by folks using the MrSID code  */
/*      to read JP2 streams in other formats (such as NITF) and         */
/*      providing their own color interpretation regardless of what     */
/*      MrSID might think the stream itself says.                       */
/************************************************************************/

CPLErr MrSIDRasterBand::SetColorInterpretation(GDALColorInterp eNewInterp)

{
    eBandInterp = eNewInterp;

    return CE_None;
}

/************************************************************************/
/*                           GetStatistics()                            */
/*                                                                      */
/*      We override this method so that we can force generation of      */
/*      statistics if approx ok is true since we know that a small      */
/*      overview is always available, and that computing statistics     */
/*      from it is very fast.                                           */
/************************************************************************/

CPLErr MrSIDRasterBand::GetStatistics(int bApproxOK, int bForce, double *pdfMin,
                                      double *pdfMax, double *pdfMean,
                                      double *pdfStdDev)

{
    if (bApproxOK)
        bForce = TRUE;

    return GDALPamRasterBand::GetStatistics(bApproxOK, bForce, pdfMin, pdfMax,
                                            pdfMean, pdfStdDev);
}

/************************************************************************/
/*                           GetNoDataValue()                           */
/************************************************************************/

double MrSIDRasterBand::GetNoDataValue(int *pbSuccess)

{
    if (bNoDataSet)
    {
        if (pbSuccess)
            *pbSuccess = bNoDataSet;

        return dfNoDataValue;
    }

    return GDALPamRasterBand::GetNoDataValue(pbSuccess);
}

/************************************************************************/
/*                          GetOverviewCount()                          */
/************************************************************************/

int MrSIDRasterBand::GetOverviewCount()

{
    return poGDS->nOverviewCount;
}

/************************************************************************/
/*                            GetOverview()                             */
/************************************************************************/

GDALRasterBand *MrSIDRasterBand::GetOverview(int i)

{
    if (i < 0 || i >= poGDS->nOverviewCount)
        return nullptr;
    else
        return poGDS->papoOverviewDS[i]->GetRasterBand(nBand);
}

/************************************************************************/
/*                           MrSIDDataset()                             */
/************************************************************************/

MrSIDDataset::MrSIDDataset(int bIsJPEG2000)
    : nBlockXSize(0), nBlockYSize(0), eSampleType(LTI_DATATYPE_UINT8),
      eDataType(GDT_Byte), eColorSpace(LTI_COLORSPACE_INVALID)
{
    poStream = nullptr;
    poImageReader = nullptr;
#ifdef MRSID_ESDK
    poImageWriter = nullptr;
#endif
    poLTINav = nullptr;
    poMetadata = nullptr;
    poNDPixel = nullptr;
    nBands = 0;

    poBuffer = nullptr;
    bPrevBlockRead = FALSE;
    nPrevBlockXOff = 0;
    nPrevBlockYOff = 0;

    psDefn = nullptr;

    dfCurrentMag = 1.0;
    bIsOverview = FALSE;
    poParentDS = this;
    nOverviewCount = 0;
    papoOverviewDS = nullptr;

    poDriver =
        (GDALDriver *)GDALGetDriverByName(bIsJPEG2000 ? "JP2MrSID" : "MrSID");
}

/************************************************************************/
/*                            ~MrSIDDataset()                           */
/************************************************************************/

MrSIDDataset::~MrSIDDataset()
{
    MrSIDDataset::FlushCache(true);

#ifdef MRSID_ESDK
    if (poImageWriter)
        delete poImageWriter;
#endif

    if (poBuffer)
        delete poBuffer;
    if (poMetadata)
        delete poMetadata;
    if (poLTINav)
        delete poLTINav;
    if (poImageReader && !bIsOverview)
#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 7
    {
        poImageReader->release();
        poImageReader = nullptr;
    }
#else
        delete poImageReader;
#endif
    // points to another member, don't delete
    poStream = nullptr;

    if (psDefn)
        delete psDefn;
    MrSIDDataset::CloseDependentDatasets();
}

/************************************************************************/
/*                      CloseDependentDatasets()                        */
/************************************************************************/

int MrSIDDataset::CloseDependentDatasets()
{
    int bRet = GDALPamDataset::CloseDependentDatasets();

    if (papoOverviewDS)
    {
        for (int i = 0; i < nOverviewCount; i++)
            delete papoOverviewDS[i];
        CPLFree(papoOverviewDS);
        papoOverviewDS = nullptr;
        bRet = TRUE;
    }
    return bRet;
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr MrSIDDataset::IRasterIO(GDALRWFlag eRWFlag, int nXOff, int nYOff,
                               int nXSize, int nYSize, void *pData,
                               int nBufXSize, int nBufYSize,
                               GDALDataType eBufType, int nBandCount,
                               BANDMAP_TYPE panBandMap, GSpacing nPixelSpace,
                               GSpacing nLineSpace, GSpacing nBandSpace,
                               GDALRasterIOExtraArg *psExtraArg)

{
    /* -------------------------------------------------------------------- */
    /*      We need various criteria to skip out to block based methods.    */
    /* -------------------------------------------------------------------- */
    int bUseBlockedIO = bForceCachedIO;

    if (nYSize == 1 || nXSize * ((double)nYSize) < 100.0)
        bUseBlockedIO = TRUE;

    if (nBufYSize == 1 || nBufXSize * ((double)nBufYSize) < 100.0)
        bUseBlockedIO = TRUE;

    if (CPLTestBool(CPLGetConfigOption("GDAL_ONE_BIG_READ", "NO")))
        bUseBlockedIO = FALSE;

    if (bUseBlockedIO && !m_nInRasterIO)
    {
        ++m_nInRasterIO;
        const CPLErr eErr = GDALDataset::BlockBasedRasterIO(
            eRWFlag, nXOff, nYOff, nXSize, nYSize, pData, nBufXSize, nBufYSize,
            eBufType, nBandCount, panBandMap, nPixelSpace, nLineSpace,
            nBandSpace, psExtraArg);
        --m_nInRasterIO;
        return eErr;
    }

    CPLDebug("MrSID", "RasterIO() - using optimized dataset level IO.");

    /* -------------------------------------------------------------------- */
    /*      What is our requested window relative to the base dataset.      */
    /*      We want to operate from here on as if we were operating on      */
    /*      the full res band.                                              */
    /* -------------------------------------------------------------------- */
    int nZoomMag = (int)((1 / dfCurrentMag) * 1.0000001);

    nXOff *= nZoomMag;
    nYOff *= nZoomMag;
    nXSize *= nZoomMag;
    nYSize *= nZoomMag;

    /* -------------------------------------------------------------------- */
    /*      We need to figure out the best zoom level to use for this       */
    /*      request.  We apply a small fudge factor to make sure that       */
    /*      request just very, very slightly larger than a zoom level do    */
    /*      not force us to the next level.                                 */
    /* -------------------------------------------------------------------- */
    int iOverview = 0;
    double dfZoomMag =
        MIN((nXSize / (double)nBufXSize), (nYSize / (double)nBufYSize));

    for (nZoomMag = 1; nZoomMag * 2 < (dfZoomMag + 0.1) &&
                       iOverview < poParentDS->nOverviewCount;
         nZoomMag *= 2, iOverview++)
    {
    }

    /* -------------------------------------------------------------------- */
    /*      Work out the size of the temporary buffer and allocate it.      */
    /*      The temporary buffer will generally be at a moderately          */
    /*      higher resolution than the buffer of data requested.            */
    /* -------------------------------------------------------------------- */
    int nTmpPixelSize;
    LTIPixel oPixel(eColorSpace, static_cast<lt_uint16>(nBands), eSampleType);

    LT_STATUS eLTStatus;
    unsigned int maxWidth;
    unsigned int maxHeight;

    eLTStatus =
        poImageReader->getDimsAtMag(1.0 / nZoomMag, maxWidth, maxHeight);

    if (!LT_SUCCESS(eLTStatus))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDDataset::IRasterIO(): Failed to get zoomed image "
                 "dimensions.\n%s",
                 getLastStatusString(eLTStatus));
        return CE_Failure;
    }

    int maxWidthAtL0 =
        bIsOverview ? poParentDS->GetRasterXSize() : this->GetRasterXSize();
    int maxHeightAtL0 =
        bIsOverview ? poParentDS->GetRasterYSize() : this->GetRasterYSize();

    int sceneUlXOff = nXOff / nZoomMag;
    int sceneUlYOff = nYOff / nZoomMag;
    int sceneWidth =
        (int)(nXSize * (double)maxWidth / (double)maxWidthAtL0 + 0.99);
    int sceneHeight =
        (int)(nYSize * (double)maxHeight / (double)maxHeightAtL0 + 0.99);

    if ((sceneUlXOff + sceneWidth) > (int)maxWidth)
        sceneWidth = maxWidth - sceneUlXOff;

    if ((sceneUlYOff + sceneHeight) > (int)maxHeight)
        sceneHeight = maxHeight - sceneUlYOff;

    LTISceneBuffer oLTIBuffer(oPixel, sceneWidth, sceneHeight, nullptr);

    nTmpPixelSize = GDALGetDataTypeSizeBytes(eDataType);

    /* -------------------------------------------------------------------- */
    /*      Create navigator, and move to the requested scene area.         */
    /* -------------------------------------------------------------------- */
    LTINavigator oNav(*poImageReader);

    if (!LT_SUCCESS(oNav.setSceneAsULWH(sceneUlXOff, sceneUlYOff, sceneWidth,
                                        sceneHeight, 1.0 / nZoomMag)))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDDataset::IRasterIO(): Failed to set scene position.");

        return CE_Failure;
    }

    CPLDebug("MrSID",
             "Dataset:IRasterIO(%d,%d %dx%d -> %dx%d -> %dx%d, zoom=%d)", nXOff,
             nYOff, nXSize, nYSize, sceneWidth, sceneHeight, nBufXSize,
             nBufYSize, nZoomMag);

    if (!oNav.isSceneValid())
        CPLDebug("MrSID", "LTINavigator in invalid state.");

    /* -------------------------------------------------------------------- */
    /*      Read into the buffer.                                           */
    /* -------------------------------------------------------------------- */

    eLTStatus = poImageReader->read(oNav.getScene(), oLTIBuffer);
    if (!LT_SUCCESS(eLTStatus))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDRasterBand::IRasterIO(): Failed to load image.\n%s",
                 getLastStatusString(eLTStatus));
        return CE_Failure;
    }

    /* -------------------------------------------------------------------- */
    /*      If we are pulling the data at a matching resolution, try to     */
    /*      do a more direct copy without subsampling.                      */
    /* -------------------------------------------------------------------- */
    int iBufLine, iBufPixel;

    if (nBufXSize == sceneWidth && nBufYSize == sceneHeight)
    {
        for (int iBand = 0; iBand < nBandCount; iBand++)
        {
            GByte *pabySrcBand = (GByte *)oLTIBuffer.myGetTotalBandData(
                static_cast<lt_uint16>(panBandMap[iBand] - 1));

            for (int iLine = 0; iLine < nBufYSize; iLine++)
            {
                GDALCopyWords(
                    pabySrcBand + iLine * nTmpPixelSize * sceneWidth, eDataType,
                    nTmpPixelSize,
                    ((GByte *)pData) + iLine * nLineSpace + iBand * nBandSpace,
                    eBufType, static_cast<int>(nPixelSpace), nBufXSize);
            }
        }
    }

    /* -------------------------------------------------------------------- */
    /*      Manually resample to our target buffer.                         */
    /* -------------------------------------------------------------------- */
    else
    {
        for (iBufLine = 0; iBufLine < nBufYSize; iBufLine++)
        {
            int iTmpLine =
                (int)floor(((iBufLine + 0.5) / nBufYSize) * sceneHeight);

            for (iBufPixel = 0; iBufPixel < nBufXSize; iBufPixel++)
            {
                int iTmpPixel =
                    (int)floor(((iBufPixel + 0.5) / nBufXSize) * sceneWidth);

                for (int iBand = 0; iBand < nBandCount; iBand++)
                {
                    GByte *pabySrc, *pabyDst;

                    pabyDst = ((GByte *)pData) + nPixelSpace * iBufPixel +
                              nLineSpace * iBufLine + nBandSpace * iBand;

                    pabySrc = (GByte *)oLTIBuffer.myGetTotalBandData(
                        static_cast<lt_uint16>(panBandMap[iBand] - 1));
                    pabySrc +=
                        (iTmpLine * sceneWidth + iTmpPixel) * nTmpPixelSize;

                    if (eDataType == eBufType)
                        memcpy(pabyDst, pabySrc, nTmpPixelSize);
                    else
                        GDALCopyWords(pabySrc, eDataType, 0, pabyDst, eBufType,
                                      0, 1);
                }
            }
        }
    }

    return CE_None;
}

/************************************************************************/
/*                          IBuildOverviews()                           */
/************************************************************************/

CPLErr MrSIDDataset::IBuildOverviews(const char *, int, const int *, int,
                                     const int *, GDALProgressFunc, void *,
                                     CSLConstList)
{
    CPLError(CE_Warning, CPLE_AppDefined,
             "MrSID overviews are built-in, so building external "
             "overviews is unnecessary. Ignoring.\n");

    return CE_None;
}

/************************************************************************/
/*                        SerializeMetadataRec()                        */
/************************************************************************/

static CPLString SerializeMetadataRec(const LTIMetadataRecord *poMetadataRec)
{
    GUInt32 iNumDims = 0;
    const GUInt32 *paiDims = nullptr;
    const void *pData = poMetadataRec->getArrayData(iNumDims, paiDims);
    CPLString osMetadata;
    GUInt32 k = 0;

    for (GUInt32 i = 0; paiDims != nullptr && i < iNumDims; i++)
    {
        // stops on large binary data
        if (poMetadataRec->getDataType() == LTI_METADATA_DATATYPE_UINT8 &&
            paiDims[i] > 1024)
            return CPLString();

        for (GUInt32 j = 0; j < paiDims[i]; j++)
        {
            CPLString osTemp;

            switch (poMetadataRec->getDataType())
            {
                case LTI_METADATA_DATATYPE_UINT8:
                case LTI_METADATA_DATATYPE_SINT8:
                    osTemp.Printf("%d", ((GByte *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_UINT16:
                    osTemp.Printf("%u", ((GUInt16 *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_SINT16:
                    osTemp.Printf("%d", ((GInt16 *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_UINT32:
                    osTemp.Printf("%u", ((GUInt32 *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_SINT32:
                    osTemp.Printf("%d", ((GInt32 *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_FLOAT32:
                    osTemp.Printf("%f", ((float *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_FLOAT64:
                    osTemp.Printf("%f", ((double *)pData)[k++]);
                    break;
                case LTI_METADATA_DATATYPE_ASCII:
                    osTemp = ((const char **)pData)[k++];
                    break;
                default:
                    break;
            }

            if (!osMetadata.empty())
                osMetadata += ',';
            osMetadata += osTemp;
        }
    }

    return osMetadata;
}

/************************************************************************/
/*                          GetMetadataElement()                        */
/************************************************************************/

int MrSIDDataset::GetMetadataElement(const char *pszKey, void *pValue,
                                     int iLength)
{
    if (!poMetadata->has(pszKey))
        return FALSE;

    const LTIMetadataRecord *poMetadataRec = nullptr;
    poMetadata->get(pszKey, poMetadataRec);

    if (poMetadataRec == nullptr || !poMetadataRec->isScalar())
        return FALSE;

    // XXX: return FALSE if we have more than one element in metadata record
    int iSize;
    switch (poMetadataRec->getDataType())
    {
        case LTI_METADATA_DATATYPE_UINT8:
        case LTI_METADATA_DATATYPE_SINT8:
            iSize = 1;
            break;
        case LTI_METADATA_DATATYPE_UINT16:
        case LTI_METADATA_DATATYPE_SINT16:
            iSize = 2;
            break;
        case LTI_METADATA_DATATYPE_UINT32:
        case LTI_METADATA_DATATYPE_SINT32:
        case LTI_METADATA_DATATYPE_FLOAT32:
            iSize = 4;
            break;
        case LTI_METADATA_DATATYPE_FLOAT64:
            iSize = 8;
            break;
        case LTI_METADATA_DATATYPE_ASCII:
            iSize = iLength;
            break;
        default:
            iSize = 0;
            break;
    }

    if (poMetadataRec->getDataType() == LTI_METADATA_DATATYPE_ASCII)
    {
        strncpy((char *)pValue,
                ((const char **)poMetadataRec->getScalarData())[0], iSize);
        ((char *)pValue)[iSize - 1] = '\0';
    }
    else
        memcpy(pValue, poMetadataRec->getScalarData(), iSize);

    return TRUE;
}

/************************************************************************/
/*                            GetFileList()                             */
/************************************************************************/

char **MrSIDDataset::GetFileList()
{
    char **papszFileList = GDALPamDataset::GetFileList();

    if (!osMETFilename.empty())
        papszFileList = CSLAddString(papszFileList, osMETFilename.c_str());

    return papszFileList;
}

/************************************************************************/
/*                             OpenZoomLevel()                          */
/************************************************************************/

CPLErr MrSIDDataset::OpenZoomLevel(lt_int32 iZoom)
{
    /* -------------------------------------------------------------------- */
    /*      Get image geometry.                                            */
    /* -------------------------------------------------------------------- */
    if (iZoom != 0)
    {
        lt_uint32 iWidth, iHeight;
        dfCurrentMag = LTIUtils::levelToMag(iZoom);
        auto eLTStatus =
            poImageReader->getDimsAtMag(dfCurrentMag, iWidth, iHeight);
        if (!LT_SUCCESS(eLTStatus))
        {
            CPLDebug("MrSID", "Cannot open zoom level %d", iZoom);
            return CE_Failure;
        }
        nRasterXSize = iWidth;
        nRasterYSize = iHeight;
    }
    else
    {
        dfCurrentMag = 1.0;
        nRasterXSize = poImageReader->getWidth();
        nRasterYSize = poImageReader->getHeight();
    }

    nBands = poImageReader->getNumBands();
    nBlockXSize = nRasterXSize;
    nBlockYSize = poImageReader->getStripHeight();

    CPLDebug("MrSID", "Opened zoom level %d with size %dx%d.", iZoom,
             nRasterXSize, nRasterYSize);

    try
    {
        poLTINav = new LTIDLLNavigator<LTINavigator>(*poImageReader);
    }
    catch (...)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDDataset::OpenZoomLevel(): "
                 "Failed to create LTINavigator object.");
        return CE_Failure;
    }

    /* -------------------------------------------------------------------- */
    /*  Handle sample type and color space.                                 */
    /* -------------------------------------------------------------------- */
    eColorSpace = poImageReader->getColorSpace();
    eSampleType = poImageReader->getDataType();
    switch (eSampleType)
    {
        case LTI_DATATYPE_UINT16:
            eDataType = GDT_UInt16;
            break;
        case LTI_DATATYPE_SINT16:
            eDataType = GDT_Int16;
            break;
        case LTI_DATATYPE_UINT32:
            eDataType = GDT_UInt32;
            break;
        case LTI_DATATYPE_SINT32:
            eDataType = GDT_Int32;
            break;
        case LTI_DATATYPE_FLOAT32:
            eDataType = GDT_Float32;
            break;
        case LTI_DATATYPE_FLOAT64:
            eDataType = GDT_Float64;
            break;
        case LTI_DATATYPE_UINT8:
        case LTI_DATATYPE_SINT8:
        default:
            eDataType = GDT_Byte;
            break;
    }

    /* -------------------------------------------------------------------- */
    /*      Read georeferencing.                                            */
    /* -------------------------------------------------------------------- */
    if (!poImageReader->isGeoCoordImplicit())
    {
        const LTIGeoCoord &oGeo = poImageReader->getGeoCoord();
        oGeo.get(adfGeoTransform[0], adfGeoTransform[3], adfGeoTransform[1],
                 adfGeoTransform[5], adfGeoTransform[2], adfGeoTransform[4]);

        adfGeoTransform[0] = adfGeoTransform[0] - adfGeoTransform[1] / 2;
        adfGeoTransform[3] = adfGeoTransform[3] - adfGeoTransform[5] / 2;
        bGeoTransformValid = TRUE;
    }
    else if (iZoom == 0)
    {
        bGeoTransformValid =
            GDALReadWorldFile(GetDescription(), nullptr, adfGeoTransform) ||
            GDALReadWorldFile(GetDescription(), ".wld", adfGeoTransform);
    }

/* -------------------------------------------------------------------- */
/*      Read wkt.                                                       */
/* -------------------------------------------------------------------- */
#ifdef MRSID_HAVE_GETWKT
    if (!poImageReader->isGeoCoordImplicit())
    {
        const LTIGeoCoord &oGeo = poImageReader->getGeoCoord();

        if (oGeo.getWKT())
        {
            /* Workaround probable issue with GeoDSK 7 on 64bit Linux */
            if (!(m_oSRS.IsEmpty() && !m_oSRS.IsLocal() &&
                  STARTS_WITH_CI(oGeo.getWKT(), "LOCAL_CS")))
            {
                m_oSRS.importFromWkt(oGeo.getWKT());
            }
        }
    }
#endif  // HAVE_MRSID_GETWKT

    /* -------------------------------------------------------------------- */
    /*      Special case for https://zulu.ssc.nasa.gov/mrsid/mrsid.pl       */
    /*      where LandSat .SID are accompanied by a .met file with the      */
    /*      projection                                                      */
    /* -------------------------------------------------------------------- */
    if (iZoom == 0 && m_oSRS.IsEmpty() &&
        EQUAL(CPLGetExtensionSafe(GetDescription()).c_str(), "sid"))
    {
        const std::string l_osMETFilename =
            CPLResetExtensionSafe(GetDescription(), "met");
        VSILFILE *fp = VSIFOpenL(l_osMETFilename.c_str(), "rb");
        if (fp)
        {
            const char *pszLine = nullptr;
            int nCountLine = 0;
            int nUTMZone = 0;
            int bWGS84 = FALSE;
            int bUnitsMeter = FALSE;
            while ((pszLine = CPLReadLine2L(fp, 200, nullptr)) != nullptr &&
                   nCountLine < 1000)
            {
                ++nCountLine;
                if (nCountLine == 1 && strcmp(pszLine, "::MetadataFile") != 0)
                    break;
                if (STARTS_WITH_CI(pszLine, "Projection UTM "))
                    nUTMZone = atoi(pszLine + 15);
                else if (EQUAL(pszLine, "Datum WGS84"))
                    bWGS84 = TRUE;
                else if (EQUAL(pszLine, "Units Meters"))
                    bUnitsMeter = TRUE;
            }
            VSIFCloseL(fp);

            /* Images in southern hemisphere have negative northings in the */
            /* .sdw file. A bit weird, but anyway we must use the northern */
            /* UTM SRS for consistency */
            if (nUTMZone >= 1 && nUTMZone <= 60 && bWGS84 && bUnitsMeter)
            {
                osMETFilename = l_osMETFilename;

                m_oSRS.importFromEPSG(32600 + nUTMZone);
                m_oSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            }
        }
    }

    /* -------------------------------------------------------------------- */
    /*      Read NoData value.                                              */
    /* -------------------------------------------------------------------- */
    poNDPixel = poImageReader->getNoDataPixel();

    /* -------------------------------------------------------------------- */
    /*      Create band information objects.                                */
    /* -------------------------------------------------------------------- */
    int iBand;

    for (iBand = 1; iBand <= nBands; iBand++)
        SetBand(iBand, new MrSIDRasterBand(this, iBand));

    return CE_None;
}

/************************************************************************/
/*                          MrSIDOpen()                                 */
/*                                                                      */
/*          Open method that only supports MrSID files.                 */
/************************************************************************/

static GDALDataset *MrSIDOpen(GDALOpenInfo *poOpenInfo)
{
    if (!MrSIDIdentify(poOpenInfo))
        return nullptr;

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
    lt_uint8 gen;
    bool raster;
    LT_STATUS eStat = MrSIDImageReaderInterface::getMrSIDGeneration(
        poOpenInfo->pabyHeader, gen, raster);
    if (!LT_SUCCESS(eStat) || !raster)
        return nullptr;
#endif

    return MrSIDDataset::Open(poOpenInfo, FALSE);
}

#ifdef MRSID_J2K

/************************************************************************/
/*                            JP2Open()                                 */
/*                                                                      */
/*      Open method that only supports JPEG2000 files.                  */
/************************************************************************/

static GDALDataset *JP2Open(GDALOpenInfo *poOpenInfo)
{
    if (!MrSIDJP2Identify(poOpenInfo))
        return nullptr;

    return MrSIDDataset::Open(poOpenInfo, TRUE);
}

#endif  // MRSID_J2K

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *MrSIDDataset::Open(GDALOpenInfo *poOpenInfo, int bIsJP2)
{
    if (poOpenInfo->fpL)
    {
        VSIFCloseL(poOpenInfo->fpL);
        poOpenInfo->fpL = nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Make sure we have hooked CSV lookup for GDAL_DATA.              */
    /* -------------------------------------------------------------------- */
    LibgeotiffOneTimeInit();

    /* -------------------------------------------------------------------- */
    /*      Create a corresponding GDALDataset.                             */
    /* -------------------------------------------------------------------- */
    LT_STATUS eStat;

    MrSIDDataset *poDS = new MrSIDDataset(bIsJP2);

    // try the LTIOFileStream first, since it uses filesystem caching
    eStat = poDS->oLTIStream.initialize(poOpenInfo->pszFilename, "rb");
    if (LT_SUCCESS(eStat))
    {
        eStat = poDS->oLTIStream.open();
        if (LT_SUCCESS(eStat))
            poDS->poStream = &(poDS->oLTIStream);
    }

    // fall back on VSI for non-files
    if (!LT_SUCCESS(eStat) || !poDS->poStream)
    {
        eStat = poDS->oVSIStream.initialize(poOpenInfo->pszFilename, "rb");
        if (!LT_SUCCESS(eStat))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "LTIVSIStream::initialize(): "
                     "failed to open file \"%s\".\n%s",
                     poOpenInfo->pszFilename, getLastStatusString(eStat));
            delete poDS;
            return nullptr;
        }

        eStat = poDS->oVSIStream.open();
        if (!LT_SUCCESS(eStat))
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "LTIVSIStream::open(): "
                     "failed to open file \"%s\".\n%s",
                     poOpenInfo->pszFilename, getLastStatusString(eStat));
            delete poDS;
            return nullptr;
        }

        poDS->poStream = &(poDS->oVSIStream);
    }

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 7

#ifdef MRSID_J2K
    if (bIsJP2)
    {
        J2KImageReader *reader = J2KImageReader::create();
        eStat = reader->initialize(*(poDS->poStream));
        poDS->poImageReader = reader;
    }
    else
#endif /* MRSID_J2K */
    {
        MrSIDImageReader *reader = MrSIDImageReader::create();
        eStat = reader->initialize(poDS->poStream, nullptr);
        poDS->poImageReader = reader;
    }

#else /* LTI_SDK_MAJOR < 7 */

#ifdef MRSID_J2K
    if (bIsJP2)
    {
        poDS->poImageReader =
            new LTIDLLReader<J2KImageReader>(*(poDS->poStream), true);
        eStat = poDS->poImageReader->initialize();
    }
    else
#endif /* MRSID_J2K */
    {
        poDS->poImageReader =
            new LTIDLLReader<MrSIDImageReader>(poDS->poStream, nullptr);
        eStat = poDS->poImageReader->initialize();
    }

#endif /* LTI_SDK_MAJOR >= 7 */

    if (!LT_SUCCESS(eStat))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "LTIImageReader::initialize(): "
                 "failed to initialize reader from the stream \"%s\".\n%s",
                 poOpenInfo->pszFilename, getLastStatusString(eStat));
        delete poDS;
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Read metadata.                                                  */
    /* -------------------------------------------------------------------- */
    poDS->poMetadata =
        new LTIDLLCopy<LTIMetadataDatabase>(poDS->poImageReader->getMetadata());
    const GUInt32 iNumRecs = poDS->poMetadata->getIndexCount();

    for (GUInt32 i = 0; i < iNumRecs; i++)
    {
        const LTIMetadataRecord *poMetadataRec = nullptr;
        if (LT_SUCCESS(poDS->poMetadata->getDataByIndex(i, poMetadataRec)))
        {
            const auto osElement = SerializeMetadataRec(poMetadataRec);
            char *pszKey = CPLStrdup(poMetadataRec->getTagName());
            char *pszTemp = pszKey;

            // GDAL metadata keys should not contain ':' and '=' characters.
            // We will replace them with '_'.
            do
            {
                if (*pszTemp == ':' || *pszTemp == '=')
                    *pszTemp = '_';
            } while (*++pszTemp);

            poDS->SetMetadataItem(pszKey, osElement.c_str());

            CPLFree(pszKey);
        }
    }

/* -------------------------------------------------------------------- */
/*      Add MrSID version.                                              */
/* -------------------------------------------------------------------- */
#ifdef MRSID_J2K
    if (!bIsJP2)
#endif
    {
#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
        lt_uint8 gen;
        bool raster;
        MrSIDImageReaderInterface::getMrSIDGeneration(poOpenInfo->pabyHeader,
                                                      gen, raster);
        poDS->SetMetadataItem(
            "VERSION",
            CPLString().Printf("MG%d%s", gen, raster ? "" : " LiDAR"));
#else
        lt_uint8 major;
        lt_uint8 minor;
        char letter;
        MrSIDImageReader *poMrSIDImageReader =
            static_cast<MrSIDImageReader *>(poDS->poImageReader);
        poMrSIDImageReader->getVersion(major, minor, minor, letter);
        if (major < 2)
            major = 2;
        poDS->SetMetadataItem("VERSION", CPLString().Printf("MG%d", major));
#endif
    }

    poDS->GetGTIFDefn();

/* -------------------------------------------------------------------- */
/*      Get number of resolution levels (we will use them as overviews).*/
/* -------------------------------------------------------------------- */
#ifdef MRSID_J2K
    if (bIsJP2)
        poDS->nOverviewCount =
            static_cast<J2KImageReader *>(poDS->poImageReader)->getNumLevels();
    else
#endif
        poDS->nOverviewCount =
            static_cast<MrSIDImageReader *>(poDS->poImageReader)
                ->getNumLevels();

    if (poDS->nOverviewCount > 0)
    {
        lt_int32 i;

        poDS->papoOverviewDS =
            (MrSIDDataset **)CPLMalloc(poDS->nOverviewCount * (sizeof(void *)));

        for (i = 0; i < poDS->nOverviewCount; i++)
        {
            poDS->papoOverviewDS[i] = new MrSIDDataset(bIsJP2);
            poDS->papoOverviewDS[i]->poImageReader = poDS->poImageReader;
            poDS->papoOverviewDS[i]->bIsOverview = TRUE;
            poDS->papoOverviewDS[i]->poParentDS = poDS;
            if (poDS->papoOverviewDS[i]->OpenZoomLevel(i + 1) != CE_None)
            {
                delete poDS->papoOverviewDS[i];
                poDS->nOverviewCount = i;
                break;
            }
        }
    }

    /* -------------------------------------------------------------------- */
    /*      Create object for the whole image.                              */
    /* -------------------------------------------------------------------- */
    poDS->SetDescription(poOpenInfo->pszFilename);
    if (poDS->OpenZoomLevel(0) != CE_None)
    {
        delete poDS;
        return nullptr;
    }

    CPLDebug("MrSID", "Opened image: width %d, height %d, bands %d",
             poDS->nRasterXSize, poDS->nRasterYSize, poDS->nBands);

    if (poDS->nBands > 1)
        poDS->SetMetadataItem("INTERLEAVE", "PIXEL", "IMAGE_STRUCTURE");

    if (bIsJP2)
    {
        poDS->LoadJP2Metadata(poOpenInfo);
    }

    /* -------------------------------------------------------------------- */
    /*      Initialize any PAM information.                                 */
    /* -------------------------------------------------------------------- */
    poDS->TryLoadXML();

    /* -------------------------------------------------------------------- */
    /*      Initialize the overview manager for mask band support.          */
    /* -------------------------------------------------------------------- */
    poDS->oOvManager.Initialize(poDS, poOpenInfo->pszFilename);

    return poDS;
}

/************************************************************************/
/*                    EPSGProjMethodToCTProjMethod()                    */
/*                                                                      */
/*      Convert between the EPSG enumeration for projection methods,    */
/*      and the GeoTIFF CT codes.                                       */
/*      Explicitly copied from geo_normalize.c of the GeoTIFF package   */
/************************************************************************/

static int EPSGProjMethodToCTProjMethod(int nEPSG)

{
    /* see trf_method.csv for list of EPSG codes */

    switch (nEPSG)
    {
        case 9801:
            return CT_LambertConfConic_1SP;

        case 9802:
            return CT_LambertConfConic_2SP;

        case 9803:
            return CT_LambertConfConic_2SP;  // Belgian variant not supported.

        case 9804:
            return CT_Mercator;  // 1SP and 2SP not differentiated.

        case 9805:
            return CT_Mercator;  // 1SP and 2SP not differentiated.

        case 9806:
            return CT_CassiniSoldner;

        case 9807:
            return CT_TransverseMercator;

        case 9808:
            return CT_TransvMercator_SouthOriented;

        case 9809:
            return CT_ObliqueStereographic;

        case 9810:
            return CT_PolarStereographic;

        case 9811:
            return CT_NewZealandMapGrid;

        case 9812:
            return CT_ObliqueMercator;  // Is hotine actually different?

        case 9813:
            return CT_ObliqueMercator_Laborde;

        case 9814:
            return CT_ObliqueMercator_Rosenmund;  // Swiss.

        case 9815:
            return CT_ObliqueMercator;

        case 9816: /* tunesia mining grid has no counterpart */
            return KvUserDefined;
    }

    return KvUserDefined;
}

/* EPSG Codes for projection parameters.  Unfortunately, these bear no
   relationship to the GeoTIFF codes even though the names are so similar. */

#define EPSGNatOriginLat 8801
#define EPSGNatOriginLong 8802
#define EPSGNatOriginScaleFactor 8805
#define EPSGFalseEasting 8806
#define EPSGFalseNorthing 8807
#define EPSGProjCenterLat 8811
#define EPSGProjCenterLong 8812
#define EPSGAzimuth 8813
#define EPSGAngleRectifiedToSkewedGrid 8814
#define EPSGInitialLineScaleFactor 8815
#define EPSGProjCenterEasting 8816
#define EPSGProjCenterNorthing 8817
#define EPSGPseudoStdParallelLat 8818
#define EPSGPseudoStdParallelScaleFactor 8819
#define EPSGFalseOriginLat 8821
#define EPSGFalseOriginLong 8822
#define EPSGStdParallel1Lat 8823
#define EPSGStdParallel2Lat 8824
#define EPSGFalseOriginEasting 8826
#define EPSGFalseOriginNorthing 8827
#define EPSGSphericalOriginLat 8828
#define EPSGSphericalOriginLong 8829
#define EPSGInitialLongitude 8830
#define EPSGZoneWidth 8831

/************************************************************************/
/*                            SetGTParamIds()                            */
/*                                                                      */
/*      This is hardcoded logic to set the GeoTIFF parameter            */
/*      identifiers for all the EPSG supported projections.  As the     */
/*      trf_method.csv table grows with new projections, this code      */
/*      will need to be updated.                                        */
/*      Explicitly copied from geo_normalize.c of the GeoTIFF package.  */
/************************************************************************/

static int SetGTParamIds(int nCTProjection, int *panProjParamId,
                         int *panEPSGCodes)

{
    int anWorkingDummy[7];

    if (panEPSGCodes == nullptr)
        panEPSGCodes = anWorkingDummy;
    if (panProjParamId == nullptr)
        panProjParamId = anWorkingDummy;

    memset(panEPSGCodes, 0, sizeof(int) * 7);

    /* psDefn->nParms = 7; */

    switch (nCTProjection)
    {
        case CT_CassiniSoldner:
        case CT_NewZealandMapGrid:
            panProjParamId[0] = ProjNatOriginLatGeoKey;
            panProjParamId[1] = ProjNatOriginLongGeoKey;
            panProjParamId[5] = ProjFalseEastingGeoKey;
            panProjParamId[6] = ProjFalseNorthingGeoKey;

            panEPSGCodes[0] = EPSGNatOriginLat;
            panEPSGCodes[1] = EPSGNatOriginLong;
            panEPSGCodes[5] = EPSGFalseEasting;
            panEPSGCodes[6] = EPSGFalseNorthing;
            return TRUE;

        case CT_ObliqueMercator:
            panProjParamId[0] = ProjCenterLatGeoKey;
            panProjParamId[1] = ProjCenterLongGeoKey;
            panProjParamId[2] = ProjAzimuthAngleGeoKey;
            panProjParamId[3] = ProjRectifiedGridAngleGeoKey;
            panProjParamId[4] = ProjScaleAtCenterGeoKey;
            panProjParamId[5] = ProjFalseEastingGeoKey;
            panProjParamId[6] = ProjFalseNorthingGeoKey;

            panEPSGCodes[0] = EPSGProjCenterLat;
            panEPSGCodes[1] = EPSGProjCenterLong;
            panEPSGCodes[2] = EPSGAzimuth;
            panEPSGCodes[3] = EPSGAngleRectifiedToSkewedGrid;
            panEPSGCodes[4] = EPSGInitialLineScaleFactor;
            panEPSGCodes[5] = EPSGProjCenterEasting;
            panEPSGCodes[6] = EPSGProjCenterNorthing;
            return TRUE;

        case CT_ObliqueMercator_Laborde:
            panProjParamId[0] = ProjCenterLatGeoKey;
            panProjParamId[1] = ProjCenterLongGeoKey;
            panProjParamId[2] = ProjAzimuthAngleGeoKey;
            panProjParamId[4] = ProjScaleAtCenterGeoKey;
            panProjParamId[5] = ProjFalseEastingGeoKey;
            panProjParamId[6] = ProjFalseNorthingGeoKey;

            panEPSGCodes[0] = EPSGProjCenterLat;
            panEPSGCodes[1] = EPSGProjCenterLong;
            panEPSGCodes[2] = EPSGAzimuth;
            panEPSGCodes[4] = EPSGInitialLineScaleFactor;
            panEPSGCodes[5] = EPSGProjCenterEasting;
            panEPSGCodes[6] = EPSGProjCenterNorthing;
            return TRUE;

        case CT_LambertConfConic_1SP:
        case CT_Mercator:
        case CT_ObliqueStereographic:
        case CT_PolarStereographic:
        case CT_TransverseMercator:
        case CT_TransvMercator_SouthOriented:
            panProjParamId[0] = ProjNatOriginLatGeoKey;
            panProjParamId[1] = ProjNatOriginLongGeoKey;
            panProjParamId[4] = ProjScaleAtNatOriginGeoKey;
            panProjParamId[5] = ProjFalseEastingGeoKey;
            panProjParamId[6] = ProjFalseNorthingGeoKey;

            panEPSGCodes[0] = EPSGNatOriginLat;
            panEPSGCodes[1] = EPSGNatOriginLong;
            panEPSGCodes[4] = EPSGNatOriginScaleFactor;
            panEPSGCodes[5] = EPSGFalseEasting;
            panEPSGCodes[6] = EPSGFalseNorthing;
            return TRUE;

        case CT_LambertConfConic_2SP:
            panProjParamId[0] = ProjFalseOriginLatGeoKey;
            panProjParamId[1] = ProjFalseOriginLongGeoKey;
            panProjParamId[2] = ProjStdParallel1GeoKey;
            panProjParamId[3] = ProjStdParallel2GeoKey;
            panProjParamId[5] = ProjFalseEastingGeoKey;
            panProjParamId[6] = ProjFalseNorthingGeoKey;

            panEPSGCodes[0] = EPSGFalseOriginLat;
            panEPSGCodes[1] = EPSGFalseOriginLong;
            panEPSGCodes[2] = EPSGStdParallel1Lat;
            panEPSGCodes[3] = EPSGStdParallel2Lat;
            panEPSGCodes[5] = EPSGFalseOriginEasting;
            panEPSGCodes[6] = EPSGFalseOriginNorthing;
            return TRUE;

        case CT_SwissObliqueCylindrical:
            panProjParamId[0] = ProjCenterLatGeoKey;
            panProjParamId[1] = ProjCenterLongGeoKey;
            panProjParamId[5] = ProjFalseEastingGeoKey;
            panProjParamId[6] = ProjFalseNorthingGeoKey;

            /* EPSG codes? */
            return TRUE;

        default:
            return FALSE;
    }
}

static const char *const papszDatumEquiv[] = {
    "Militar_Geographische_Institut",
    "Militar_Geographische_Institute",
    "World_Geodetic_System_1984",
    "WGS_1984",
    "WGS_72_Transit_Broadcast_Ephemeris",
    "WGS_1972_Transit_Broadcast_Ephemeris",
    "World_Geodetic_System_1972",
    "WGS_1972",
    "European_Terrestrial_Reference_System_89",
    "European_Reference_System_1989",
    nullptr};

/************************************************************************/
/*                          WKTMassageDatum()                           */
/*                                                                      */
/*      Massage an EPSG datum name into WMT format.  Also transform     */
/*      specific exception cases into WKT versions.                     */
/*      Explicitly copied from the gt_wkt_srs.cpp.                      */
/************************************************************************/

static void WKTMassageDatum(char **ppszDatum)

{
    int i, j;
    char *pszDatum = *ppszDatum;

    if (pszDatum[0] == '\0')
        return;

    /* -------------------------------------------------------------------- */
    /*      Translate non-alphanumeric values to underscores.               */
    /* -------------------------------------------------------------------- */
    for (i = 0; pszDatum[i] != '\0'; i++)
    {
        if (!(pszDatum[i] >= 'A' && pszDatum[i] <= 'Z') &&
            !(pszDatum[i] >= 'a' && pszDatum[i] <= 'z') &&
            !(pszDatum[i] >= '0' && pszDatum[i] <= '9'))
        {
            pszDatum[i] = '_';
        }
    }

    /* -------------------------------------------------------------------- */
    /*      Remove repeated and trailing underscores.                       */
    /* -------------------------------------------------------------------- */
    for (i = 1, j = 0; pszDatum[i] != '\0'; i++)
    {
        if (pszDatum[j] == '_' && pszDatum[i] == '_')
            continue;

        pszDatum[++j] = pszDatum[i];
    }
    if (pszDatum[j] == '_')
        pszDatum[j] = '\0';
    else
        pszDatum[j + 1] = '\0';

    /* -------------------------------------------------------------------- */
    /*      Search for datum equivalences.  Specific massaged names get     */
    /*      mapped to OpenGIS specified names.                              */
    /* -------------------------------------------------------------------- */
    for (i = 0; papszDatumEquiv[i] != nullptr; i += 2)
    {
        if (EQUAL(*ppszDatum, papszDatumEquiv[i]))
        {
            CPLFree(*ppszDatum);
            *ppszDatum = CPLStrdup(papszDatumEquiv[i + 1]);
            return;
        }
    }
}

/************************************************************************/
/*                           FetchProjParams()                           */
/*                                                                      */
/*      Fetch the projection parameters for a particular projection     */
/*      from MrSID metadata, and fill the GTIFDefn structure out        */
/*      with them.                                                      */
/*      Copied from geo_normalize.c of the GeoTIFF package.             */
/************************************************************************/

void MrSIDDataset::FetchProjParams()
{
    double dfNatOriginLong = 0.0, dfNatOriginLat = 0.0, dfRectGridAngle = 0.0;
    double dfFalseEasting = 0.0, dfFalseNorthing = 0.0, dfNatOriginScale = 1.0;
    double dfStdParallel1 = 0.0, dfStdParallel2 = 0.0, dfAzimuth = 0.0;

    /* -------------------------------------------------------------------- */
    /*      Get the false easting, and northing if available.               */
    /* -------------------------------------------------------------------- */
    if (!GetMetadataElement("GEOTIFF_NUM::3082::ProjFalseEastingGeoKey",
                            &dfFalseEasting) &&
        !GetMetadataElement("GEOTIFF_NUM::3090:ProjCenterEastingGeoKey",
                            &dfFalseEasting))
        dfFalseEasting = 0.0;

    if (!GetMetadataElement("GEOTIFF_NUM::3083::ProjFalseNorthingGeoKey",
                            &dfFalseNorthing) &&
        !GetMetadataElement("GEOTIFF_NUM::3091::ProjCenterNorthingGeoKey",
                            &dfFalseNorthing))
        dfFalseNorthing = 0.0;

    switch (psDefn->CTProjection)
    {
            /* --------------------------------------------------------------------
             */
        case CT_Stereographic:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            if (GetMetadataElement(
                    "GEOTIFF_NUM::3092::ProjScaleAtNatOriginGeoKey",
                    &dfNatOriginScale) == 0)
                dfNatOriginScale = 1.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjCenterLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjCenterLongGeoKey;
            psDefn->ProjParm[4] = dfNatOriginScale;
            psDefn->ProjParmId[4] = ProjScaleAtNatOriginGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_LambertConfConic_1SP:
        case CT_Mercator:
        case CT_ObliqueStereographic:
        case CT_TransverseMercator:
        case CT_TransvMercator_SouthOriented:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            if (GetMetadataElement(
                    "GEOTIFF_NUM::3092::ProjScaleAtNatOriginGeoKey",
                    &dfNatOriginScale) == 0)
                dfNatOriginScale = 1.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjNatOriginLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjNatOriginLongGeoKey;
            psDefn->ProjParm[4] = dfNatOriginScale;
            psDefn->ProjParmId[4] = ProjScaleAtNatOriginGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_ObliqueMercator: /* hotine */
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3094::ProjAzimuthAngleGeoKey",
                                   &dfAzimuth) == 0)
                dfAzimuth = 0.0;

            if (GetMetadataElement(
                    "GEOTIFF_NUM::3096::ProjRectifiedGridAngleGeoKey",
                    &dfRectGridAngle) == 0)
                dfRectGridAngle = 90.0;

            if (GetMetadataElement(
                    "GEOTIFF_NUM::3092::ProjScaleAtNatOriginGeoKey",
                    &dfNatOriginScale) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3093::ProjScaleAtCenterGeoKey",
                                   &dfNatOriginScale) == 0)
                dfNatOriginScale = 1.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjCenterLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjCenterLongGeoKey;
            psDefn->ProjParm[2] = dfAzimuth;
            psDefn->ProjParmId[2] = ProjAzimuthAngleGeoKey;
            psDefn->ProjParm[3] = dfRectGridAngle;
            psDefn->ProjParmId[3] = ProjRectifiedGridAngleGeoKey;
            psDefn->ProjParm[4] = dfNatOriginScale;
            psDefn->ProjParmId[4] = ProjScaleAtCenterGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_CassiniSoldner:
        case CT_Polyconic:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            if (GetMetadataElement(
                    "GEOTIFF_NUM::3092::ProjScaleAtNatOriginGeoKey",
                    &dfNatOriginScale) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3093::ProjScaleAtCenterGeoKey",
                                   &dfNatOriginScale) == 0)
                dfNatOriginScale = 1.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjNatOriginLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjNatOriginLongGeoKey;
            psDefn->ProjParm[4] = dfNatOriginScale;
            psDefn->ProjParmId[4] = ProjScaleAtNatOriginGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_AzimuthalEquidistant:
        case CT_MillerCylindrical:
        case CT_Equirectangular:
        case CT_Gnomonic:
        case CT_LambertAzimEqualArea:
        case CT_Orthographic:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjCenterLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjCenterLongGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_Robinson:
        case CT_Sinusoidal:
        case CT_VanDerGrinten:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjCenterLongGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_PolarStereographic:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement(
                    "GEOTIFF_NUM::3095::ProjStraightVertPoleLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            if (GetMetadataElement(
                    "GEOTIFF_NUM::3092::ProjScaleAtNatOriginGeoKey",
                    &dfNatOriginScale) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3093::ProjScaleAtCenterGeoKey",
                                   &dfNatOriginScale) == 0)
                dfNatOriginScale = 1.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjNatOriginLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjStraightVertPoleLongGeoKey;
            psDefn->ProjParm[4] = dfNatOriginScale;
            psDefn->ProjParmId[4] = ProjScaleAtNatOriginGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_LambertConfConic_2SP:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3078::ProjStdParallel1GeoKey",
                                   &dfStdParallel1) == 0)
                dfStdParallel1 = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3079::ProjStdParallel2GeoKey",
                                   &dfStdParallel2) == 0)
                dfStdParallel1 = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfNatOriginLat;
            psDefn->ProjParmId[0] = ProjFalseOriginLatGeoKey;
            psDefn->ProjParm[1] = dfNatOriginLong;
            psDefn->ProjParmId[1] = ProjFalseOriginLongGeoKey;
            psDefn->ProjParm[2] = dfStdParallel1;
            psDefn->ProjParmId[2] = ProjStdParallel1GeoKey;
            psDefn->ProjParm[3] = dfStdParallel2;
            psDefn->ProjParmId[3] = ProjStdParallel2GeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;

            /* --------------------------------------------------------------------
             */
        case CT_AlbersEqualArea:
        case CT_EquidistantConic:
            /* --------------------------------------------------------------------
             */
            if (GetMetadataElement("GEOTIFF_NUM::3078::ProjStdParallel1GeoKey",
                                   &dfStdParallel1) == 0)
                dfStdParallel1 = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3079::ProjStdParallel2GeoKey",
                                   &dfStdParallel2) == 0)
                dfStdParallel1 = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3080::ProjNatOriginLongGeoKey",
                                   &dfNatOriginLong) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3084::ProjFalseOriginLongGeoKey",
                    &dfNatOriginLong) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3088::ProjCenterLongGeoKey",
                                   &dfNatOriginLong) == 0)
                dfNatOriginLong = 0.0;

            if (GetMetadataElement("GEOTIFF_NUM::3081::ProjNatOriginLatGeoKey",
                                   &dfNatOriginLat) == 0 &&
                GetMetadataElement(
                    "GEOTIFF_NUM::3085::ProjFalseOriginLatGeoKey",
                    &dfNatOriginLat) == 0 &&
                GetMetadataElement("GEOTIFF_NUM::3089::ProjCenterLatGeoKey",
                                   &dfNatOriginLat) == 0)
                dfNatOriginLat = 0.0;

            /* notdef: should transform to decimal degrees at this point */

            psDefn->ProjParm[0] = dfStdParallel1;
            psDefn->ProjParmId[0] = ProjStdParallel1GeoKey;
            psDefn->ProjParm[1] = dfStdParallel2;
            psDefn->ProjParmId[1] = ProjStdParallel2GeoKey;
            psDefn->ProjParm[2] = dfNatOriginLat;
            psDefn->ProjParmId[2] = ProjNatOriginLatGeoKey;
            psDefn->ProjParm[3] = dfNatOriginLong;
            psDefn->ProjParmId[3] = ProjNatOriginLongGeoKey;
            psDefn->ProjParm[5] = dfFalseEasting;
            psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
            psDefn->ProjParm[6] = dfFalseNorthing;
            psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

            psDefn->nParms = 7;
            break;
    }
}

/************************************************************************/
/*                            GetGTIFDefn()                             */
/*      This function borrowed from the GTIFGetDefn() function.         */
/*      See geo_normalize.c from the GeoTIFF package.                   */
/************************************************************************/

void MrSIDDataset::GetGTIFDefn()
{
    double dfInvFlattening;

    /* -------------------------------------------------------------------- */
    /*      Make sure we have hooked CSV lookup for GDAL_DATA.              */
    /* -------------------------------------------------------------------- */
    LibgeotiffOneTimeInit();

    /* -------------------------------------------------------------------- */
    /*      Initially we default all the information we can.                */
    /* -------------------------------------------------------------------- */
    psDefn = new (GTIFDefn);
    psDefn->Model = KvUserDefined;
    psDefn->PCS = KvUserDefined;
    psDefn->GCS = KvUserDefined;
    psDefn->UOMLength = KvUserDefined;
    psDefn->UOMLengthInMeters = 1.0;
    psDefn->UOMAngle = KvUserDefined;
    psDefn->UOMAngleInDegrees = 1.0;
    psDefn->Datum = KvUserDefined;
    psDefn->Ellipsoid = KvUserDefined;
    psDefn->SemiMajor = 0.0;
    psDefn->SemiMinor = 0.0;
    psDefn->PM = KvUserDefined;
    psDefn->PMLongToGreenwich = 0.0;

    psDefn->ProjCode = KvUserDefined;
    psDefn->Projection = KvUserDefined;
    psDefn->CTProjection = KvUserDefined;

    psDefn->nParms = 0;
    for (int i = 0; i < MAX_GTIF_PROJPARMS; i++)
    {
        psDefn->ProjParm[i] = 0.0;
        psDefn->ProjParmId[i] = 0;
    }

    psDefn->MapSys = KvUserDefined;
    psDefn->Zone = 0;

    /* -------------------------------------------------------------------- */
    /*      Try to get the overall model type.                              */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::1024::GTModelTypeGeoKey",
                       &(psDefn->Model));

    /* -------------------------------------------------------------------- */
    /*      Try to get a PCS.                                               */
    /* -------------------------------------------------------------------- */
    if (GetMetadataElement("GEOTIFF_NUM::3072::ProjectedCSTypeGeoKey",
                           &(psDefn->PCS)) &&
        psDefn->PCS != KvUserDefined)
    {
        /*
         * Translate this into useful information.
         */
        GTIFGetPCSInfo(psDefn->PCS, nullptr, &(psDefn->ProjCode),
                       &(psDefn->UOMLength), &(psDefn->GCS));
    }

    /* -------------------------------------------------------------------- */
    /*       If we have the PCS code, but didn't find it in the CSV files   */
    /*      (likely because we can't find them) we will try some ``jiffy    */
    /*      rules'' for UTM and state plane.                                */
    /* -------------------------------------------------------------------- */
    if (psDefn->PCS != KvUserDefined && psDefn->ProjCode == KvUserDefined)
    {
        int nMapSys, nZone;
        int nGCS = psDefn->GCS;

        nMapSys = GTIFPCSToMapSys(psDefn->PCS, &nGCS, &nZone);
        if (nMapSys != KvUserDefined)
        {
            psDefn->ProjCode = (short)GTIFMapSysToProj(nMapSys, nZone);
            psDefn->GCS = (short)nGCS;
        }
    }

    /* -------------------------------------------------------------------- */
    /*      If the Proj_ code is specified directly, use that.              */
    /* -------------------------------------------------------------------- */
    if (psDefn->ProjCode == KvUserDefined)
        GetMetadataElement("GEOTIFF_NUM::3074::ProjectionGeoKey",
                           &(psDefn->ProjCode));

    if (psDefn->ProjCode != KvUserDefined)
    {
        /*
         * We have an underlying projection transformation value.  Look
         * this up.  For a PCS of ``WGS 84 / UTM 11'' the transformation
         * would be Transverse Mercator, with a particular set of options.
         * The nProjTRFCode itself would correspond to the name
         * ``UTM zone 11N'', and doesn't include datum info.
         */
        GTIFGetProjTRFInfo(psDefn->ProjCode, nullptr, &(psDefn->Projection),
                           psDefn->ProjParm);

        /*
         * Set the GeoTIFF identity of the parameters.
         */
        psDefn->CTProjection =
            (short)EPSGProjMethodToCTProjMethod(psDefn->Projection);

        SetGTParamIds(psDefn->CTProjection, psDefn->ProjParmId, nullptr);
        psDefn->nParms = 7;
    }

    /* -------------------------------------------------------------------- */
    /*      Try to get a GCS.  If found, it will override any implied by    */
    /*      the PCS.                                                        */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::2048::GeographicTypeGeoKey",
                       &(psDefn->GCS));

    /* -------------------------------------------------------------------- */
    /*      Derive the datum, and prime meridian from the GCS.              */
    /* -------------------------------------------------------------------- */
    if (psDefn->GCS != KvUserDefined)
    {
        GTIFGetGCSInfo(psDefn->GCS, nullptr, &(psDefn->Datum), &(psDefn->PM),
                       &(psDefn->UOMAngle));
    }

    /* -------------------------------------------------------------------- */
    /*      Handle the GCS angular units.  GeogAngularUnitsGeoKey           */
    /*      overrides the GCS or PCS setting.                               */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::2054::GeogAngularUnitsGeoKey",
                       &(psDefn->UOMAngle));
    if (psDefn->UOMAngle != KvUserDefined)
    {
        GTIFGetUOMAngleInfo(psDefn->UOMAngle, nullptr,
                            &(psDefn->UOMAngleInDegrees));
    }

    /* -------------------------------------------------------------------- */
    /*      Check for a datum setting, and then use the datum to derive     */
    /*      an ellipsoid.                                                   */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::2050::GeogGeodeticDatumGeoKey",
                       &(psDefn->Datum));

    if (psDefn->Datum != KvUserDefined)
    {
        GTIFGetDatumInfo(psDefn->Datum, nullptr, &(psDefn->Ellipsoid));
    }

    /* -------------------------------------------------------------------- */
    /*      Check for an explicit ellipsoid.  Use the ellipsoid to          */
    /*      derive the ellipsoid characteristics, if possible.              */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::2056::GeogEllipsoidGeoKey",
                       &(psDefn->Ellipsoid));

    if (psDefn->Ellipsoid != KvUserDefined)
    {
        GTIFGetEllipsoidInfo(psDefn->Ellipsoid, nullptr, &(psDefn->SemiMajor),
                             &(psDefn->SemiMinor));
    }

    /* -------------------------------------------------------------------- */
    /*      Check for overridden ellipsoid parameters.  It would be nice    */
    /*      to warn if they conflict with provided information, but for     */
    /*      now we just override.                                           */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::2057::GeogSemiMajorAxisGeoKey",
                       &(psDefn->SemiMajor));
    GetMetadataElement("GEOTIFF_NUM::2058::GeogSemiMinorAxisGeoKey",
                       &(psDefn->SemiMinor));

    if (GetMetadataElement("GEOTIFF_NUM::2059::GeogInvFlatteningGeoKey",
                           &dfInvFlattening) == 1)
    {
        if (dfInvFlattening != 0.0)
            psDefn->SemiMinor = OSRCalcSemiMinorFromInvFlattening(
                psDefn->SemiMajor, dfInvFlattening);
    }

    /* -------------------------------------------------------------------- */
    /*      Get the prime meridian info.                                    */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::2051::GeogPrimeMeridianGeoKey",
                       &(psDefn->PM));

    if (psDefn->PM != KvUserDefined)
    {
        GTIFGetPMInfo(psDefn->PM, nullptr, &(psDefn->PMLongToGreenwich));
    }
    else
    {
        GetMetadataElement("GEOTIFF_NUM::2061::GeogPrimeMeridianLongGeoKey",
                           &(psDefn->PMLongToGreenwich));

        psDefn->PMLongToGreenwich =
            GTIFAngleToDD(psDefn->PMLongToGreenwich, psDefn->UOMAngle);
    }

    /* -------------------------------------------------------------------- */
    /*      Have the projection units of measure been overridden?  We       */
    /*      should likely be doing something about angular units too,       */
    /*      but these are very rarely not decimal degrees for actual        */
    /*      file coordinates.                                               */
    /* -------------------------------------------------------------------- */
    GetMetadataElement("GEOTIFF_NUM::3076::ProjLinearUnitsGeoKey",
                       &(psDefn->UOMLength));

    if (psDefn->UOMLength != KvUserDefined)
    {
        GTIFGetUOMLengthInfo(psDefn->UOMLength, nullptr,
                             &(psDefn->UOMLengthInMeters));
    }

    /* -------------------------------------------------------------------- */
    /*      Handle a variety of user defined transform types.               */
    /* -------------------------------------------------------------------- */
    if (GetMetadataElement("GEOTIFF_NUM::3075::ProjCoordTransGeoKey",
                           &(psDefn->CTProjection)))
    {
        FetchProjParams();
    }

    /* -------------------------------------------------------------------- */
    /*      Try to set the zoned map system information.                    */
    /* -------------------------------------------------------------------- */
    psDefn->MapSys = GTIFProjToMapSys(psDefn->ProjCode, &(psDefn->Zone));

    /* -------------------------------------------------------------------- */
    /*      If this is UTM, and we were unable to extract the projection    */
    /*      parameters from the CSV file, just set them directly now,       */
    /*      since it is pretty easy, and a common case.                     */
    /* -------------------------------------------------------------------- */
    if ((psDefn->MapSys == MapSys_UTM_North ||
         psDefn->MapSys == MapSys_UTM_South) &&
        psDefn->CTProjection == KvUserDefined)
    {
        psDefn->CTProjection = CT_TransverseMercator;
        psDefn->nParms = 7;
        psDefn->ProjParmId[0] = ProjNatOriginLatGeoKey;
        psDefn->ProjParm[0] = 0.0;

        psDefn->ProjParmId[1] = ProjNatOriginLongGeoKey;
        psDefn->ProjParm[1] = psDefn->Zone * 6 - 183.0;

        psDefn->ProjParmId[4] = ProjScaleAtNatOriginGeoKey;
        psDefn->ProjParm[4] = 0.9996;

        psDefn->ProjParmId[5] = ProjFalseEastingGeoKey;
        psDefn->ProjParm[5] = 500000.0;

        psDefn->ProjParmId[6] = ProjFalseNorthingGeoKey;

        if (psDefn->MapSys == MapSys_UTM_North)
            psDefn->ProjParm[6] = 0.0;
        else
            psDefn->ProjParm[6] = 10000000.0;
    }

    char *pszProjection = GetOGISDefn(psDefn);
    if (pszProjection)
    {
        m_oSRS.importFromWkt(pszProjection);
        m_oSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    }
    CPLFree(pszProjection);
}

/************************************************************************/
/*                       GTIFToCPLRecyleString()                        */
/*                                                                      */
/*      This changes a string from the libgeotiff heap to the GDAL      */
/*      heap.                                                           */
/************************************************************************/

static void GTIFToCPLRecycleString(char **ppszTarget)

{
    if (*ppszTarget == nullptr)
        return;

    char *pszTempString = CPLStrdup(*ppszTarget);
    GTIFFreeMemory(*ppszTarget);
    *ppszTarget = pszTempString;
}

/************************************************************************/
/*                          GetOGISDefn()                               */
/*  Copied from the gt_wkt_srs.cpp.                                     */
/************************************************************************/

char *MrSIDDataset::GetOGISDefn(GTIFDefn *psDefnIn)
{
    OGRSpatialReference oSRS;

    if (psDefnIn->Model != ModelTypeProjected &&
        psDefnIn->Model != ModelTypeGeographic)
        return CPLStrdup("");

    /* -------------------------------------------------------------------- */
    /*      If this is a projected SRS we set the PROJCS keyword first      */
    /*      to ensure that the GEOGCS will be a child.                      */
    /* -------------------------------------------------------------------- */
    if (psDefnIn->Model == ModelTypeProjected)
    {
        int bPCSNameSet = FALSE;

        if (psDefnIn->PCS != KvUserDefined)
        {
            char *pszPCSName = nullptr;

            if (GTIFGetPCSInfo(psDefnIn->PCS, &pszPCSName, nullptr, nullptr,
                               nullptr))
                bPCSNameSet = TRUE;

            oSRS.SetNode("PROJCS", bPCSNameSet ? pszPCSName : "unnamed");
            if (bPCSNameSet)
                GTIFFreeMemory(pszPCSName);

            oSRS.SetAuthority("PROJCS", "EPSG", psDefnIn->PCS);
        }
        else
        {
            char szPCSName[200];
            strcpy(szPCSName, "unnamed");
            if (GetMetadataElement("GEOTIFF_NUM::1026::GTCitationGeoKey",
                                   szPCSName, sizeof(szPCSName)))
                oSRS.SetNode("PROJCS", szPCSName);
        }
    }

    /* ==================================================================== */
    /*      Setup the GeogCS                                                */
    /* ==================================================================== */
    char *pszGeogName = nullptr;
    char *pszDatumName = nullptr;
    char *pszPMName = nullptr;
    char *pszSpheroidName = nullptr;
    char *pszAngularUnits = nullptr;
    double dfInvFlattening, dfSemiMajor;
    char szGCSName[200];

    if (GetMetadataElement("GEOTIFF_NUM::2049::GeogCitationGeoKey", szGCSName,
                           sizeof(szGCSName)))
        pszGeogName = CPLStrdup(szGCSName);
    else
    {
        GTIFGetGCSInfo(psDefnIn->GCS, &pszGeogName, nullptr, nullptr, nullptr);
        GTIFToCPLRecycleString(&pszGeogName);
    }
    GTIFGetDatumInfo(psDefnIn->Datum, &pszDatumName, nullptr);
    GTIFToCPLRecycleString(&pszDatumName);
    GTIFGetPMInfo(psDefnIn->PM, &pszPMName, nullptr);
    GTIFToCPLRecycleString(&pszPMName);
    GTIFGetEllipsoidInfo(psDefnIn->Ellipsoid, &pszSpheroidName, nullptr,
                         nullptr);
    GTIFToCPLRecycleString(&pszSpheroidName);

    GTIFGetUOMAngleInfo(psDefnIn->UOMAngle, &pszAngularUnits, nullptr);
    GTIFToCPLRecycleString(&pszAngularUnits);
    if (pszAngularUnits == nullptr)
        pszAngularUnits = CPLStrdup("unknown");

    if (pszDatumName != nullptr)
        WKTMassageDatum(&pszDatumName);

    dfSemiMajor = psDefnIn->SemiMajor;
    if (psDefnIn->SemiMajor == 0.0)
    {
        CPLFree(pszSpheroidName);
        pszSpheroidName = CPLStrdup("unretrievable - using WGS84");
        dfSemiMajor = SRS_WGS84_SEMIMAJOR;
        dfInvFlattening = SRS_WGS84_INVFLATTENING;
    }
    else
        dfInvFlattening =
            OSRCalcInvFlattening(psDefnIn->SemiMajor, psDefnIn->SemiMinor);

    oSRS.SetGeogCS(pszGeogName, pszDatumName, pszSpheroidName, dfSemiMajor,
                   dfInvFlattening, pszPMName,
                   psDefnIn->PMLongToGreenwich / psDefnIn->UOMAngleInDegrees,
                   pszAngularUnits,
                   psDefnIn->UOMAngleInDegrees * 0.0174532925199433);

    if (psDefnIn->GCS != KvUserDefined)
        oSRS.SetAuthority("GEOGCS", "EPSG", psDefnIn->GCS);

    if (psDefnIn->Datum != KvUserDefined)
        oSRS.SetAuthority("DATUM", "EPSG", psDefnIn->Datum);

    if (psDefnIn->Ellipsoid != KvUserDefined)
        oSRS.SetAuthority("SPHEROID", "EPSG", psDefnIn->Ellipsoid);

    CPLFree(pszGeogName);
    CPLFree(pszDatumName);
    CPLFree(pszPMName);
    CPLFree(pszSpheroidName);
    CPLFree(pszAngularUnits);

    /* ==================================================================== */
    /*      Handle projection parameters.                                   */
    /* ==================================================================== */
    if (psDefnIn->Model == ModelTypeProjected)
    {
        /* --------------------------------------------------------------------
         */
        /*      Make a local copy of params, and convert back into the */
        /*      angular units of the GEOGCS and the linear units of the */
        /*      projection. */
        /* --------------------------------------------------------------------
         */
        double adfParam[10];
        int i;

        for (i = 0; i < MIN(10, psDefnIn->nParms); i++)
            adfParam[i] = psDefnIn->ProjParm[i];
        for (; i < 10; i++)
            adfParam[i] = 0;

        adfParam[0] /= psDefnIn->UOMAngleInDegrees;
        adfParam[1] /= psDefnIn->UOMAngleInDegrees;
        adfParam[2] /= psDefnIn->UOMAngleInDegrees;
        adfParam[3] /= psDefnIn->UOMAngleInDegrees;

        adfParam[5] /= psDefnIn->UOMLengthInMeters;
        adfParam[6] /= psDefnIn->UOMLengthInMeters;

        /* --------------------------------------------------------------------
         */
        /*      Translation the fundamental projection. */
        /* --------------------------------------------------------------------
         */
        switch (psDefnIn->CTProjection)
        {
            case CT_TransverseMercator:
                oSRS.SetTM(adfParam[0], adfParam[1], adfParam[4], adfParam[5],
                           adfParam[6]);
                break;

            case CT_TransvMercator_SouthOriented:
                oSRS.SetTMSO(adfParam[0], adfParam[1], adfParam[4], adfParam[5],
                             adfParam[6]);
                break;

            case CT_Mercator:
                oSRS.SetMercator(adfParam[0], adfParam[1], adfParam[4],
                                 adfParam[5], adfParam[6]);
                break;

            case CT_ObliqueStereographic:
                oSRS.SetOS(adfParam[0], adfParam[1], adfParam[4], adfParam[5],
                           adfParam[6]);
                break;

            case CT_Stereographic:
                oSRS.SetOS(adfParam[0], adfParam[1], adfParam[4], adfParam[5],
                           adfParam[6]);
                break;

            case CT_ObliqueMercator: /* hotine */
                oSRS.SetHOM(adfParam[0], adfParam[1], adfParam[2], adfParam[3],
                            adfParam[4], adfParam[5], adfParam[6]);
                break;

            case CT_EquidistantConic:
                oSRS.SetEC(adfParam[0], adfParam[1], adfParam[2], adfParam[3],
                           adfParam[5], adfParam[6]);
                break;

            case CT_CassiniSoldner:
                oSRS.SetCS(adfParam[0], adfParam[1], adfParam[5], adfParam[6]);
                break;

            case CT_Polyconic:
                oSRS.SetPolyconic(adfParam[0], adfParam[1], adfParam[5],
                                  adfParam[6]);
                break;

            case CT_AzimuthalEquidistant:
                oSRS.SetAE(adfParam[0], adfParam[1], adfParam[5], adfParam[6]);
                break;

            case CT_MillerCylindrical:
                oSRS.SetMC(adfParam[0], adfParam[1], adfParam[5], adfParam[6]);
                break;

            case CT_Equirectangular:
                oSRS.SetEquirectangular(adfParam[0], adfParam[1], adfParam[5],
                                        adfParam[6]);
                break;

            case CT_Gnomonic:
                oSRS.SetGnomonic(adfParam[0], adfParam[1], adfParam[5],
                                 adfParam[6]);
                break;

            case CT_LambertAzimEqualArea:
                oSRS.SetLAEA(adfParam[0], adfParam[1], adfParam[5],
                             adfParam[6]);
                break;

            case CT_Orthographic:
                oSRS.SetOrthographic(adfParam[0], adfParam[1], adfParam[5],
                                     adfParam[6]);
                break;

            case CT_Robinson:
                oSRS.SetRobinson(adfParam[1], adfParam[5], adfParam[6]);
                break;

            case CT_Sinusoidal:
                oSRS.SetSinusoidal(adfParam[1], adfParam[5], adfParam[6]);
                break;

            case CT_VanDerGrinten:
                oSRS.SetVDG(adfParam[1], adfParam[5], adfParam[6]);
                break;

            case CT_PolarStereographic:
                oSRS.SetPS(adfParam[0], adfParam[1], adfParam[4], adfParam[5],
                           adfParam[6]);
                break;

            case CT_LambertConfConic_2SP:
                oSRS.SetLCC(adfParam[2], adfParam[3], adfParam[0], adfParam[1],
                            adfParam[5], adfParam[6]);
                break;

            case CT_LambertConfConic_1SP:
                oSRS.SetLCC1SP(adfParam[0], adfParam[1], adfParam[4],
                               adfParam[5], adfParam[6]);
                break;

            case CT_AlbersEqualArea:
                oSRS.SetACEA(adfParam[0], adfParam[1], adfParam[2], adfParam[3],
                             adfParam[5], adfParam[6]);
                break;

            case CT_NewZealandMapGrid:
                oSRS.SetNZMG(adfParam[0], adfParam[1], adfParam[5],
                             adfParam[6]);
                break;
        }

        /* --------------------------------------------------------------------
         */
        /*      Set projection units. */
        /* --------------------------------------------------------------------
         */
        char *pszUnitsName = nullptr;

        GTIFGetUOMLengthInfo(psDefnIn->UOMLength, &pszUnitsName, nullptr);

        if (pszUnitsName != nullptr && psDefnIn->UOMLength != KvUserDefined)
        {
            oSRS.SetLinearUnits(pszUnitsName, psDefnIn->UOMLengthInMeters);
            oSRS.SetAuthority("PROJCS|UNIT", "EPSG", psDefnIn->UOMLength);
        }
        else
            oSRS.SetLinearUnits("unknown", psDefnIn->UOMLengthInMeters);

        GTIFFreeMemory(pszUnitsName);
    }

    /* -------------------------------------------------------------------- */
    /*      Return the WKT serialization of the object.                     */
    /* -------------------------------------------------------------------- */

    char *pszWKT = nullptr;
    if (oSRS.exportToWkt(&pszWKT) == OGRERR_NONE)
        return pszWKT;
    else
    {
        CPLFree(pszWKT);
        return nullptr;
    }
}

#ifdef MRSID_ESDK

/************************************************************************/
/* ==================================================================== */
/*                        MrSIDDummyImageReader                         */
/*                                                                      */
/*  This is a helper class to wrap GDAL calls in MrSID interface.       */
/* ==================================================================== */
/************************************************************************/

class MrSIDDummyImageReader : public LTIImageReader
{
  public:
    MrSIDDummyImageReader(GDALDataset *poSrcDS);
    ~MrSIDDummyImageReader();
    LT_STATUS initialize();

    lt_int64 getPhysicalFileSize(void) const
    {
        return 0;
    };

  private:
    GDALDataset *poDS;
    GDALDataType eDataType;
    LTIDataType eSampleType;
    const LTIPixel *poPixel;

    double adfGeoTransform[6];

    virtual LT_STATUS decodeStrip(LTISceneBuffer &stripBuffer,
                                  const LTIScene &stripScene);

    virtual LT_STATUS decodeBegin(const LTIScene &)
    {
        return LT_STS_Success;
    };

    virtual LT_STATUS decodeEnd()
    {
        return LT_STS_Success;
    };
};

/************************************************************************/
/*                        MrSIDDummyImageReader()                       */
/************************************************************************/

MrSIDDummyImageReader::MrSIDDummyImageReader(GDALDataset *poSrcDS)
    : LTIImageReader(), poDS(poSrcDS)
{
    poPixel = nullptr;
}

/************************************************************************/
/*                        ~MrSIDDummyImageReader()                      */
/************************************************************************/

MrSIDDummyImageReader::~MrSIDDummyImageReader()
{
    if (poPixel)
        delete poPixel;
}

/************************************************************************/
/*                             initialize()                             */
/************************************************************************/

LT_STATUS MrSIDDummyImageReader::initialize()
{
    LT_STATUS eStat = LT_STS_Uninit;
#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 6
    if (!LT_SUCCESS(eStat = LTIImageReader::init()))
        return eStat;
#else
    if (!LT_SUCCESS(eStat = LTIImageReader::initialize()))
        return eStat;
#endif

    lt_uint16 nBands = (lt_uint16)poDS->GetRasterCount();
    LTIColorSpace eColorSpace = LTI_COLORSPACE_RGB;
    switch (nBands)
    {
        case 1:
            eColorSpace = LTI_COLORSPACE_GRAYSCALE;
            break;
        case 3:
            eColorSpace = LTI_COLORSPACE_RGB;
            break;
        default:
            eColorSpace = LTI_COLORSPACE_MULTISPECTRAL;
            break;
    }

    eDataType = poDS->GetRasterBand(1)->GetRasterDataType();
    switch (eDataType)
    {
        case GDT_UInt16:
            eSampleType = LTI_DATATYPE_UINT16;
            break;
        case GDT_Int16:
            eSampleType = LTI_DATATYPE_SINT16;
            break;
        case GDT_UInt32:
            eSampleType = LTI_DATATYPE_UINT32;
            break;
        case GDT_Int32:
            eSampleType = LTI_DATATYPE_SINT32;
            break;
        case GDT_Float32:
            eSampleType = LTI_DATATYPE_FLOAT32;
            break;
        case GDT_Float64:
            eSampleType = LTI_DATATYPE_FLOAT64;
            break;
        case GDT_Byte:
        default:
            eSampleType = LTI_DATATYPE_UINT8;
            break;
    }

    poPixel = new LTIDLLPixel<LTIPixel>(eColorSpace, nBands, eSampleType);
    if (!LT_SUCCESS(setPixelProps(*poPixel)))
        return LT_STS_Failure;

    if (!LT_SUCCESS(
            setDimensions(poDS->GetRasterXSize(), poDS->GetRasterYSize())))
        return LT_STS_Failure;

    if (poDS->GetGeoTransform(adfGeoTransform) == CE_None)
    {
#ifdef MRSID_SDK_40
        LTIGeoCoord oGeo(adfGeoTransform[0] + adfGeoTransform[1] / 2,
                         adfGeoTransform[3] + adfGeoTransform[5] / 2,
                         adfGeoTransform[1], adfGeoTransform[5],
                         adfGeoTransform[2], adfGeoTransform[4], nullptr,
                         poDS->GetProjectionRef());
#else
        LTIGeoCoord oGeo(adfGeoTransform[0] + adfGeoTransform[1] / 2,
                         adfGeoTransform[3] + adfGeoTransform[5] / 2,
                         adfGeoTransform[1], adfGeoTransform[5],
                         adfGeoTransform[2], adfGeoTransform[4],
                         poDS->GetProjectionRef());
#endif
        if (!LT_SUCCESS(setGeoCoord(oGeo)))
            return LT_STS_Failure;
    }

    /*int     bSuccess;
    double  dfNoDataValue = poDS->GetNoDataValue( &bSuccess );
    if ( bSuccess )
    {
        LTIPixel    oNoDataPixel( *poPixel );
        lt_uint16   iBand;

        for (iBand = 0; iBand < (lt_uint16)poDS->GetRasterCount(); iBand++)
            oNoDataPixel.setSampleValueFloat32( iBand, dfNoDataValue );
        if ( !LT_SUCCESS(setNoDataPixel( &oNoDataPixel )) )
            return LT_STS_Failure;
    }*/

    setDefaultDynamicRange();
#if !defined(LTI_SDK_MAJOR) || LTI_SDK_MAJOR < 8
    setClassicalMetadata();
#endif

    return LT_STS_Success;
}

/************************************************************************/
/*                             decodeStrip()                            */
/************************************************************************/

LT_STATUS MrSIDDummyImageReader::decodeStrip(LTISceneBuffer &stripData,
                                             const LTIScene &stripScene)

{
    const lt_int32 nXOff = stripScene.getUpperLeftCol();
    const lt_int32 nYOff = stripScene.getUpperLeftRow();
    const lt_int32 nBufXSize = stripScene.getNumCols();
    const lt_int32 nBufYSize = stripScene.getNumRows();
    const lt_int32 nDataBufXSize = stripData.getTotalNumCols();
    const lt_int32 nDataBufYSize = stripData.getTotalNumRows();
    const lt_uint16 nBands = poPixel->getNumBands();

    void *pData =
        CPLMalloc(nDataBufXSize * nDataBufYSize * poPixel->getNumBytes());
    if (!pData)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDDummyImageReader::decodeStrip(): "
                 "Cannot allocate enough space for scene buffer");
        return LT_STS_Failure;
    }

    poDS->RasterIO(GF_Read, nXOff, nYOff, nBufXSize, nBufYSize, pData,
                   nBufXSize, nBufYSize, eDataType, nBands, nullptr, 0, 0, 0,
                   nullptr);

    stripData.importDataBSQ(pData);
    CPLFree(pData);
    return LT_STS_Success;
}

/************************************************************************/
/*                          MrSIDCreateCopy()                           */
/************************************************************************/

static GDALDataset *MrSIDCreateCopy(const char *pszFilename,
                                    GDALDataset *poSrcDS, int bStrict,
                                    char **papszOptions,
                                    GDALProgressFunc pfnProgress,
                                    void *pProgressData)

{
    const char *pszVersion = CSLFetchNameValue(papszOptions, "VERSION");
#ifdef MRSID_HAVE_MG4WRITE
    int iVersion = pszVersion ? atoi(pszVersion) : 4;
#else
    int iVersion = pszVersion ? atoi(pszVersion) : 3;
#endif
    LT_STATUS eStat = LT_STS_Uninit;

#ifdef DEBUG
    bool bMeter = false;
#else
    bool bMeter = true;
#endif

    if (poSrcDS->GetRasterBand(1)->GetColorTable() != nullptr)
    {
        CPLError((bStrict) ? CE_Failure : CE_Warning, CPLE_NotSupported,
                 "MrSID driver ignores color table. "
                 "The source raster band will be considered as grey level.\n"
                 "Consider using color table expansion (-expand option in "
                 "gdal_translate)\n");
        if (bStrict)
            return nullptr;
    }

    MrSIDProgress oProgressDelegate(pfnProgress, pProgressData);
    if (LT_FAILURE(eStat = oProgressDelegate.setProgressStatus(0)))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDProgress.setProgressStatus failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

    // Create the file.
    MrSIDDummyImageReader oImageReader(poSrcDS);
    if (LT_FAILURE(eStat = oImageReader.initialize()))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDDummyImageReader.Initialize failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

    LTIGeoFileImageWriter *poImageWriter = nullptr;
    switch (iVersion)
    {
        case 2:
        {
            // Output Mrsid Version 2 file.
#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
            LTIDLLDefault<MG2ImageWriter> *poMG2ImageWriter;
            poMG2ImageWriter = new LTIDLLDefault<MG2ImageWriter>;
            eStat = poMG2ImageWriter->initialize(&oImageReader);
#else
            LTIDLLWriter<MG2ImageWriter> *poMG2ImageWriter;
            poMG2ImageWriter = new LTIDLLWriter<MG2ImageWriter>(&oImageReader);
            eStat = poMG2ImageWriter->initialize();
#endif
            if (LT_FAILURE(eStat))
            {
                delete poMG2ImageWriter;
                CPLError(CE_Failure, CPLE_AppDefined,
                         "MG2ImageWriter.initialize() failed.\n%s",
                         getLastStatusString(eStat));
                return nullptr;
            }

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
            eStat = poMG2ImageWriter->setEncodingApplication(
                "MrSID Driver", GDALVersionInfo("--version"));
            if (LT_FAILURE(eStat))
            {
                delete poMG2ImageWriter;
                CPLError(CE_Failure, CPLE_AppDefined,
                         "MG2ImageWriter.setEncodingApplication() failed.\n%s",
                         getLastStatusString(eStat));
                return nullptr;
            }
#endif

            poMG2ImageWriter->setUsageMeterEnabled(bMeter);

            poMG2ImageWriter->params().setBlockSize(
                poMG2ImageWriter->params().getBlockSize());

            // check for compression option
            const char *pszValue =
                CSLFetchNameValue(papszOptions, "COMPRESSION");
            if (pszValue != nullptr)
                poMG2ImageWriter->params().setCompressionRatio(
                    (float)CPLAtof(pszValue));

            poImageWriter = poMG2ImageWriter;

            break;
        }
        case 3:
        {
            // Output Mrsid Version 3 file.
#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
            LTIDLLDefault<MG3ImageWriter> *poMG3ImageWriter;
            poMG3ImageWriter = new LTIDLLDefault<MG3ImageWriter>;
            eStat = poMG3ImageWriter->initialize(&oImageReader);
#else
            LTIDLLWriter<MG3ImageWriter> *poMG3ImageWriter;
            poMG3ImageWriter = new LTIDLLWriter<MG3ImageWriter>(&oImageReader);
            eStat = poMG3ImageWriter->initialize();
#endif
            if (LT_FAILURE(eStat))
            {
                delete poMG3ImageWriter;
                CPLError(CE_Failure, CPLE_AppDefined,
                         "MG3ImageWriter.initialize() failed.\n%s",
                         getLastStatusString(eStat));
                return nullptr;
            }

#if defined(LTI_SDK_MAJOR) && LTI_SDK_MAJOR >= 8
            eStat = poMG3ImageWriter->setEncodingApplication(
                "MrSID Driver", GDALVersionInfo("--version"));
            if (LT_FAILURE(eStat))
            {
                delete poMG3ImageWriter;
                CPLError(CE_Failure, CPLE_AppDefined,
                         "MG3ImageWriter.setEncodingApplication() failed.\n%s",
                         getLastStatusString(eStat));
                return nullptr;
            }
#endif

            // usage meter should only be disabled for debugging
            poMG3ImageWriter->setUsageMeterEnabled(bMeter);

#if !defined(LTI_SDK_MAJOR) || LTI_SDK_MAJOR < 8
            // Set 64-bit Interface for large files.
            poMG3ImageWriter->setFileStream64(true);
#endif

            // set 2 pass optimizer option
            if (CSLFetchNameValue(papszOptions, "TWOPASS") != nullptr)
                poMG3ImageWriter->params().setTwoPassOptimizer(true);

            // set filesize in KB
            const char *pszValue = CSLFetchNameValue(papszOptions, "FILESIZE");
            if (pszValue != nullptr)
                poMG3ImageWriter->params().setTargetFilesize(atoi(pszValue));

            poImageWriter = poMG3ImageWriter;

            break;
        }
#ifdef MRSID_HAVE_MG4WRITE
        case 4:
        {
            // Output Mrsid Version 4 file.
            LTIDLLDefault<MG4ImageWriter> *poMG4ImageWriter;
            poMG4ImageWriter = new LTIDLLDefault<MG4ImageWriter>;
            eStat =
                poMG4ImageWriter->initialize(&oImageReader, nullptr, nullptr);
            if (LT_FAILURE(eStat))
            {
                delete poMG4ImageWriter;
                CPLError(CE_Failure, CPLE_AppDefined,
                         "MG3ImageWriter.initialize() failed.\n%s",
                         getLastStatusString(eStat));
                return nullptr;
            }

            eStat = poMG4ImageWriter->setEncodingApplication(
                "MrSID Driver", GDALVersionInfo("--version"));
            if (LT_FAILURE(eStat))
            {
                delete poMG4ImageWriter;
                CPLError(CE_Failure, CPLE_AppDefined,
                         "MG3ImageWriter.setEncodingApplication() failed.\n%s",
                         getLastStatusString(eStat));
                return nullptr;
            }

            // usage meter should only be disabled for debugging
            poMG4ImageWriter->setUsageMeterEnabled(bMeter);

            // set 2 pass optimizer option
            if (CSLFetchNameValue(papszOptions, "TWOPASS") != nullptr)
                poMG4ImageWriter->params().setTwoPassOptimizer(true);

            // set filesize in KB
            const char *pszValue = CSLFetchNameValue(papszOptions, "FILESIZE");
            if (pszValue != nullptr)
                poMG4ImageWriter->params().setTargetFilesize(atoi(pszValue));

            poImageWriter = poMG4ImageWriter;

            break;
        }
#endif /* MRSID_HAVE_MG4WRITE */
        default:
            CPLError(CE_Failure, CPLE_AppDefined,
                     "Invalid MrSID generation specified (VERSION=%s).",
                     pszVersion);
            return nullptr;
    }

    // set output filename
    poImageWriter->setOutputFileSpec(pszFilename);

    // set progress delegate
    poImageWriter->setProgressDelegate(&oProgressDelegate);

    // set defaults
    poImageWriter->setStripHeight(poImageWriter->getStripHeight());

    // set MrSID world file
    if (CSLFetchNameValue(papszOptions, "WORLDFILE") != nullptr)
        poImageWriter->setWorldFileSupport(true);

    // write the scene
    int nXSize = poSrcDS->GetRasterXSize();
    int nYSize = poSrcDS->GetRasterYSize();
    const LTIScene oScene(0, 0, nXSize, nYSize, 1.0);
    if (LT_FAILURE(eStat = poImageWriter->write(oScene)))
    {
        delete poImageWriter;
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MG2ImageWriter.write() failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

    delete poImageWriter;
    /* -------------------------------------------------------------------- */
    /*      Re-open dataset, and copy any auxiliary pam information.         */
    /* -------------------------------------------------------------------- */
    GDALPamDataset *poDS = (GDALPamDataset *)GDALOpen(pszFilename, GA_ReadOnly);

    if (poDS)
        poDS->CloneInfo(poSrcDS, GCIF_PAM_DEFAULT);

    return poDS;
}

#ifdef MRSID_J2K
/************************************************************************/
/*                           JP2CreateCopy()                            */
/************************************************************************/

static GDALDataset *JP2CreateCopy(const char *pszFilename, GDALDataset *poSrcDS,
                                  int bStrict, char **papszOptions,
                                  GDALProgressFunc pfnProgress,
                                  void *pProgressData)

{
#ifdef DEBUG
    bool bMeter = false;
#else
    bool bMeter = true;
#endif

    int nXSize = poSrcDS->GetRasterXSize();
    int nYSize = poSrcDS->GetRasterYSize();
    LT_STATUS eStat;

    if (poSrcDS->GetRasterBand(1)->GetColorTable() != nullptr)
    {
        CPLError((bStrict) ? CE_Failure : CE_Warning, CPLE_NotSupported,
                 "MrSID driver ignores color table. "
                 "The source raster band will be considered as grey level.\n"
                 "Consider using color table expansion (-expand option in "
                 "gdal_translate)\n");
        if (bStrict)
            return nullptr;
    }

    MrSIDProgress oProgressDelegate(pfnProgress, pProgressData);
    if (LT_FAILURE(eStat = oProgressDelegate.setProgressStatus(0)))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDProgress.setProgressStatus failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

    // Create the file.
    MrSIDDummyImageReader oImageReader(poSrcDS);
    eStat = oImageReader.initialize();
    if (eStat != LT_STS_Success)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "MrSIDDummyImageReader.Initialize failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

#if !defined(MRSID_POST5)
    J2KImageWriter oImageWriter(&oImageReader);
    eStat = oImageWriter.initialize();
#elif !defined(LTI_SDK_MAJOR) || LTI_SDK_MAJOR < 8
    JP2WriterManager oImageWriter(&oImageReader);
    eStat = oImageWriter.initialize();
#else
    JP2WriterManager oImageWriter;
    eStat = oImageWriter.initialize(&oImageReader);
#endif
    if (eStat != LT_STS_Success)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "J2KImageWriter.Initialize failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

#if !defined(LTI_SDK_MAJOR) || LTI_SDK_MAJOR < 8
    // Set 64-bit Interface for large files.
    oImageWriter.setFileStream64(true);
#endif

    oImageWriter.setUsageMeterEnabled(bMeter);

    // set output filename
    oImageWriter.setOutputFileSpec(pszFilename);

    // set progress delegate
    oImageWriter.setProgressDelegate(&oProgressDelegate);

    // Set defaults
    // oImageWriter.setStripHeight(oImageWriter.getStripHeight());

    // set MrSID world file
    if (CSLFetchNameValue(papszOptions, "WORLDFILE") != nullptr)
        oImageWriter.setWorldFileSupport(true);

    // check for compression option
    const char *pszValue = CSLFetchNameValue(papszOptions, "COMPRESSION");
    if (pszValue != nullptr)
        oImageWriter.params().setCompressionRatio((float)CPLAtof(pszValue));

    pszValue = CSLFetchNameValue(papszOptions, "XMLPROFILE");
    if (pszValue != nullptr)
    {
        LTFileSpec xmlprofile(pszValue);
        eStat = oImageWriter.params().readProfile(xmlprofile);
        if (eStat != LT_STS_Success)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "JPCWriterParams.readProfile failed.\n%s",
                     getLastStatusString(eStat));
            return nullptr;
        }
    }

    // write the scene
    const LTIScene oScene(0, 0, nXSize, nYSize, 1.0);
    eStat = oImageWriter.write(oScene);
    if (eStat != LT_STS_Success)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "J2KImageWriter.write() failed.\n%s",
                 getLastStatusString(eStat));
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Re-open dataset, and copy any auxiliary pam information.         */
    /* -------------------------------------------------------------------- */
    GDALOpenInfo oOpenInfo(pszFilename, GA_ReadOnly);
    GDALPamDataset *poDS = (GDALPamDataset *)JP2Open(&oOpenInfo);

    if (poDS)
        poDS->CloneInfo(poSrcDS, GCIF_PAM_DEFAULT);

    return poDS;
}
#endif /* MRSID_J2K */
#endif /* MRSID_ESDK */

/************************************************************************/
/*                        GDALRegister_MrSID()                          */
/************************************************************************/

void GDALRegister_MrSID()

{
    if (!GDAL_CHECK_VERSION("MrSID driver"))
        return;

    /* -------------------------------------------------------------------- */
    /*      MrSID driver.                                                   */
    /* -------------------------------------------------------------------- */
    if (GDALGetDriverByName(MRSID_DRIVER_NAME) != nullptr)
        return;

    GDALDriver *poDriver = new GDALDriver();
    MrSIDDriverSetCommonMetadata(poDriver);
#ifdef MRSID_ESDK
    poDriver->pfnCreateCopy = MrSIDCreateCopy;
#endif
    poDriver->pfnOpen = MrSIDOpen;

    GetGDALDriverManager()->RegisterDriver(poDriver);

/* -------------------------------------------------------------------- */
/*      JP2MRSID driver.                                                */
/* -------------------------------------------------------------------- */
#ifdef MRSID_J2K
    poDriver = new GDALDriver();
    JP2MrSIDDriverSetCommonMetadata(poDriver);
#ifdef MRSID_ESDK
    poDriver->pfnCreateCopy = JP2CreateCopy;
#endif
    poDriver->pfnOpen = JP2Open;

    GetGDALDriverManager()->RegisterDriver(poDriver);
#endif /* def MRSID_J2K */
}

#if defined(MRSID_USE_TIFFSYMS_WORKAROUND)
extern "C"
{

    /* This is not pretty but I am not sure how else to get the plugin to build
     * against the ESDK.  ESDK symbol dependencies bring in __TIFFmemcpy and
     * __gtiff_size, which are not exported from gdal.dll.  Rather than link
     * these symbols from the ESDK distribution of GDAL, or link in the entire
     * gdal.lib statically, it seemed safer and smaller to bring in just the
     * objects that wouldsatisfy these symbols from the enclosing GDAL build.
     * However, doing so pulls in a few more dependencies.  /Gy and /OPT:REF did
     * not seem to help things, so I have implemented no-op versions of these
     * symbols since they do not actually get called.  If the MrSID ESDK ever
     * comes to require the actual versions of these functions, we'll hope
     * duplicate symbol errors will bring attention back to this problem.
     */
    void TIFFClientOpen()
    {
    }

    void TIFFError()
    {
    }

    void TIFFGetField()
    {
    }

    void TIFFSetField()
    {
    }
}
#endif
