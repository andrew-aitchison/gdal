/******************************************************************************
 * 
 *
 * Author:  Andrew C Aitchison
 *
 ******************************************************************************
 * Copyright (c) 2019-2021, Andrew C Aitchison
 ******************************************************************************
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

// #ifdef FRMT_vrc

#include "VRC.h"

// Like strncmp but null bytes don't terminate.
// Used in verifySubTileMem()
static
size_t bytesmatch(const unsigned char*data, const unsigned char*pattern, size_t nLen)
{
    size_t count=0;
    for (  ; count<nLen && data[count]==pattern[count]; count++) {
    }
    return count;
}


void VRCRasterBand::read_VRC_Tile_36( VSILFILE *fp,
                                int block_xx, int block_yy,
                                void *pImage)
{
    auto *poGDS = dynamic_cast<VRCDataset *>(poDS);
    if (block_xx < 0 || block_xx >= nRasterXSize ) {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "read_VRC_Tile_36 invalid row %d", block_xx );
        return ;
    }
    if (block_yy < 0 || block_yy >= nRasterYSize ) {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "read_VRC_Tile_36 invalid column %d", block_yy );
        return ;
    }
    if (pImage == nullptr ) {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "read_VRC_Tile_36 passed no image" );
        return ;
    }
    if (poGDS->nMagic != vrc_magic36) {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "read_VRC_Tile_36 called with wrong magic number x%08x",
                  poGDS->nMagic );
        return ;
    }
        
    CPLDebug("Viewranger", "read_VRC_Tile_36(%p, %d, %d, %p)",
             static_cast<void*>(fp), block_xx, block_yy, pImage
             );

    int tilenum = poGDS->tileXcount * block_yy + block_xx;
    // VRC36_PIXEL_IS_PIXEL
    // this will be the default
    CPLDebug("Viewranger", "vrc36_pixel_is_pixel only partially implemented");
    unsigned int nTileIndex = poGDS->anTileIndex[tilenum];
    // CPLDebug("Viewranger", "vrcmetres_pixel_is_pixel");
    CPLDebug("Viewranger", "\tblock %d x %d, (%d, %d) tilenum %d tileIndex x%08x",
             nBlockXSize,
             nBlockYSize,
             block_xx, block_yy,
             tilenum,
             nTileIndex
             );

    if (nTileIndex==0) {
        // No data for this tile
        CPLDebug("Viewranger",
                 "read_VRC_Tile_36(.. %d %d ..) null tile",
                 block_xx, block_yy );

        if (eDataType==GDT_Byte) {
            for (int j=0; j < nBlockYSize ; j++) {
                int pixelnum = j * nBlockXSize;
                for (int i=0; i < nBlockXSize ; i++) {
                    static_cast<GUInt32*>(pImage)[pixelnum++] = nVRCNoData;
                }
            }
        } else {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "read_VRC_Tile_36 eDataType %d unexpected for null tile",
                      eDataType);
        }
        return;
    }  // nTileIndex==0 No data for this tile

    if ( VSIFSeekL( fp, nTileIndex, SEEK_SET ) ) {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "cannot seek to tile header x%08x", nTileIndex );
        return;
    }

    if (poGDS->nMapID != 8) {
        nOverviewCount = VRReadInt(fp);
        if (nOverviewCount != 7) {
            CPLDebug("Viewranger OVRV", "read_VRC_Tile_Metres: nOverviewCount is %d - expected seven - MapID %d",
                nOverviewCount,
                poGDS->nMapID
            );
            return;
        }

        unsigned int anTileOverviewIndex[7]={};
        for (int ii=0; ii<nOverviewCount; ii++) {
            anTileOverviewIndex[ii] = VRReadUInt(fp);
        }
        CPLDebug("Viewranger OVRV",
                 "x%08x:  x%08x x%08x x%08x x%08x  x%08x x%08x x%08x x%08x",
                 nTileIndex, nOverviewCount,
                 anTileOverviewIndex[0],  anTileOverviewIndex[1],
                 anTileOverviewIndex[2],  anTileOverviewIndex[3],
                 anTileOverviewIndex[4],  anTileOverviewIndex[5],
                 anTileOverviewIndex[6]
                 );

        // VRC counts main image plus 6 overviews.
        // GDAL just counts the 6 overview images.
        // anTileOverviewIndex[0] points to the full image
        // ..[1-6] are the overviews:
        nOverviewCount--; // equals 6

        // If the smallest overviews do not exist, ignore them.
        // This saves this driver generating them from larger overviews,
        // they may need to be generated elsewhere ...
        while (nOverviewCount>0 && 0==anTileOverviewIndex[nOverviewCount]) {
            nOverviewCount--;
        }
        if (nOverviewCount<6) {
            CPLDebug("Viewranger OVRV",
                     "Overviews %d-6 not available",
                     1+nOverviewCount);
        }

        if (nOverviewCount<1 || anTileOverviewIndex[0] == 0) {
            CPLDebug("Viewranger",
                     "VRCRasterBand::read_VRC_Tile_Metres(.. %d %d ..) empty tile",
                     block_xx, block_yy );
            return; 
        }
    
        // This is just for the developer's understanding.
        if (0x20 + nTileIndex == anTileOverviewIndex[1]) {
            CPLDebug("Viewranger",
                     "anTileOverviewIndex[1] %d x%08x - 0x20 = %d x%08x as expected",
                     anTileOverviewIndex[1], anTileOverviewIndex[1],
                     nTileIndex, nTileIndex);
        } else {
            CPLDebug("Viewranger",
                     "anTileOverviewIndex[1] %d x%08x - anTileOverviewIndex[0] %d x%08x = %d x%08x - expected 0x20",
                     anTileOverviewIndex[1], anTileOverviewIndex[1],  
                     nTileIndex,           nTileIndex,
                     anTileOverviewIndex[1] - nTileIndex,
                     anTileOverviewIndex[1] - nTileIndex);
        }

        dumpTileHeaderData(fp, nTileIndex,
                           1+static_cast<unsigned int>(nOverviewCount),
                           anTileOverviewIndex,
                           block_xx, block_yy );

        if (nThisOverview < -1 || nThisOverview >= nOverviewCount) {
            CPLDebug("Viewranger",
                     "read_VRC_Tile_36: overview %d=x%08x not in range [-1, %d]",
                     nThisOverview, nThisOverview, nOverviewCount);
            return;
        }

        if (anTileOverviewIndex[nThisOverview+1] >= poGDS->oStatBufL.st_size) {
            CPLDebug("Viewranger",
                     "\toverview level %d data beyond end of file at x%08x",
                     nThisOverview, anTileOverviewIndex[nThisOverview+1] );
            return ;
        }
        CPLDebug("Viewranger",
                 "\toverview level %d data at x%08x",
                 nThisOverview, anTileOverviewIndex[nThisOverview+1] );
    
        bool bTileShrink = (anTileOverviewIndex[nThisOverview+1]==0);
        // int nShrinkFactor=1;
        if (bTileShrink == false) {
            // nShrinkFactor = 1;
            if ( VSIFSeekL( fp, anTileOverviewIndex[nThisOverview+1], SEEK_SET ) ) {
                CPLError( CE_Failure, CPLE_AppDefined,
                          "cannot seek to overview level %d data at x%08x",
                          nThisOverview, anTileOverviewIndex[nThisOverview+1] );
                return;
            }

            unsigned int nTileMax = poGDS->tileSizeMax;
            unsigned int nTileMin = poGDS->tileSizeMin;
            if (nTileMax==0) {
                CPLError( CE_Failure, CPLE_NotSupported,
                          "tileSizeMax is zero and invalid"
                          );
                return;
            }
            if (nTileMin==0) {
                poGDS->tileSizeMin=nTileMax;
                CPLDebug("Viewranger",
                         "nTileMin is zero. Using nTileMax %d",
                         nTileMax
                         );
            } else {
            }
        } else { // if(bTileShrink == false)
            // If data for this block is not available, we need to rescale another overview
            // perhaps with GDALRegenerateOverviews from gcore/overview.cpp
            
            CPLDebug("Viewranger",
                     "Band %d block %d,%d empty at overview %d\n",
                     nBand, block_xx, block_yy, nThisOverview
                     );
            auto* hOvrBandSrc =
                // reinterpret_cast
                static_cast<GDALRasterBandH>(GetOverview( nThisOverview+1 ));
            GDALRasterBandH ahOvrBandTgts[1];
            ahOvrBandTgts[0] = // reinterpret_cast
                static_cast<GDALRasterBandH>(GetOverview( nThisOverview+2 ));
            if (hOvrBandSrc==nullptr || ahOvrBandTgts[0]==nullptr) {
                CPLDebug("Viewranger",
                         "SrcBand %p, TargetBand %p\n",
                         hOvrBandSrc, ahOvrBandTgts[0]
                         );
                return;
            }
            CPLErr regErr =
                GDALRegenerateOverviews
                (hOvrBandSrc,
                 1,             // nOverviewCount,
                 &ahOvrBandTgts[0],   // GDALRasterBandH
                 "AVERAGE",     // const char * pszResampling,
                 nullptr,       // GDALProgressFunc
                 nullptr        // void * pProgressData 
                 );
            if (regErr!=CE_None) {
                CPLDebug("Viewranger",
                         "Band %d block %d,%d downsampling for overview %d failed: %d\n",
                         nBand, block_xx, block_yy, nThisOverview, regErr
                         );
            } else {
                CPLDebug("Viewranger",
                         "Band %d block %d,%d downsampling for overview %d succeeded\n",
                         nBand, block_xx, block_yy, nThisOverview
                         );
            }
            return;
        } // end else clause of if(bTileShrink == false) 
    } // nMapID != 8

    // We have reached the start of the tile
    // ... but it is split into subtiles (of a format yet to be determined)
    int nRawXcount = VRReadInt(fp);
    int nRawYcount = VRReadInt(fp);
    int nRawXsize  = VRReadInt(fp);
    int nRawYsize  = VRReadInt(fp);

    if (nRawXcount <=0) {
        CPLDebug("Viewranger",
                 "nRawXcount %d zero or negative in tilenum %d",
                 nRawXcount, tilenum );
        return;
    }
    if (nRawYcount <=0) {
        CPLDebug("Viewranger",
                 "nRawYcount %d zero or negative in tilenum %d",
                 nRawYcount, tilenum );
        return;
    }
    if (nRawXsize <=0) {
        CPLDebug("Viewranger",
                 "nRawXsize %d zero or negative in tilenum %d",
                 nRawXsize, tilenum );
        return;
    }
    if (nRawYsize <=0) {
        CPLDebug("Viewranger",
                 "nRawYsize %d zero or negative in tilenum %d",
                 nRawYsize, tilenum );
        return;
    }

    if ( nRawXcount > nBlockXSize
         || nRawXsize > nBlockXSize
         || nRawXcount * nRawXsize > nBlockXSize
         ) {
        CPLDebug("Viewranger",
                 "nRawXcount %d x nRawXsize %d too big > nBlockXSize %d\tx%08x x x%08x > x%08x",
                 nRawXcount, nRawXsize, nBlockXSize,
                 nRawXcount, nRawXsize, nBlockXSize );
        // return;
    }
    if ( nRawYcount > nBlockYSize
         || nRawYsize > nBlockYSize
         || nRawYcount * nRawYsize > nBlockYSize
         ) {
        CPLDebug("Viewranger",
                 "nRawYcount %d x nRawYsize %d too big > nBlockYSize %d\tx%08x x x%08x > x%08x",
                 nRawYcount, nRawYsize, nBlockYSize,
                 nRawYcount, nRawYsize, nBlockYSize );
        // return;
    }

    CPLDebug("Viewranger",
             "nRawXcount %d nRawYcount %d nRawXsize %d nRawYsize %d",
             nRawXcount, nRawYcount, nRawXsize, nRawYsize);

    // Allow for under-height tiles
    {
        int nSkipTopRows = nBlockYSize - nRawYcount * nRawYsize;
        if (nSkipTopRows > 0) {
            CPLDebug("Viewranger",
                     "underheight tile nRawYcount %d x nRawYsize %d < blocksize %d",
                     nRawYcount, nRawYsize, nBlockYSize );
            //   This is a short (underheight) tile.
            // GDAL expects these at the top of the bottom tile,
            // but VRC puts these at the bottom of the top tile.
            //   We need to add a blank strip at the top of the
            // tile to compensate.

            for (int nPix=0; nPix<nSkipTopRows*nBlockXSize; nPix++) {
                static_cast<char*>(pImage)[nPix] = nVRCNoData;
            }
        } else if (nSkipTopRows != 0) {
            // This should not happen
            CPLDebug("Viewranger",
                     "OVERheight tile nRawYcount %d x nRawYsize %d > blocksize %d)",
                     nRawYcount, nRawYsize, nBlockYSize );
        }
    }  // Finished allowing for under-height tiles

    CPLDebug("Viewranger",
             "nRawXcount %d nRawYcount %d nRawXsize %d nRawYsize %d",
             nRawXcount, nRawYcount, nRawXsize, nRawYsize);

    // Read in this tile's index to ?raw? sub-tiles.
    std::vector<unsigned int> anSubTileIndex;
    anSubTileIndex.reserve
        (static_cast<size_t>(nRawXcount)*static_cast<size_t>(nRawYcount) +1);
    for (size_t loop=0;
         loop <= static_cast<size_t>(nRawXcount*nRawYcount);
         loop++) {
        anSubTileIndex[loop] = VRReadUInt(fp);
        if (anSubTileIndex[loop] >= poGDS->oStatBufL.st_size) {
            anSubTileIndex[loop] = 0;
        }
    }
    

    for (unsigned int loopX=0;
         loopX < static_cast<size_t>(nRawXcount);
         loopX++) {
        for (unsigned int loopY=0;
             loopY < static_cast<size_t>(nRawYcount);
             loopY++) {
            auto loop = static_cast<size_t>(nRawYcount)-1-loopY
                + static_cast<size_t>(loopX)*static_cast<size_t>(nRawYcount);

            //((GUInt32 *) pImage)[i] = poGDS->anSubTileIndex[loop];

            unsigned long nStart = anSubTileIndex[loop];
            unsigned long nFinish= anSubTileIndex[loop+1];
            unsigned long nFileSize = static_cast<unsigned int>
                (poGDS->oStatBufL.st_size);
            CPLString osBaseLabel
                = CPLString().Printf("/tmp/werdna/vrc2tif/%s.%03d.%03d.%08lu.%02u",
                                     // CPLGetBasename(poOpenInfo->pszFilename) doesn't quite work
                                     poGDS->sLongTitle.c_str(),
                                     loopX, loopY, nStart,
                                     nBand);

            if (/* 0<=nStart && */ nStart<=nFinish && nFinish <= nFileSize) {
                auto nRawSubtileSize = static_cast<size_t>(nRawXsize*nRawYsize);
                if (nRawSubtileSize>nFinish-nStart) {
                    nRawSubtileSize = nFinish-nStart;
                }
                auto *abySubTileData = static_cast<GByte *>(VSIMalloc(nRawSubtileSize));

                int seekres = VSIFSeekL( fp, nStart, SEEK_SET );
                if ( seekres ) {
                    CPLError( CE_Failure, CPLE_AppDefined,
                              "cannot seek to x%lx", nStart);
                    return;
                }
                size_t bytesread = VSIFReadL(abySubTileData, sizeof(GByte), nRawSubtileSize, fp);
                if (bytesread < nRawSubtileSize) {
                    CPLError(CE_Failure, CPLE_AppDefined,
                             "problem reading bytes [x%lx, x%lx)\n",
                             nStart,nFinish);
                    return;  
                }
            
                int nVerifyResult =
                    verifySubTileMem(abySubTileData,
                                     nStart, nFinish,
                                     block_xx, block_yy,
                                     static_cast<unsigned int >(loopX),
                                     static_cast<unsigned int >(loopY)
                                     );
                if (0==(nVerifyResult & ~0xff)) {
                    CPLDebug("Viewranger",
                             "raw data at x%08lx for tile (%d,%d) sub tile (%d,%d) did not verify\n",
                             nStart, block_xx, block_yy, loopX, loopY);
                }
                if ( VSIFSeekL( fp, nStart, SEEK_SET ) ) {
                    CPLError( CE_Failure, CPLE_AppDefined,
                              "cannot seek to start of tile (%d,%d) sub tile (%d,%d)",
                              block_xx, block_yy, loopX, loopY);
                    return;
                }

                // Allow for under-height tiles
                int nSkipTopRows =
                    nBlockYSize - static_cast<int>(nRawYcount * nRawYsize);
                if (nSkipTopRows > 0) {
                    CPLDebug("Viewranger",
                             "underheight tile nRawYcount %d x nRawYsize %d < blocksize %d)",
                             nRawYcount, nRawYsize, nBlockYSize );
                    //   This is a short (underheight) tile.
                    // GDAL expects these at the top of the bottom tile,
                    // but VRC puts these at the bottom of the top tile.
                    //   We need to add a blank strip at the top of the
                    // tile to compensate.
                    
                    for (int nPix=0; nPix<nSkipTopRows*nBlockXSize; nPix++) {
                        // This probably blanks all the subtiles in the top row,
                        // not just subtile loopX
                        (static_cast<char*>(pImage))[nPix] = nVRCNoData;
                    }
                } else if (nSkipTopRows != 0) {
                    // This should not happen
                    CPLDebug("Viewranger",
                             "OVERheight tile nRawYcount %d x nRawYsize %d > blocksize %d)",
                             nRawYcount, nRawYsize, nBlockYSize );
                    nSkipTopRows = 0;
                }
                
                // Write the raw data into the subtile of the image,
                // padding with the result of verifySubTileMem/File.
                auto nCount= static_cast<unsigned int>(nStart);
                for (int j=0; j < nRawYsize ; j++) {
                    int pixelnum =
                        (j + static_cast<signed int>(loopY)*nRawYsize+nSkipTopRows)
                        * nBlockXSize
                        + static_cast<int>(loopX)*nRawXsize;
                    for (int i=0; i < nRawXsize ; i++) {
                        if (pixelnum >= nBlockXSize*nBlockYSize) {
                            CPLDebug("Viewranger",
                                     "pixelnum %d > %x x %d - tile(%x,%d) loop(%x,%d) i=%d j=%d nCount=%d\n",
                                     pixelnum, nBlockXSize, nBlockYSize,
                                     block_xx, block_yy, loopX, loopY,
                                     i, j, nCount);
                            break;
                        }
                        if (nCount<nFinish) {
                            static_cast<GByte*>(pImage)[pixelnum] =
                                static_cast<GByte>(VRReadChar(fp));
                        } else {
                            static_cast<GByte*>(pImage)[pixelnum] =
                                static_cast<GByte>(nVerifyResult);
                            // static_cast<GByte*>(pImage)[pixelnum] =nf0count;
                        }
                        nCount++;
                        pixelnum++;
                    }
                }
                if(abySubTileData) {
                    VSIFree(abySubTileData);
                }            
            } else {
                CPLDebug("Viewranger",
                         "skipping %s: expected 0 <= x%lx <= x%lx <= x%lx filesize",
                         osBaseLabel.c_str(),
                         nStart, nFinish, nFileSize
                         );
            } // end range check 
        } // for loopY
    } // for loopX

    if (getenv("VRC_DUMP_TILE") && 1==nBand) {
        long nDumpCount = strtol(getenv("VRC_DUMP_TILE"),nullptr,10);
        // Dump first band of VRC tile as a (monochrome) .pgm.
        // The bands are currently all the same.
        CPLString osBaseLabel
            = CPLString().Printf("/tmp/werdna/vrc2tif/%s.%03d.%03d.%02u",
                                 // CPLGetBasename(poOpenInfo->pszFilename) doesn't quite work
                                 poGDS->sLongTitle.c_str(),
                                 block_xx, block_yy,
                                 nBand);
        
        dumpPPM(
                static_cast<unsigned int>(nBlockXSize),
                static_cast<unsigned int>(nBlockYSize),
                static_cast<unsigned char*>(pImage),
                static_cast<unsigned int>(nBlockXSize),
                osBaseLabel,
                band,
                static_cast<unsigned int>(nDumpCount)
                );
    }

} // VRCRasterBand::read_VRC_Tile_36


int VRCRasterBand::verifySubTileFile(
        VSILFILE *fp,
        unsigned long start,
        unsigned long finish,
        int nGDtile_xx,
        int nGDtile_yy,
        unsigned int nVRtile_xx,
        unsigned int nVRtile_yy
)
{
    CPLString osBaseLabel;
    osBaseLabel.Printf("/tmp/werdna/vrc2tif/%s.%03d.%03d.%03d.%03d.%08lu.%02u",
                       // CPLGetBasename(poOpenInfo->pszFilename) doesn't quite work
                       static_cast<VRCDataset *>(poDS)->sLongTitle.c_str(),
                       nGDtile_xx, nGDtile_yy,
                       nVRtile_xx, nVRtile_yy,
                       start, nBand);

    if (start>finish) {
        CPLDebug("Viewranger", "Backwards sub-tile: %lu>%lu bytes at %s",
                 start, finish, osBaseLabel.c_str());
        return -1;
    }

    int seekres = VSIFSeekL( fp, start, SEEK_SET );
    if ( seekres ) {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "cannot seek to x%lx", start);
        return -1;
    }

    auto nLen = static_cast<unsigned int>(finish-start);
    std::vector<GByte> abyRawSubtileData;
    abyRawSubtileData.reserve(nLen);
    size_t bytesread = VSIFReadL(abyRawSubtileData.data(), sizeof(GByte), nLen, fp);
    if (bytesread < nLen) {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "problem reading bytes [x%lx, x%lx)\n",
                 start,finish);
        return -1;  
    }

    return verifySubTileMem(abyRawSubtileData.data(),
                          start, finish,
                          nGDtile_xx, nGDtile_yy,
                          nVRtile_xx, nVRtile_yy );
} // VRCRasterBand::verifySubTileFile()

int VRCRasterBand::verifySubTileMem(
        GByte abyRawStartData[],
        unsigned long start,
        unsigned long finish,
        int nGDtile_xx,
        int nGDtile_yy,
        unsigned int nVRtile_xx,
        unsigned int nVRtile_yy
)
{
    CPLString osBaseLabel;
    osBaseLabel.Printf("/tmp/werdna/vrc2tif/%s.%03d.%03d.%03d.%03d.%08lu.%02u",
                       // CPLGetBasename(poOpenInfo->pszFilename) doesn't quite work
                       static_cast<VRCDataset *>(poDS)->sLongTitle.c_str(),
                       nGDtile_xx, nGDtile_yy,
                       nVRtile_xx, nVRtile_yy,
                       start, nBand);

    const unsigned char kacExpectedValues[144] = 
        {
         0x00, 0xbe, 0xe9, 0x42,        0x77, 0x64, 0x30, 0x21,
         0x3d, 0x5c, 0x2e, 0x34,        0x77, 0x46, 0x5a, 0x59,
         0x79, 0x24, 0x4b, 0x4b,        0x4e, 0x51, 0x38, 0x48,
         0x3d, 0x6d, 0x3c, 0x31,        0x36, 0x55, 0x27, 0x20,
                     
         0x66, 0x54, 0x47, 0x47,        0x69, 0x37, 0x5b, 0x55,
         0x5e, 0x5c, 0x17, 0x5d,        0x2e, 0x7f, 0x15, 0x39,
         0x2e, 0x4c, 0x0b, 0x1c,        0x51, 0x63, 0x79, 0x78,
         0x57, 0x09, 0x64, 0x5a,        0x5b, 0x6c, 0x02, 0x6f,
                     
         0x1c, 0x54, 0x13, 0x0d,        0x11, 0x72, 0xd4, 0xeb,
         0x71, 0x03, 0x5e, 0x58,        0x79, 0x24, 0x47,
         // Some USA sub-tiles only match up to here.
                                                          0x4b,
         // 80=x50 bytes
         0x4e, 0x52, 0x38, 0x48,        0x27, 0x4c, 0x2c, 0x33,
         0x22,
         // These 20 bytes ...
               0x72, 0x03, 0x18,        0x59, 0x68, 0x77, 0x77,
         0x56, 0x0b, 0x65, 0x6b,        0x6c, 0x69, 0x1a, 0x6a,
         0x1c, 0x4c, 0x1e, 0x0d,        0x10,
         // .. repeat ...
               0x72, 0x03, 0x18,        0x59, 0x68, 0x77, 0x77,
         0x56, 0x0b, 0x65, 0x6b,        0x6c, 0x69, 0x1a, 0x6a,
         0x1c, 0x4c, 0x1e, 0x0d,        0x10,
         // ... and 10 bytes again
               0x72, 0x03, 0x18,        0x59, 0x68, 0x77, 0x77,
         0x56, 0x0b, 0x65,
         //
                           0xbc,        0x84, 0x41, 0x23, 0x4a
        };

    if (start>finish) {
        CPLDebug("Viewranger", "Backwards sub-tile: %lu>%lu bytes at %s",
                 start, finish, osBaseLabel.c_str());
        return -1;
    }

    auto nLen = static_cast<unsigned int>(finish-start);
    unsigned int nHeadLen = 144;
    if (nLen < nHeadLen) {
        CPLDebug("Viewranger", "Short sub-tile: %u<144 bytes at x%lx %s",
                 nLen, start, osBaseLabel.c_str());       
        if (nLen>0) {
            nHeadLen = nLen;
        } else {
            nHeadLen=0;
        }
    }

    if (abyRawStartData==nullptr) {
        CPLDebug("Viewranger", "SubTile %s [%lu>%lu) has null ptr",
                 osBaseLabel.c_str(), start, finish
                 );
        return -1;
    }

    size_t nBytesMatched=bytesmatch(abyRawStartData, kacExpectedValues, nHeadLen);

    return 0x0100 | static_cast<int>(nBytesMatched);
} // VRCRasterBand::verifySubTileMem()

// #endif // def FRMT_vrc
