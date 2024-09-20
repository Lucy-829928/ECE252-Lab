#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "lab_png.h"
#include "crc.h"

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
    fseek(fp, 0, SEEK_SET); /* initialize file pointer */
    fseek(fp, 8, whence);   /* skip the header file (8 bytes) */

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

int get_png_height(struct data_IHDR *buf)
{
    return buf->height;
}

int get_png_width(struct data_IHDR *buf)
{
    return buf->width;
}

int get_png_chunks(simple_PNG_p out, FILE *fp, long offset, int whence)
{
    fseek(fp, 0, SEEK_SET); /* initialize file pointer */
    fseek(fp, 8, SEEK_CUR); /* skip the header (8 bytes) */

    /* IHDR chunk (25 bytes) */
    U8 buf_IHDR[25];
    size_t read_count1 = fread(buf_IHDR, 1, 25, fp);
    if (read_count1 != 25)
    {
        return -1; /*error reading IHDR*/
    }
    out->p_IHDR->length = ((U32)buf_IHDR[0] << 24) | ((U32)buf_IHDR[1] << 16) | ((U32)buf_IHDR[2] << 8) | (U32)buf_IHDR[3]; /* 4 bytes */
    int i;
    for (i = 0; i < 4; i++)
    {
        out->p_IHDR->type[i] = buf_IHDR[i + 4]; /* 4 bytes array */
    }
    for (i = 0; i < 13; i++)
    {
        out->p_IHDR->p_data[i] = buf_IHDR[i + 8];
    }
    // out->p_IHDR->p_data = (buf_IHDR[8] << 96) | (buf_IHDR[9] << 88) | (buf_IHDR[10] << 80) | (buf_IHDR[11] << 72) | (buf_IHDR[12] << 64) |
    //                         (buf_IHDR[13] << 56) | (buf_IHDR[14] << 48) | (buf_IHDR[15] << 40) | (buf_IHDR[16] << 32) | (buf_IHDR[17] << 24) |
    //                         (buf_IHDR[18] << 16) | (buf_IHDR[19] << 8) | buf_IHDR[20]; /* 13 bytes */
    out->p_IHDR->crc = (buf_IHDR[21] << 24) | (buf_IHDR[22] << 16) | (buf_IHDR[23] << 8) | buf_IHDR[24];

    /* IDAT chunk first half part (8 bytes) */
    // U32 width = ((U32)buf_IHDR[8] << 24) | ((U32)buf_IHDR[9] << 16) | ((U32)buf_IHDR[10] << 8) | (U32)buf_IHDR[11];    /* get the width of image */
    // U32 height = ((U32)buf_IHDR[12] << 24) | ((U32)buf_IHDR[13] << 16) | ((U32)buf_IHDR[14] << 8) | (U32)buf_IHDR[15]; /* get the height of image */
    // size_t raw_data = height * (width * 4 + 1); /* calculate the raw data size (max) */
    // U8 *buf_IDAT = malloc(raw_data);
    U8 buf_IDAT[12];
    size_t read_count2 = fread(buf_IDAT, 1, 8, fp); /* add the first 8 bytes (length&type for IDAT) from current location */
    if (read_count2 != 8)
    {
        return -1; /* error reading IDAT chunk */
    }
    out->p_IDAT->length = ((U32)buf_IDAT[0] << 24) | ((U32)buf_IDAT[1] << 16) | ((U32)buf_IDAT[2] << 8) | (U32)buf_IDAT[3]; /* 4 bytes */
    for (i = 0; i < 4; i++)
    {
        out->p_IDAT->type[i] = buf_IDAT[i + 4]; /* 4 bytes array */
    }
    /* IDAT chunk last half part */
    out->p_IDAT->p_data = malloc(out->p_IDAT->length);
    if (out->p_IDAT->p_data == NULL)
    {
        return -1; /* memory allocation error */
    }
    size_t read_count3 = fread(out->p_IDAT->p_data, 1, out->p_IDAT->length, fp);
    if (read_count3 != out->p_IDAT->length)
    {
        free(out->p_IDAT->p_data);
        return -1; // Error reading IDAT data
    }
    fseek(fp, -16, SEEK_END); /* relocate pointer to the start of IDAT chunk's CRC part */
    size_t read_count4 = fread(buf_IDAT, 1, 4, fp);
    if (read_count4 != 4)
    {
        free(out->p_IDAT->p_data);
        return -1;
    }
    out->p_IDAT->crc = ((U32)buf_IDAT[8] << 24) | ((U32)buf_IDAT[9] << 16) | ((U32)buf_IDAT[10] << 8) | (U32)buf_IDAT[11]; /* 4 bytes */

    /* IEND chunk (12 bytes) */
    fseek(fp, -12, SEEK_END); /* relocate pointer to the start of IEND chunk */
    U8 buf_IEND[12];
    size_t read_count5 = fread(buf_IEND, 1, 12, fp);
    if (read_count5 != 12)
    {
        free(out->p_IDAT->p_data);
        return -1; /* error reading IEND */
    }
    out->p_IEND->length = 0x00000000; /* 4 bytes, length is 0 */
    for (i = 0; i < 4; i++)
    {
        out->p_IEND->type[i] = buf_IEND[i + 4]; /* 4 bytes array */
    }
    out->p_IEND->crc = (buf_IEND[8] << 24) | (buf_IEND[9] << 16) | (buf_IEND[10] << 8) | buf_IEND[11];

    return 0;
}

// chunk_p get_chunk(FILE *fp)
// {
// }

// U32 get_chunk_crc(chunk_p in)
// {
// }

// U32 calculate_chunk_crc(chunk_p in)
// {
// }

// simple_PNG_p mallocPNG()
// {
// }