/******************************************************************************
 *
 *
 * Author:  Andrew C Aitchison
 *
 ******************************************************************************
 * Copyright (c) 2020-24, Andrew C Aitchison
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

#pragma clang diagnostic push
// #pragma clang diagnostic ignored "-Wformat-pedantic"
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-dtor"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma clang diagnostic ignored "-Winconsistent-missing-destructor-override"
#pragma clang diagnostic ignored "-Wpadded"
#pragma clang diagnostic ignored "-Wreserved-id-macro"
// #pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wsuggest-destructor-override"
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
// #if defined(__clang__)
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
// #endif
#pragma clang diagnostic ignored "-Wunused-template"
#pragma clang diagnostic ignored "-Wweak-vtables"
#include <gdal_pam.h>
#pragma clang diagnostic pop
#include <ogr_spatialref.h>
// #include <cpl_string.h>

void VRC_file_strerror_r(int nFileErr, char *buf, size_t buflen);

extern OGRSpatialReference *CRSfromCountry(int16_t nCountry, int32_t nMapID,
                                           const char *szCountry);
extern const char *CharsetFromCountry(int16_t nCountry);

extern void dumpTileHeaderData(VSILFILE *fp, unsigned int nTileIndex,
                               unsigned int nOverviewCount,
                               unsigned int anTileOverviewIndex[], int tile_xx,
                               int tile_yy);

extern short VRGetShort(const void *base, int byteOffset);
extern int32_t VRGetInt(const void *base, unsigned int byteOffset);
extern uint32_t VRGetUInt(const void *base, unsigned int byteOffset);

extern int VRReadChar(VSILFILE *fp);
extern int VRReadShort(VSILFILE *fp);
extern int32_t VRReadInt(VSILFILE *fp);
extern int32_t VRReadInt(VSILFILE *fp, unsigned int byteOffset);
extern uint32_t VRReadUInt(VSILFILE *fp);
extern uint32_t VRReadUInt(VSILFILE *fp, unsigned int byteOffset);

#endif

// #endif
