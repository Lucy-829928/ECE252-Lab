#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"

#define IMAGE_DIR "images/" /* for simplify input commands */

#define BUF_LEN (256 * 16)
#define BUF_LEN2 (256 * 32)

U8 gp_buf_def[BUF_LEN2]; /* output buffer for mem_def() */
U8 gp_buf_inf[BUF_LEN2]; /* output buffer for mem_inf() */

bool is_png(U8 *buf, size_t n)
{
    if (n < PNG_SIG_SIZE)
    {
        // printf("Not enough bytes for png\n");
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
        // printf("Incalcrc_calid IHDR chunk.\n");
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
        free(in->p_IHDR);  /* free the dynamically allocated memory for chunk data */
        in->p_IHDR = NULL; /* set the pointer to NULL to avoid dangling pointer issues */
    }
    if (in->p_IDAT != NULL)
    {
        free(in->p_IDAT);  /* free the dynamically allocated memory for chunk data */
        in->p_IDAT = NULL; /* set the pointer to NULL to avoid dangling pointer issues */
    }
    if (in->p_IEND != NULL)
    {
        free(in->p_IEND);  /* free the dynamically allocated memory for chunk data */
        in->p_IEND = NULL; /* set the pointer to NULL to avoid dangling pointer issues */
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

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("%s\n", "No file provided");
        return -1;
    }

    int i;
    // printf("A complete list of command line arguments:\n");
    for (i = 1; i < argc; i++) /* start at 1 to skip the program name */
    {
        /* calculate the required size for the full file path */
        size_t path_size = strlen(IMAGE_DIR) + strlen(argv[i]) + 1; /* strlen() will return a string length; +1 for the null terminator */

        /* allocate memory for the full path */
        char *filepath = malloc(path_size);
        if (filepath == NULL)
        {
            printf("Memory allocation for file failed\n");
            return -1;
        }

        /* construct the full file path */
        strcpy(filepath, IMAGE_DIR);
        strcat(filepath, argv[i]);

        FILE *file = fopen(filepath, "rb"); /* open file for reading & binary mode */
        if (file == NULL)
        {
            printf("%s: Failed to open file\n", argv[i]);
            free(filepath);
            continue;
        }

        /* refer to https://www.geeksforgeeks.org/c-program-to-read-contents-of-whole-file/ for binary mode oped file, use fread() is better */
        U8 buffer[8];
        size_t read_size = fread(buffer, 1, 8, file); /* get the first 8 bytes for `is_png()` */
        printf("%s: ", argv[i]);

        if (!is_png(buffer, read_size))
        {
            printf("Not a PNG file\n");
            fclose(file);
            free(filepath);
            continue;
        }
        // printf("location1: %ld\n", ftell(file));

        struct data_IHDR ihdr;
        // if (get_png_data_IHDR(&ihdr, file, 8, SEEK_SET) != 0) /* skip the first 8 bytes of png */
        // {
        //     // printf("location failed: %ld\n", ftell(file));
        //     // printf("Failed to read IHDR chunk\n"); /* fail to get IHDR data */
        //     goto error;
        // }
        get_png_data_IHDR(&ihdr, file, 8, SEEK_SET);
        // printf("location final: %ld\n", ftell(file));
        printf("%u x %u\n", ihdr.width, ihdr.height);

        simple_PNG_p png = mallocPNG();
        // if (get_png_chunks(png, file, 8, SEEK_SET) != 0)
        // {
        //     printf("Failed to get png chunks\n");
        //     goto error;
        // }
        get_png_chunks(png, file, 8, SEEK_SET);
        /* for IHDR chunk */
        U32 crc_cal = calculate_chunk_crc(png->p_IHDR);
        U32 crc = get_chunk_crc(png->p_IHDR);

        if (crc_cal != crc)
        {
            printf("%.4s chunk CRC error: computed %08x, expected %08x\n", png->p_IHDR->type, crc_cal, crc);
        }
        /* for IDAT chunk */
        crc_cal = calculate_chunk_crc(png->p_IDAT);
        crc = get_chunk_crc(png->p_IDAT);
        if (crc_cal != crc)
        {
            printf("%.4s chunk CRC error: computed %08x, expected %08x\n", png->p_IDAT->type, crc_cal, crc);
        }
        /* for IEND chunk */

        crc_cal = calculate_chunk_crc(png->p_IEND);
        crc = get_chunk_crc(png->p_IEND);
        if (crc_cal != crc)
        {
            printf("%.4s chunk CRC error: computed %08x, expected %08x\n", png->p_IEND->type, crc_cal, crc);
        }

        free_png(png);
        fclose(file);
        free(filepath);
    }

    return 0;
}