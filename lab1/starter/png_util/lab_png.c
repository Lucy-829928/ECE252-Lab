#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <lab_png.h>
#include <crc.h>

bool is_png(U8 *buf, size_t n)
{
    if (n < PNG_SIG_SIZE)
    {
        return false;
    }
    U8 png_signature[PNG_SIG_SIZE] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}; /* first 8 bytes signature for png */

    int i;
    for (i = 0; i < 8; i++)
    {
        if (buf[i] != png_signature[i])
        {
            return false; /* mismatch, not png file */
        }
    }
    return true; /* matched, png file */
}

int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence) // takes in file pointer and how to reach data field of the IHDR chunk (see `fseek()` parameters)
{
    /* read the IHDR chunk length (4 bytes) and type (4 bytes) */
    U8 header[8];
    size_t read_count = fread(header, 1, 8, fp);
    if (read_count != 8)
    {
        return -1; /* Read error */
    }

    /* verify that this is the 'IHDR' chunk */
    if (header[4] != 'I' || header[5] != 'H' || header[6] != 'D' || header[7] != 'R')
    {
        return -1; /* not IHDR chunk */
    }

    U8 buffer[DATA_IHDR_SIZE];
    read_count = fread(buffer, 1, DATA_IHDR_SIZE, fp);
    if (read_count != DATA_IHDR_SIZE)
    {
        return -1; /* read error */
    }

    out->width = ((U32)buffer[0] << 24) | ((U32)buffer[1] << 16) | ((U32)buffer[2] << 8) | (U32)buffer[3];  /* 4 bytes */
    out->height = ((U32)buffer[4] << 24) | ((U32)buffer[5] << 16) | ((U32)buffer[6] << 8) | (U32)buffer[7]; /* 4 bytes */
    out->bit_depth = buffer[8];
    out->color_type = buffer[9];
    out->compression = buffer[10];
    out->filter = buffer[11];
    out->interlace = buffer[12];

    return 0; /* success */
}