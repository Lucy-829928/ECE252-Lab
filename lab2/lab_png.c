#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h> /* for eg. ntohl() */
#include "lab_png.h"
#include "crc.h"

/* return true(png file) or false(not png file), input parameters are binary array's pointer(first 8 bytes to compared with signature) read from file and the length of read bytes */
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

/* return -1(get failed) or 0(get success), parameters are pointer pointed to IHDR chunk's data section, pointer to file, offset and whence for fseek() */
int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence) // takes in file pointer and how to reach data field of the IHDR chunk (see `fseek()` parameters)
{
    // fseek(fp, 0, SEEK_SET); /* initialize file pointer */
    fseek(fp, offset, whence); /* skip the header file (8 bytes) */

    /* read the IHDR chunk length (4 bytes) and type (4 bytes) */
    U8 header[8];
    size_t read_count = fread(header, 1, 8, fp);
    if (read_count != 8)
    {
        return -1; /* Read error */
    }
    // printf("location2: %ld\n", ftell(fp));

    /* verify that this is the 'IHDR' chunk */
    if (header[4] != 'I' || header[5] != 'H' || header[6] != 'D' || header[7] != 'R')
    {
        // printf("Invalid IHDR chunk.\n");
        return -1; /* not IHDR chunk */
    }

    U8 buffer[DATA_IHDR_SIZE];
    read_count = fread(buffer, 1, DATA_IHDR_SIZE, fp);
    if (read_count != DATA_IHDR_SIZE)
    {
        return -1; /* read error */
    }
    // printf("location3: %ld\n", ftell(fp));

    out->width = ((U32)buffer[0] << 24) | ((U32)buffer[1] << 16) | ((U32)buffer[2] << 8) | (U32)buffer[3];  /* 4 bytes */
    out->height = ((U32)buffer[4] << 24) | ((U32)buffer[5] << 16) | ((U32)buffer[6] << 8) | (U32)buffer[7]; /* 4 bytes */
    out->bit_depth = buffer[8];
    out->color_type = buffer[9];
    out->compression = buffer[10];
    out->filter = buffer[11];
    out->interlace = buffer[12];

    return 0; /* success */
}
// int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence) // takes in file pointer and how to reach data field of the IHDR chunk (see `fseek()` parameters)
// {
//     simple_PNG_p png = mallocPNG(); /* allocate memory for simple_PNG struct */

//     /* extract all PNG chunks (IHDR, IDAT, IEND) */
//     if (get_png_chunks(png, fp, offset, whence) != 0)
//     {
//         printf("Failed to get PNG chunks\n");
//         free_png(png);
//         return -1;
//     }

//     /* IHDR chunk must be present, check if it's there */
//     if (png->p_IHDR == NULL)
//     {
//         printf("IHDR chunk not found\n");
//         free_png(png);
//         return -1;
//     }

//     /* cannot do this since big endian need to change to host byte order, use ntohl() instead */
//     // memcpy(&out->width, data_ihdr, 4);      /* copy first 4 bytes to width */
//     // memcpy(&out->height, data_ihdr + 4, 4); /* copy next 4 bytes to height */
//     // IHDR data section (png->p_IHDR->p_data) should contain 13 bytes

//     /* create a pointer pointed to the data section of certain IHDR chunk */
//     U8 * data_ihdr = png->p_IHDR->p_data;

//     /* read the first 4 bytes for width, convert from big-endian to host order */
//     out->width = ntohl(*(U32 *)(data_ihdr)); /* 4 bytes buf[0]~buf[3] */
//     out->height = ntohl(*(U32 *)(data_ihdr + 4)); /* 4 bytes buf[4]~buf[7] */
//     out->bit_depth = *(data_ihdr + 8); /* 1 byte buf[8] */
//     out->color_type = *(data_ihdr + 9); /* 1 byte buf[9] */
//     out->compression = *(data_ihdr + 10); /* 1 byte buf[10] */
//     out->filter = *(data_ihdr + 11); /* 1 byte buf[11] */
//     out->interlace = *(data_ihdr + 12); /* 1 byte buf[12] */

//     return 0;
// }

/* return height of png, parameter is the IHDR data section */
int get_png_height(struct data_IHDR *buf)
{
    if (buf == NULL)
    {
        printf("Error: data_IHDR is NULL\n");
        return 0;
    }
    return buf->height;
}

/* return width of png, parameter is the IHDR data section */
int get_png_width(struct data_IHDR *buf)
{
    if (buf == NULL)
    {
        printf("Error: data_IHDR is NULL\n");
        return 0;
    }
    return buf->width;
}

/* get all three chunks sequentially and stored to element pointer of struct pointer simple_PNG_p, return -1(get failed) or 0(get success) */
int get_png_chunks(simple_PNG_p out, FILE *fp, long offset, int whence)
{
    /* move the file pointer to the offset position (right after PNG signature, offset = 8, whence = SEEK_SET) */
    if (fseek(fp, offset, whence) != 0)
    {
        // perror("fseek failed");
        return -1;
    }

    chunk_p chunk = NULL;

    /* read chunks sequentially until we find IEND */
    while (1)
    {
        chunk = get_chunk(fp);
        if (chunk == NULL)
        {
            if (feof(fp))
            {
                // printf("reached end of file\n");
                break; /* stop when end of file reached */
            }
            // printf("Error reading chunk, continuing...\n");
            continue; /* continue reading next chunk if there is an error */
        }

        /* check chunk type */
        if (memcmp(chunk->type, "IHDR", 4) == 0)
        {
            out->p_IHDR = chunk; /* store IHDR chunk */
        }
        else if (memcmp(chunk->type, "IDAT", 4) == 0)
        {
            out->p_IDAT = chunk; /* store IDAT chunk */
        }
        else if (memcmp(chunk->type, "IEND", 4) == 0)
        {
            out->p_IEND = chunk; /* store IEND chunk */
            return 0;            /* stop when IEND is found */
        }
        else
        {
            /* free chunk if it's not IHDR, IDAT, or IEND */
            free_chunk(chunk);
        }
    }

    /* something went wrong if reach here (e.g., IEND not found) */
    // printf("Error: IEND chunk not found\n");
    return -1;
}

/* get detailed section for chunk read from file(pointer), return updated chunk or NULL */
chunk_p get_chunk(FILE *fp)
{
    chunk_p chunk = malloc(sizeof(struct chunk)); /* allocate memory for chunk */
    if (!chunk)
    {
        // perror("Failed to allocate memory for chunk");
        return NULL;
    }

    /* read length (4 bytes) */
    if (fread(&chunk->length, 1, CHUNK_LEN_SIZE, fp) != CHUNK_LEN_SIZE)
    {
        // printf("Failed to read chunk length: read size != expected %d\n", CHUNK_LEN_SIZE);

        // perror("Failed to read chunk length");
        free(chunk);
        return NULL;
    }
    chunk->length = ntohl(chunk->length); /* convert length from big endian to host byte order */

    // printf("Chunk length = %u\n", chunk->length);

    /* read type (4 bytes) */
    if (fread(chunk->type, 1, CHUNK_TYPE_SIZE, fp) != CHUNK_TYPE_SIZE)
    {
        // perror("Failed to read chunk type");
        free(chunk);
        return NULL;
    }

    /* allocate memory for data */
    if (chunk->length > 0)
    {
        chunk->p_data = malloc(chunk->length);
        if (!chunk->p_data && chunk->length > 0) /* check for non-zero length when allocating data */
        {
            // perror("Failed to allocate memory for chunk data");
            free(chunk);
            return NULL;
        }

        /* read data (chunk->length bytes) */
        if (chunk->length > 0 && fread(chunk->p_data, 1, chunk->length, fp) != chunk->length)
        {
            // perror("Failed to read chunk data");
            free(chunk->p_data);
            free(chunk);
            return NULL;
        }
    }
    else
    {
        chunk->p_data = NULL; // change 3: handle zero-length data chunks
    }

    /* read CRC (4 bytes) */
    if (fread(&chunk->crc, 1, CHUNK_CRC_SIZE, fp) != CHUNK_CRC_SIZE)
    {
        // perror("Failed to read chunk CRC");
        free(chunk->p_data);
        free(chunk);
        return NULL;
    }
    chunk->crc = ntohl(chunk->crc); /* convert CRC from network to host byte order */

    // if (chunk->p_data != NULL)
    // {
    //     printf("Chunk data not NULL, ");
    // }
    // printf("Chunk type: %.4s, length: %u\n", chunk->type, chunk->length);

    return chunk;
}

/* return crc of chunk, parameter is pointer to chunk */
U32 get_chunk_crc(chunk_p in)
{
    if (in == NULL)
    {
        // printf("Error: chunk is NULL\n");
        return 0;
    }
    return in->crc;
}

/* calculate the crc of input chunk */
U32 calculate_chunk_crc(chunk_p in)
{
    if (in == NULL /*|| in->p_data == NULL*/)
    {
        // printf("Error: chunk or data is NULL\n");
        return 0;
    }

    /* first, concatenate the type and data for CRC calculation */
    U32 type_data_len = CHUNK_TYPE_SIZE + in->length; /* type&data length = type (4 bytes) + data length */
    U8 *buf = malloc(type_data_len);                  /* allocate buffer for type + data */
    if (buf == NULL)
    {
        // perror("malloc failed");
        return 0;
    }

    /* copy the type to the buffer (4 bytes) */
    memcpy(buf, in->type, CHUNK_TYPE_SIZE); /* copy from in->type to buf for 4 bytes*/

    /* copy the data to the buffer (data length) */
    memcpy(buf + CHUNK_TYPE_SIZE, in->p_data, in->length); /* copy from in->p_data to buf(4 bytes after) for in->length byte */

    /* calculate crc */
    U32 computed_crc = crc(buf, type_data_len); /* from crc.c */

    free(buf);
    return computed_crc;
}

/* allocate memory for the simple_PN    G structure */
simple_PNG_p mallocPNG()
{
    simple_PNG_p png = malloc(sizeof(struct simple_PNG));
    if (png == NULL)
    {
        // perror("Failed to allocate memory for simple_PNG\n");
        return NULL; /* memory allocation fails */
    }

    /* initialize the chunk pointers to NULL */
    png->p_IHDR = NULL;
    png->p_IDAT = NULL;
    png->p_IEND = NULL;

    return png; /* return the allocated and initialized structure */
}

/* free simple_PNG_p(struct pointer) */
void free_png(simple_PNG_p in) // free the memory of a struct simple_PNG
{
    if (in == NULL)
    {
        return; /* nothing to free */
    }

    /* free the element which is pointer if it has been allocated */
    if (in->p_IHDR != NULL)
    {
        if (in->p_IHDR->p_data != NULL)
        {
            free(in->p_IHDR->p_data); /* free chunk's data */
        }                 
        free(in->p_IHDR); /* free the dynamically allocated memory for chunk data */
        in->p_IHDR = NULL;  /* avoid double free */
    }
    if (in->p_IDAT != NULL)
    {
        if (in->p_IDAT->p_data != NULL)
        {
            free(in->p_IDAT->p_data); /* free chunk's data */
        }
        free(in->p_IDAT); /* free the dynamically allocated memory for chunk data */
        in->p_IDAT = NULL; /* avoid double free */
    }

    if (in->p_IEND != NULL)
    {
        if (in->p_IEND->p_data != NULL)
        {
            free(in->p_IEND->p_data); /* free chunk's data */
        }
        free(in->p_IEND); /* free the dynamically al    located memory for chunk data */
        in->p_IEND = NULL; /* avoid double free */
    }

    /* free the chunk structure itself */
    free(in);
}

/* free chunk_p(struct pointer) */
void free_chunk(chunk_p in)
{
    if (in == NULL)
    {
        return; /* nothing to free */
    }

    /* free the element which is pointer if it has been allocated */
    if (in->p_data != NULL)
    {
        free(in->p_data); /* free the dynamically allocated memory for chunk data */
    }

    /* free the chunk structure itself */
    free(in);
}

/* write a struct simple_PNG to file, return -1(write failed) or 0(write success) */
int write_PNG(char *filepath, simple_PNG_p in)
{
    if (in == NULL)
    {
        printf("Error: PNG structure is NULL\n");
        return -1;
    }

    FILE *fp = fopen(filepath, "bw+");
    if (fp == NULL)
    {
        perror("Failed to open file for writing");
        return -1;
    }

    /* first write PNG signature (8 bytes) */
    const U8 png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (fwrite(png_signature, 1, 8, fp) != 8)
    {
        perror("Failed to write PNG signature");
        fclose(fp);
        return -1;
    }

    /* second write IHDR chunk */
    if (in->p_IHDR != NULL)
    {
        if (write_chunk(fp, in->p_IHDR) != 0)
        {
            printf("Failed to write IHDR chunk\n");
            fclose(fp);
            return -1;
        }
    }

    /* third write IDAT chunk */
    if (in->p_IDAT != NULL)
    {
        if (write_chunk(fp, in->p_IDAT) != 0)
        {
            printf("Failed to write IDAT chunk\n");
            fclose(fp);
            return -1;
        }
    }

    /* last write IEND chunk */
    if (in->p_IEND != NULL)
    {
        if (write_chunk(fp, in->p_IEND) != 0)
        {
            printf("Failed to write IEND chunk\n");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0; /* write success */
}

/* write a struct chunk to file, return -1(write failed) or 0(write success) */
int write_chunk(FILE *fp, chunk_p in)
{
    if (in == NULL || fp == NULL)
    {
        printf("Error: chunk or file pointer is NULL\n");
        return -1;
    }

    /* first write the chunk length (4 bytes) in big-endian order */
    U32 length_be = htonl(in->length); /* convert length to big-endian */
    if (fwrite(&length_be, 1, CHUNK_LEN_SIZE, fp) != CHUNK_LEN_SIZE)
    {
        perror("Failed to write chunk length");
        return -1;
    }

    /* second write the chunk type (4 bytes) */
    if (fwrite(in->type, 1, CHUNK_TYPE_SIZE, fp) != CHUNK_TYPE_SIZE)
    {
        perror("Failed to write chunk type");
        return -1;
    }

    /* third write the chunk data (data length) */
    if (fwrite(in->p_data, 1, in->length, fp) != in->length)
    {
        perror("Failed to write chunk data");
        return -1;
    }

    /* last write the chunk CRC (4 bytes) in big-endian order */
    U32 crc_be = htonl(in->crc); /* convert CRC to big-endian */
    if (fwrite(&crc_be, 1, CHUNK_CRC_SIZE, fp) != CHUNK_CRC_SIZE)
    {
        perror("Failed to write chunk CRC");
        return -1;
    }

    return 0; /* write success */
}
