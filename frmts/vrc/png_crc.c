/*
 *
 *
 * http://www.libpng.org/pub/png/spec/1.2/PNG-CRCAppendix.html
 *
 */

#include "png_crc.h"

// This should be C not C++, so this is not needed ?
// ... unless compiled with something like $(CC) ... -std=c++11
CPL_C_START

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <string.h>

const uint32_t nBitsPerByte = 8;

//  Table of CRCs of all 8-bit messages.

// - gives clang-tidy warning [modernize-macro-to-enum]
// and constexpr requires C++ :-(
enum eTABLESIZE
{
    NCRC_TABLE_SIZE = 256
};

static uint32_t crc_table[NCRC_TABLE_SIZE]
    // clang-format off
= {
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
}
// clang-format on
;

/* Flag: has the table been computed? Initially false. */
// static
int crc_table_computed = 0;

/* Make the table for a fast CRC. */
static void make_crc_table(void)
{
    // unsigned int n, k;

    uint32_t const crc_magic = 0xedb88320L;

    for (unsigned int n = 0; n < NCRC_TABLE_SIZE; n++)
    {
        uint32_t c = (uint32_t)n;
        for (unsigned int k = 0; k < nBitsPerByte; k++)
        {
            if (c & 1U)
            {
                c = crc_magic ^ (c >> 1U);
            }
            else
            {
                c = c >> 1U;
            }
        }
        crc_table[n] = c;
    }
    crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
   should be initialized to all 1's, and the transmitted value
   is the 1's complement of the final running CRC (see the
   crc() routine below)). */

static uint32_t update_crc(const uint32_t crc, const unsigned char *buf,
                           const unsigned int len)
{
    uint32_t c = crc;

    if (!crc_table_computed)
    {
        make_crc_table();
    }
    const unsigned char fullbyte = 0xff;
    for (unsigned int n = 0; n < len; n++)
    {
        c = crc_table[(c ^ buf[n]) & fullbyte] ^ (c >> nBitsPerByte);
    }
    return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
extern uint32_t pngcrc_for_VRC(const unsigned char *buf, const unsigned int len)
{
    const uint32_t full32bits = 0xffffffffL;
    return update_crc(full32bits, buf, len) ^ full32bits;
}

CPL_C_END
