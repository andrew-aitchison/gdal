/*
 *
 */

#pragma once

#ifndef PNG_CRC_H_INCLUDED
#define PNG_CRC_H_INCLUDED

#include <cpl_port.h>
#include <stdint.h>

CPL_C_START

#include <png.h>

/* Return the PNG CRC of the bytes buf[0..len-1]. */
extern uint32_t pngcrc_for_VRC(const unsigned char *buf, unsigned int len);

CPL_C_END

#endif
