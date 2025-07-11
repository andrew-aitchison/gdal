/******************************************************************************
 *
 * Name:     Band.i
 * Project:  GDAL Python Interface
 * Purpose:  GDAL Core SWIG Interface declarations.
 * Author:   Kevin Ruland, kruland@ku.edu
 *
 ******************************************************************************
 * Copyright (c) 2005, Kevin Ruland
 *
 * SPDX-License-Identifier: MIT
 *****************************************************************************/

/************************************************************************
 *
 * Define the extensions for Band (nee GDALRasterBandShadow)
 *
*************************************************************************/

%{
/* Returned size is in bytes or 0 if an error occurred. */
static
GIntBig ComputeBandRasterIOSize (int buf_xsize, int buf_ysize, int nPixelSize,
                                 GIntBig nPixelSpace, GIntBig nLineSpace,
                                 int bSpacingShouldBeMultipleOfPixelSize )
{
    if (buf_xsize <= 0 || buf_ysize <= 0)
    {
        CPLError(CE_Failure, CPLE_IllegalArg, "Illegal values for buffer size");
        return 0;
    }

    if (nPixelSpace < 0 || nLineSpace < 0)
    {
        CPLError(CE_Failure, CPLE_IllegalArg, "Illegal values for space arguments");
        return 0;
    }

    if (nPixelSize == 0)
    {
        CPLError(CE_Failure, CPLE_IllegalArg, "Illegal value for data type");
        return 0;
    }

    if( nPixelSpace == 0 )
        nPixelSpace = nPixelSize;
    else if ( bSpacingShouldBeMultipleOfPixelSize && (nPixelSpace % nPixelSize) != 0 )
    {
        CPLError(CE_Failure, CPLE_IllegalArg, "nPixelSpace should be a multiple of nPixelSize");
        return 0;
    }

    if( nLineSpace == 0 )
    {
        nLineSpace = nPixelSpace * buf_xsize;
    }
    else if ( bSpacingShouldBeMultipleOfPixelSize && (nLineSpace % nPixelSize) != 0 )
    {
        CPLError(CE_Failure, CPLE_IllegalArg, "nLineSpace should be a multiple of nPixelSize");
        return 0;
    }

    GIntBig nRet = (GIntBig)(buf_ysize - 1) * nLineSpace + (GIntBig)(buf_xsize - 1) * nPixelSpace + nPixelSize;
#if SIZEOF_VOIDP == 4
    if (nRet > INT_MAX)
    {
        CPLError(CE_Failure, CPLE_IllegalArg, "Integer overflow");
        return 0;
    }
#endif

    return nRet;
}
%}

#if defined(SWIGPYTHON)
%{
static
CPLErr WriteRaster_internal( GDALRasterBandShadow *obj,
                             int xoff, int yoff, int xsize, int ysize,
                             int buf_xsize, int buf_ysize,
                             GDALDataType buf_type,
                             GIntBig buf_size, char *buffer,
                             GIntBig pixel_space, GIntBig line_space,
                             GDALRasterIOExtraArg* psExtraArg )
{
    GIntBig min_buffer_size = ComputeBandRasterIOSize (buf_xsize, buf_ysize, GDALGetDataTypeSizeBytes( buf_type ),
                                                   pixel_space, line_space, FALSE );
    if ( min_buffer_size == 0 )
      return CE_Failure;

    if ( buf_size < min_buffer_size ) {
      CPLError(CE_Failure, CPLE_AppDefined, "Buffer too small");
      return CE_Failure;
    }

    return GDALRasterIOEx( obj, GF_Write, xoff, yoff, xsize, ysize,
                           (void *) buffer, buf_xsize, buf_ysize, buf_type, pixel_space, line_space, psExtraArg );
}
%}

#endif

%rename (Band) GDALRasterBandShadow;

class GDALRasterBandShadow : public GDALMajorObjectShadow {
private:
  GDALRasterBandShadow();
  ~GDALRasterBandShadow();
public:
%extend {

%immutable;
  int XSize;
  int YSize;
  GDALDataType DataType;
%mutable;

  /* Interface method added for GDAL 1.12.0 */
  GDALDatasetShadow* GetDataset()
  {
    return (GDALDatasetShadow*) GDALGetBandDataset(self);
  }

  /* Interface method added for GDAL 1.7.0 */
  int GetBand()
  {
    return GDALGetBandNumber(self);
  }

%apply (int *OUTPUT){int *pnBlockXSize, int *pnBlockYSize}

  void GetBlockSize(int *pnBlockXSize, int *pnBlockYSize) {
      GDALGetBlockSize(self, pnBlockXSize, pnBlockYSize);
  }

#if defined(SWIGPYTHON)
  void GetActualBlockSize(int nXBlockOff, int nYBlockOff, int* pnxvalid, int* pnyvalid, int* pisvalid)
  {
    *pisvalid = (GDALGetActualBlockSize(self, nXBlockOff, nYBlockOff, pnxvalid, pnyvalid) == CE_None);
  }
#endif

  // Preferred name to match C++ API
  /* Interface method added for GDAL 1.7.0 */
  GDALColorInterp GetColorInterpretation() {
    return GDALGetRasterColorInterpretation(self);
  }

  // Deprecated name
  GDALColorInterp GetRasterColorInterpretation() {
    return GDALGetRasterColorInterpretation(self);
  }

  // Preferred name to match C++ API
  /* Interface method added for GDAL 1.7.0 */
  CPLErr SetColorInterpretation( GDALColorInterp val ) {
    return GDALSetRasterColorInterpretation( self, val );
  }

  // Deprecated name
  CPLErr SetRasterColorInterpretation( GDALColorInterp val ) {
    return GDALSetRasterColorInterpretation( self, val );
  }

  void GetNoDataValue( double *val, int *hasval ) {
    *val = GDALGetRasterNoDataValue( self, hasval );
  }

#ifdef SWIGPYTHON
  void GetNoDataValueAsInt64( GIntBig *val, int *hasval ) {
    *val = GDALGetRasterNoDataValueAsInt64( self, hasval );
  }

  void GetNoDataValueAsUInt64( GUIntBig *val, int *hasval ) {
    *val = GDALGetRasterNoDataValueAsUInt64( self, hasval );
  }
#endif

  CPLErr SetNoDataValue( double d) {
    return GDALSetRasterNoDataValue( self, d );
  }

#ifdef SWIGPYTHON
  CPLErr SetNoDataValueAsInt64( GIntBig v ) {
    return GDALSetRasterNoDataValueAsInt64( self, v );
  }

  CPLErr SetNoDataValueAsUInt64( GUIntBig v ) {
    return GDALSetRasterNoDataValueAsUInt64( self, v );
  }
#endif

  CPLErr DeleteNoDataValue() {
    return GDALDeleteRasterNoDataValue(self);
  }

  /* Interface method added for GDAL 1.7.0 */
  const char* GetUnitType() {
      return GDALGetRasterUnitType(self);
  }

  /* Interface method added for GDAL 1.8.0 */
  CPLErr SetUnitType( const char* val ) {
    return GDALSetRasterUnitType( self, val );
  }

  %apply (char **options) { (char **) };
  char** GetRasterCategoryNames( ) {
    return GDALGetRasterCategoryNames(self);
  }
  %clear (char **);

  %apply (char **options) { (char **names) };
  CPLErr SetRasterCategoryNames( char **names ) {
    return GDALSetRasterCategoryNames( self, names );
  }
  %clear (char **names);

  void GetMinimum( double *val, int *hasval ) {
    *val = GDALGetRasterMinimum( self, hasval );
  }

  void GetMaximum( double *val, int *hasval ) {
    *val = GDALGetRasterMaximum( self, hasval );
  }

  void GetOffset( double *val, int *hasval ) {
    *val = GDALGetRasterOffset( self, hasval );
  }

  void GetScale( double *val, int *hasval ) {
    *val = GDALGetRasterScale( self, hasval );
  }

  /* Interface method added for GDAL 1.8.0 */
  CPLErr SetOffset( double val ) {
    return GDALSetRasterOffset( self, val );
  }

  /* Interface method added for GDAL 1.8.0 */
  CPLErr SetScale( double val ) {
    return GDALSetRasterScale( self, val );
  }

%apply (double *OUTPUT){double *min, double *max, double *mean, double *stddev};
%apply (IF_ERROR_RETURN_NONE) { (CPLErr) };
  CPLErr GetStatistics( int approx_ok, int force,
                      double *min, double *max, double *mean, double *stddev ){
    if (min) *min = 0;
    if (max) *max = 0;
    if (mean) *mean = 0;
    if (stddev) *stddev = -1; /* This is the only way to recognize from Python if GetRasterStatistics() has updated the values */
    return GDALGetRasterStatistics( self, approx_ok, force,
				    min, max, mean, stddev );
  }
%clear (CPLErr);

  /* Interface method added for GDAL 1.7.0 */
%apply (double *OUTPUT){double *min, double *max, double *mean, double *stddev};
%apply (IF_ERROR_RETURN_NONE) { (CPLErr) };
%feature ("kwargs") ComputeStatistics;
  CPLErr ComputeStatistics( bool approx_ok, double *min, double *max, double *mean, double *stddev,
                            GDALProgressFunc callback = NULL, void* callback_data=NULL){
    return GDALComputeRasterStatistics( self, approx_ok, min, max, mean, stddev, callback, callback_data );
  }
%clear (CPLErr);

  CPLErr SetStatistics( double min, double max, double mean, double stddev ) {
    return GDALSetRasterStatistics( self, min, max, mean, stddev );
  }

  int GetOverviewCount() {
    return GDALGetOverviewCount(self);
  }

  GDALRasterBandShadow *GetOverview(int i) {
    return (GDALRasterBandShadow*) GDALGetOverview( self, i );
  }

  GDALRasterBandShadow *GetSampleOverview(GUIntBig nDesiredSamples) {
    return (GDALRasterBandShadow*) GDALGetRasterSampleOverviewEx( self, nDesiredSamples );
  }

#if defined (SWIGJAVA)
  int Checksum( int xoff, int yoff, int xsize, int ysize) {
    return GDALChecksumImage( self, xoff, yoff, xsize, ysize );
  }
#else
%apply (int *optional_int) {(int*)};
%feature ("kwargs") Checksum;
  int Checksum( int xoff = 0, int yoff = 0, int *xsize = 0, int *ysize = 0) {
    int nxsize = (xsize!=0) ? *xsize : GDALGetRasterBandXSize( self );
    int nysize = (ysize!=0) ? *ysize : GDALGetRasterBandYSize( self );
    return GDALChecksumImage( self, xoff, yoff, nxsize, nysize );
  }
%clear (int*);
#endif

#if defined(SWIGPYTHON)

%feature("kwargs") ComputeRasterMinMax;
  void ComputeRasterMinMax( double argout[2], int* isvalid, bool approx_ok = false, bool can_return_none = false) {
    *isvalid = GDALComputeRasterMinMax( self, approx_ok, argout ) == CE_None;
    if( !can_return_none && !*isvalid )
    {
        *isvalid = true;
        argout[0] = CPLAtof("nan");
        argout[1] = CPLAtof("nan");
    }
  }
%clear (CPLErr);

#else
  void ComputeRasterMinMax( double argout[2], int approx_ok = 0) {
    GDALComputeRasterMinMax( self, approx_ok, argout );
  }
#endif

  void ComputeBandStats( double argout[2], int samplestep = 1) {
    GDALComputeBandStats( self, samplestep, argout+0, argout+1,
                          NULL, NULL );
  }

  CPLErr Fill( double real_fill, double imag_fill =0.0 ) {
    return GDALFillRaster( self, real_fill, imag_fill );
  }

#if defined(SWIGPYTHON)
%apply (GIntBig nLen, char *pBuf) { (GIntBig buf_len, char *buf_string) };
%apply (GIntBig *optional_GIntBig) { (GIntBig*) };
%apply ( int *optional_int ) {(int*)};
#if defined(SWIGPYTHON)
%apply (GDALDataType *optional_GDALDataType) { (GDALDataType *buf_type) };
#else
%apply (int *optional_int) { (GDALDataType *buf_type) };
#endif
%feature( "kwargs" ) WriteRaster;
  CPLErr WriteRaster( int xoff, int yoff, int xsize, int ysize,
                      GIntBig buf_len, char *buf_string,
                      int *buf_xsize = 0,
                      int *buf_ysize = 0,
                      GDALDataType *buf_type = 0,
                      GIntBig *buf_pixel_space = 0,
                      GIntBig *buf_line_space = 0) {
    int nxsize = (buf_xsize==0) ? xsize : *buf_xsize;
    int nysize = (buf_ysize==0) ? ysize : *buf_ysize;
    GDALDataType ntype  = (buf_type==0) ? GDALGetRasterDataType(self)
                                        : *buf_type;
    GIntBig pixel_space = (buf_pixel_space == 0) ? 0 : *buf_pixel_space;
    GIntBig line_space = (buf_line_space == 0) ? 0 : *buf_line_space;
    GDALRasterIOExtraArg* psExtraArg = NULL;
    return WriteRaster_internal( self, xoff, yoff, xsize, ysize,
                                 nxsize, nysize, ntype, buf_len, buf_string, pixel_space, line_space, psExtraArg );
  }
%clear (GIntBig buf_len, char *buf_string);
%clear (GDALDataType *buf_type);
%clear (int*);
%clear (GIntBig*);
#endif

  void FlushCache() {
    GDALFlushRasterCache( self );
  }

  // Deprecated name
  GDALColorTableShadow *GetRasterColorTable() {
    return (GDALColorTableShadow*) GDALGetRasterColorTable( self );
  }

  // Preferred name to match C++ API
  GDALColorTableShadow *GetColorTable() {
    return (GDALColorTableShadow*) GDALGetRasterColorTable( self );
  }

  // Deprecated name
  int SetRasterColorTable( GDALColorTableShadow *arg ) {
    return GDALSetRasterColorTable( self, arg );
  }

  // Preferred name to match C++ API
  int SetColorTable( GDALColorTableShadow *arg ) {
    return GDALSetRasterColorTable( self, arg );
  }

  GDALRasterAttributeTableShadow *GetDefaultRAT() {
      return (GDALRasterAttributeTableShadow*) GDALGetDefaultRAT(self);
  }

  int SetDefaultRAT( GDALRasterAttributeTableShadow *table ) {
      return GDALSetDefaultRAT(self, table);
  }

  GDALRasterBandShadow *GetMaskBand() {
      return (GDALRasterBandShadow *) GDALGetMaskBand( self );
  }

  int GetMaskFlags() {
      return GDALGetMaskFlags( self );
  }

  CPLErr CreateMaskBand( int nFlags ) {
      return GDALCreateMaskBand( self, nFlags );
  }

  bool IsMaskBand() {
      return GDALIsMaskBand( self );
  }

#if defined(SWIGPYTHON)
%feature( "kwargs" ) GetHistogram;
  CPLErr GetHistogram( double min=-0.5,
                     double max=255.5,
                     int buckets=256,
                     GUIntBig *panHistogram = NULL,
                     int include_out_of_range = 0,
                     int approx_ok = 1,
                     GDALProgressFunc callback = NULL,
                     void* callback_data=NULL ) {
    CPLErrorReset();
    CPLErr err = GDALGetRasterHistogramEx( self, min, max, buckets, panHistogram,
                                         include_out_of_range, approx_ok,
                                         callback, callback_data );
    return err;
  }
#else
#ifndef SWIGJAVA
#if defined(SWIGCSHARP)
%apply (int inout[ANY]) {int *panHistogram};
#endif
%feature( "kwargs" ) GetHistogram;
  CPLErr GetHistogram( double min=-0.5,
                     double max=255.5,
                     int buckets=256,
                     int *panHistogram = NULL,
                     int include_out_of_range = 0,
                     int approx_ok = 1,
                     GDALProgressFunc callback = NULL,
                     void* callback_data=NULL ) {
    CPLErrorReset();
    CPLErr err = GDALGetRasterHistogram( self, min, max, buckets, panHistogram,
                                         include_out_of_range, approx_ok,
                                         callback, callback_data );
    return err;
  }
#if defined(SWIGCSHARP)
%clear int *panHistogram;
#endif
#endif
#endif

#if defined(SWIGPYTHON)
%feature ("kwargs") GetDefaultHistogram;
CPLErr GetDefaultHistogram( double *min_ret=NULL, double *max_ret=NULL, int *buckets_ret = NULL,
                            GUIntBig **ppanHistogram = NULL, int force = 1,
                            GDALProgressFunc callback = NULL,
                            void* callback_data=NULL ) {
    return GDALGetDefaultHistogramEx( self, min_ret, max_ret, buckets_ret,
                                    ppanHistogram, force,
                                    callback, callback_data );
}
#else
#ifndef SWIGJAVA
%feature ("kwargs") GetDefaultHistogram;
CPLErr GetDefaultHistogram( double *min_ret=NULL, double *max_ret=NULL, int *buckets_ret = NULL,
                            int **ppanHistogram = NULL, int force = 1,
			    GDALProgressFunc callback = NULL,
                            void* callback_data=NULL ) {
    return GDALGetDefaultHistogram( self, min_ret, max_ret, buckets_ret,
                                    ppanHistogram, force,
                                    callback, callback_data );
}
#endif
#endif

#if defined(SWIGPYTHON)
%apply (int nList, GUIntBig* pList) {(int buckets_in, GUIntBig *panHistogram_in)}
CPLErr SetDefaultHistogram( double min, double max,
                            int buckets_in, GUIntBig *panHistogram_in ) {
    return GDALSetDefaultHistogramEx( self, min, max,
                                    buckets_in, panHistogram_in );
}
%clear (int buckets_in, GUIntBig *panHistogram_in);
#else
#if defined(SWIGJAVA)
%apply (int nList, int* pList) {(int buckets_in, int *panHistogram_in)}
#endif
CPLErr SetDefaultHistogram( double min, double max,
       			    int buckets_in, int *panHistogram_in ) {
    return GDALSetDefaultHistogram( self, min, max,
    	   			    buckets_in, panHistogram_in );
}
#if defined(SWIGJAVA)
%clear (int buckets_in, int *panHistogram_in);
#endif
#endif

  /* Interface method added for GDAL 1.7.0 */
  bool HasArbitraryOverviews() {
      return (GDALHasArbitraryOverviews( self ) != 0) ? true : false;
  }

  /* Interface method added for GDAL 1.9.0 */
%apply (char **options) {char **};
  char **GetCategoryNames() {
    return GDALGetRasterCategoryNames( self );
  }
%clear char **;

%apply (char **options) { char ** papszCategoryNames };
  CPLErr SetCategoryNames( char ** papszCategoryNames ) {
    return GDALSetRasterCategoryNames( self, papszCategoryNames );
  }
%clear char **papszMetadata;

#if defined(SWIGPYTHON)
%feature( "kwargs" ) GetVirtualMem;
%newobject GetVirtualMem;
  CPLVirtualMemShadow* GetVirtualMem( GDALRWFlag eRWFlag,
                                      int nXOff, int nYOff,
                                      int nXSize, int nYSize,
                                      int nBufXSize, int nBufYSize,
                                      GDALDataType eBufType,
                                      size_t nCacheSize,
                                      size_t nPageSizeHint,
                                      char **options = NULL )
    {
        CPLVirtualMem* vmem = GDALRasterBandGetVirtualMem( self,
                                         eRWFlag,
                                         nXOff, nYOff,
                                         nXSize, nYSize,
                                         nBufXSize, nBufYSize,
                                         eBufType,
                                         0,
                                         0,
                                         nCacheSize,
                                         nPageSizeHint,
                                         FALSE,
                                         options );
        if( vmem == NULL )
            return NULL;
        CPLVirtualMemShadow* vmemshadow = (CPLVirtualMemShadow*)calloc(1, sizeof(CPLVirtualMemShadow));
        vmemshadow->vmem = vmem;
        vmemshadow->eBufType = eBufType;
        vmemshadow->bIsBandSequential = TRUE;
        vmemshadow->bReadOnly = (eRWFlag == GF_Read);
        vmemshadow->nBufXSize = nBufXSize;
        vmemshadow->nBufYSize = nBufYSize;
        vmemshadow->nBandCount = 1;
        return vmemshadow;
    }

%feature( "kwargs" ) GetVirtualMemAuto;
%newobject GetVirtualMemAuto;
  CPLVirtualMemShadow* GetVirtualMemAuto( GDALRWFlag eRWFlag,
                                          char **options = NULL )
    {
        int            nPixelSpace;
        GIntBig        nLineSpace;
        CPLVirtualMem* vmem = GDALGetVirtualMemAuto( self,
                                         eRWFlag,
                                         &nPixelSpace,
                                         &nLineSpace,
                                         options );
        if( vmem == NULL )
            return NULL;
        CPLVirtualMemShadow* vmemshadow = (CPLVirtualMemShadow*)calloc(1, sizeof(CPLVirtualMemShadow));
        vmemshadow->vmem = vmem;
        vmemshadow->eBufType = GDALGetRasterDataType( self );
        vmemshadow->bAuto = TRUE;
        vmemshadow->bReadOnly = (eRWFlag == GF_Read);
        vmemshadow->nBandCount = 1;
        vmemshadow->nPixelSpace = nPixelSpace;
        vmemshadow->nLineSpace = nLineSpace;
        vmemshadow->nBufXSize = GDALGetRasterBandXSize(self);
        vmemshadow->nBufYSize = GDALGetRasterBandYSize(self);
        return vmemshadow;
    }

%feature( "kwargs" ) GetTiledVirtualMem;
%newobject GetTiledVirtualMem;
  CPLVirtualMemShadow* GetTiledVirtualMem( GDALRWFlag eRWFlag,
                                      int nXOff, int nYOff,
                                      int nXSize, int nYSize,
                                      int nTileXSize, int nTileYSize,
                                      GDALDataType eBufType,
                                      size_t nCacheSize,
                                      char **options = NULL )
    {
        CPLVirtualMem* vmem = GDALRasterBandGetTiledVirtualMem( self,
                                         eRWFlag,
                                         nXOff, nYOff,
                                         nXSize, nYSize,
                                         nTileXSize, nTileYSize,
                                         eBufType,
                                         nCacheSize,
                                         FALSE,
                                         options );
        if( vmem == NULL )
            return NULL;
        CPLVirtualMemShadow* vmemshadow = (CPLVirtualMemShadow*)calloc(1, sizeof(CPLVirtualMemShadow));
        vmemshadow->vmem = vmem;
        vmemshadow->eBufType = eBufType;
        vmemshadow->bIsBandSequential = -1;
        vmemshadow->bReadOnly = (eRWFlag == GF_Read);
        vmemshadow->nBufXSize = nXSize;
        vmemshadow->nBufYSize = nYSize;
        vmemshadow->eTileOrganization = GTO_BSQ;
        vmemshadow->nTileXSize = nTileXSize;
        vmemshadow->nTileYSize = nTileYSize;
        vmemshadow->nBandCount = 1;
        return vmemshadow;
    }

#endif /* #if defined(SWIGPYTHON) */

#if defined(SWIGPYTHON)
    // Check with other bindings how to return both the integer status and
    // *pdfDataPct

    %apply (double *OUTPUT) {(double *)};
    int GetDataCoverageStatus( int nXOff, int nYOff,
                               int nXSize, int nYSize,
                               int nMaskFlagStop = 0,
                               double* pdfDataPct = NULL)
    {
        return GDALGetDataCoverageStatus(self, nXOff, nYOff,
                                         nXSize, nYSize,
                                         nMaskFlagStop,
                                         pdfDataPct);
    }
    %clear (double *);
#endif

%apply (int *optional_int) { (GDALDataType *buf_type) };
CPLErr AdviseRead(  int xoff, int yoff, int xsize, int ysize,
                    int *buf_xsize = 0, int *buf_ysize = 0,
                    GDALDataType *buf_type = 0,
                    char** options = NULL )
{
    int nxsize = (buf_xsize==0) ? xsize : *buf_xsize;
    int nysize = (buf_ysize==0) ? ysize : *buf_ysize;
    GDALDataType ntype;
    if ( buf_type != 0 ) {
      ntype = (GDALDataType) *buf_type;
    } else {
      ntype = GDALGetRasterDataType( self );
    }
    return GDALRasterAdviseRead(self, xoff, yoff, xsize, ysize,
                                nxsize, nysize, ntype, options);
}
%clear (GDALDataType *buf_type);
%clear (int band_list, int *pband_list );

%apply (double *OUTPUT){double *pdfRealValue, double *pdfImagValue};
#if !defined(SWIGPYTHON)
%apply (IF_ERROR_RETURN_NONE) { (CPLErr) };
#endif
  CPLErr InterpolateAtPoint( double pixel, double line,
                             GDALRIOResampleAlg interpolation,
                             double *pdfRealValue,
                             double *pdfImagValue ) {
    if (pdfRealValue) *pdfRealValue = 0;
    if (pdfImagValue) *pdfImagValue = 0;
    return GDALRasterInterpolateAtPoint( self, pixel, line, interpolation, pdfRealValue, pdfImagValue );
  }
#if !defined(SWIGPYTHON)
%clear (CPLErr);
#endif


%apply (double *OUTPUT){double *pdfRealValue, double *pdfImagValue};
#if !defined(SWIGPYTHON)
%apply (IF_ERROR_RETURN_NONE) { (CPLErr) };
#endif
%apply (char **options) { char ** transformerOptions };
  CPLErr InterpolateAtGeolocation( double geolocX, double geolocY,
                                   OSRSpatialReferenceShadow* srs,
                                   GDALRIOResampleAlg interpolation,
                                   double *pdfRealValue,
                                   double *pdfImagValue, char** transformerOptions = NULL ) {
    if (pdfRealValue) *pdfRealValue = 0;
    if (pdfImagValue) *pdfImagValue = 0;
    return GDALRasterInterpolateAtGeolocation( self, geolocX, geolocY,
                (OGRSpatialReferenceH)srs, interpolation,
                pdfRealValue, pdfImagValue, transformerOptions );
  }
#if !defined(SWIGPYTHON)
%clear (CPLErr);
%clear char ** transformerOptions;
#endif


%apply (double *OUTPUT){double *pdfMin, double *pdfMax};
%apply (int *OUTPUT){int *pnMinX, int *pnMinY};
%apply (int *OUTPUT){int *pnMaxX, int *pnMaxY};
#if !defined(SWIGPYTHON)
%apply (IF_ERROR_RETURN_NONE) { (CPLErr) };
#endif
  CPLErr ComputeMinMaxLocation( double *pdfMin, double *pdfMax,
                                int *pnMinX, int *pnMinY,
                                int *pnMaxX, int *pnMaxY ) {
    return GDALComputeRasterMinMaxLocation( self, pdfMin, pdfMax,
                                            pnMinX, pnMinY,
                                            pnMaxX, pnMaxY );
  }
#if !defined(SWIGPYTHON)
%clear (CPLErr);
#endif

%newobject AsMDArray;
  GDALMDArrayHS *AsMDArray()
  {
    return GDALRasterBandAsMDArray(self);
  }

  /* Internal use only! To be removed in GDAL 4.0 */
  void _EnablePixelTypeSignedByteWarning(bool b)
  {
      GDALEnablePixelTypeSignedByteWarning(self, b);
  }

  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject Add;
  GDALComputedRasterBandShadow* Add(GDALRasterBandShadow* other)
  {
      return GDALRasterBandAddBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject AddDouble;
  GDALComputedRasterBandShadow* AddDouble(double constant)
  {
      return GDALRasterBandAddDouble(self, constant);
  }

  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject Sub;
  GDALComputedRasterBandShadow* Sub(GDALRasterBandShadow* other)
  {
      return GDALRasterBandSubBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject SubDouble;
  GDALComputedRasterBandShadow* SubDouble(double constant)
  {
      return GDALRasterBandSubDouble(self, constant);
  }

  %newobject SubDoubleToBand;
  GDALComputedRasterBandShadow* SubDoubleToBand(double constant)
  {
      return GDALRasterBandSubDoubleToBand(constant, self);
  }

  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject Mul;
  GDALComputedRasterBandShadow* Mul(GDALRasterBandShadow* other)
  {
      return GDALRasterBandMulBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject MulDouble;
  GDALComputedRasterBandShadow* MulDouble(double constant)
  {
      return GDALRasterBandMulDouble(self, constant);
  }

  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject Div;
  GDALComputedRasterBandShadow* Div(GDALRasterBandShadow* other)
  {
      return GDALRasterBandDivBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject DivDouble;
  GDALComputedRasterBandShadow* DivDouble(double constant)
  {
      return GDALRasterBandDivDouble(self, constant);
  }

  %newobject DivDoubleByBand;
  GDALComputedRasterBandShadow* DivDoubleByBand(double constant)
  {
      return GDALRasterBandDivDoubleByBand(constant, self);
  }


  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject GreaterThan;
  GDALComputedRasterBandShadow* GreaterThan(GDALRasterBandShadow* other)
  {
      return GDALRasterBandGreaterThanBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject GreaterThanDouble;
  GDALComputedRasterBandShadow* GreaterThanDouble(double constant)
  {
      return GDALRasterBandGreaterThanDouble(self, constant);
  }


  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject GreaterOrEqualTo;
  GDALComputedRasterBandShadow* GreaterOrEqualTo(GDALRasterBandShadow* other)
  {
      return GDALRasterBandGreaterOrEqualToBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject GreaterOrEqualToDouble;
  GDALComputedRasterBandShadow* GreaterOrEqualToDouble(double constant)
  {
      return GDALRasterBandGreaterOrEqualToDouble(self, constant);
  }


  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject LesserThan;
  GDALComputedRasterBandShadow* LesserThan(GDALRasterBandShadow* other)
  {
      return GDALRasterBandLesserThanBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject LesserThanDouble;
  GDALComputedRasterBandShadow* LesserThanDouble(double constant)
  {
      return GDALRasterBandLesserThanDouble(self, constant);
  }

  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject LesserOrEqualTo;
  GDALComputedRasterBandShadow* LesserOrEqualTo(GDALRasterBandShadow* other)
  {
      return GDALRasterBandLesserOrEqualToBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject LesserOrEqualToDouble;
  GDALComputedRasterBandShadow* LesserOrEqualToDouble(double constant)
  {
      return GDALRasterBandLesserOrEqualToDouble(self, constant);
  }


  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject EqualTo;
  GDALComputedRasterBandShadow* EqualTo(GDALRasterBandShadow* other)
  {
      return GDALRasterBandEqualToBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject EqualToDouble;
  GDALComputedRasterBandShadow* EqualToDouble(double constant)
  {
      return GDALRasterBandEqualToDouble(self, constant);
  }


  %apply Pointer NONNULL {GDALRasterBandShadow* other};
  %newobject NotEqualTo;
  GDALComputedRasterBandShadow* NotEqualTo(GDALRasterBandShadow* other)
  {
      return GDALRasterBandNotEqualToBand(self, other);
  }
  %clear GDALRasterBandShadow* other;

  %newobject NotEqualToDouble;
  GDALComputedRasterBandShadow* NotEqualToDouble(double constant)
  {
      return GDALRasterBandNotEqualToDouble(self, constant);
  }

  %apply Pointer NONNULL {GDALRasterBandShadow* condBand};
  %apply Pointer NONNULL {GDALRasterBandShadow* thenBand};
  %apply Pointer NONNULL {GDALRasterBandShadow* elseBand};
  %newobject IfThenElse;
  static GDALComputedRasterBandShadow* IfThenElse(GDALRasterBandShadow* condBand,
                                                  GDALRasterBandShadow* thenBand,
                                                  GDALRasterBandShadow* elseBand)
  {
      return GDALRasterBandIfThenElse(condBand, thenBand, elseBand);
  }
  %clear GDALRasterBandShadow* condBand;
  %clear GDALRasterBandShadow* thenBand;
  %clear GDALRasterBandShadow* elseBand;


  %newobject AsType;
  GDALComputedRasterBandShadow* AsType(GDALDataType dt)
  {
      return GDALRasterBandAsDataType(self, dt);
  }

  %newobject MaximumOfNBands;
  %apply (int object_list_count, GDALRasterBandShadow **poObjects) {(int band_count, GDALRasterBandShadow **bands)};
  static GDALComputedRasterBandShadow* MaximumOfNBands(int band_count, GDALRasterBandShadow** bands)
  {
     return GDALMaximumOfNBands(band_count, bands);
  }
  %clear (int band_count, GDALRasterBandShadow **bands);

  %newobject MaxConstant;
  GDALComputedRasterBandShadow* MaxConstant(double constant)
  {
      return GDALRasterBandMaxConstant(self, constant);
  }

  %newobject MinimumOfNBands;
  %apply (int object_list_count, GDALRasterBandShadow **poObjects) {(int band_count, GDALRasterBandShadow **bands)};
  static GDALComputedRasterBandShadow* MinimumOfNBands(int band_count, GDALRasterBandShadow** bands)
  {
     return GDALMinimumOfNBands(band_count, bands);
  }
  %clear (int band_count, GDALRasterBandShadow **bands);

  %newobject MinConstant;
  GDALComputedRasterBandShadow* MinConstant(double constant)
  {
      return GDALRasterBandMinConstant(self, constant);
  }

  %newobject MeanOfNBands;
  %apply (int object_list_count, GDALRasterBandShadow **poObjects) {(int band_count, GDALRasterBandShadow **bands)};
  static GDALComputedRasterBandShadow* MeanOfNBands(int band_count, GDALRasterBandShadow** bands)
  {
     return GDALMeanOfNBands(band_count, bands);
  }
  %clear (int band_count, GDALRasterBandShadow **bands);


} /* %extend */

};

%{
GDALDataType GDALRasterBandShadow_DataType_get( GDALRasterBandShadow *h ) {
  return GDALGetRasterDataType( h );
}
int GDALRasterBandShadow_XSize_get( GDALRasterBandShadow *h ) {
  return GDALGetRasterBandXSize( h );
}
int GDALRasterBandShadow_YSize_get( GDALRasterBandShadow *h ) {
  return GDALGetRasterBandYSize( h );
}
%}

#if defined(SWIGPYTHON)
%pythoncode %{
del Band.Add
del Band.AddDouble
del Band.Sub
del Band.SubDouble
del Band.SubDoubleToBand
del Band.Mul
del Band.MulDouble
del Band.Div
del Band.DivDouble
del Band.DivDoubleByBand
del Band.GreaterThan
del Band.GreaterThanDouble
del Band.LesserThan
del Band.LesserThanDouble
del Band.GreaterOrEqualTo
del Band.GreaterOrEqualToDouble
del Band.LesserOrEqualTo
del Band.LesserOrEqualToDouble
del Band.EqualTo
del Band.EqualToDouble
del Band.NotEqualTo
del Band.NotEqualToDouble
del Band.AsType
del Band.MinimumOfNBands
del Band.MinConstant
del Band.MaximumOfNBands
del Band.MaxConstant
del Band.MeanOfNBands
del Band.IfThenElse
%}
#endif

/************************************************************************
 *
 * Define the extensions for ComputedBand (GDALComputedRasterBandShadow)
 *
*************************************************************************/

%rename (ComputedBand) GDALComputedRasterBandShadow;

class GDALComputedRasterBandShadow : public GDALRasterBandShadow {
private:
  GDALComputedRasterBandShadow();
public:
%extend {

  ~GDALComputedRasterBandShadow() {
      GDALComputedRasterBandRelease(self);
  }

} /* %extend */

};
