/******************************************************************************
 * $Id: VRCutils.h,v 1.6 2021/06/26 19:10:13 werdna Exp werdna $
 *
 * Author:  Andrew C Aitchison
 *
 ******************************************************************************
 * Copyright (c) 2020-21, Andrew C Aitchison
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

// Everything declared here is also declared in VRC.h
// #ifndef VRC_H_INCLUDED

#ifndef VRC_UTILS_H_INCLUDED
#define VRC_UTILS_H_INCLUDED

#include "gdal_pam.h"
#include "ogr_spatialref.h"
//#include "cpl_string.h"

int VRReadChar(VSILFILE *fp);
int VRReadInt(VSILFILE *fp);
void VRC_file_strerror_r(int nFileErr, char *buf, size_t buflen);

extern OGRSpatialReference* CRSfromCountry(int nCountry);
extern const char* CharsetFromCountry(int nCountry);

extern void
dumpTileHeaderData(
                   VSILFILE *fp,
                   unsigned int nTileIndex,
                   unsigned int nOverviewCount,
                   unsigned int anTileOverviewIndex[],
                   const int tile_xx, const int tile_yy );

extern short VRGetShort(const void* base, int byteOffset );
extern signed int VRGetInt(const void* base, unsigned int byteOffset );
extern unsigned int VRGetUInt(const void* base, unsigned int byteOffset );

extern int VRReadChar(VSILFILE *fp);
extern int VRReadShort(VSILFILE *fp);
extern int VRReadInt(VSILFILE *fp);
extern int VRReadInt(VSILFILE *fp, unsigned int byteOffset );
extern unsigned int VRReadUInt(VSILFILE *fp);
extern unsigned int VRReadUInt(VSILFILE *fp, unsigned int byteOffset );

#endif // ndef VRC_UTILS_H_INCLUDED

// #endif // ndef VRC_H_INCLUDED
