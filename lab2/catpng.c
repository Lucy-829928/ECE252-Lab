#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h> /* add for htonl() */
#include "catpng.h"
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"

#define MAX_T 9999999
#define PNG_SIG_SIZE 8
#define MAX_PNG_FILES 50

/* get all three chunks sequentially and stored to element pointer of struct pointer simple_PNG_p, return -1(get failed) or 0(get success) */
int load_png_chunks(simple_PNG_p out, image_segment_t * segment)
{
    unsigned char * data = segment->data;  /* pointer to mem buffer containing data of image segment, like a pointer that always pointed to the starter of file */
    size_t seg_size = segment->size;   /* size of segment */
    size_t offset = PNG_SIG_SIZE;        /* 8 bytes of signature of png */
    /* check PNG signature */
    if (memcmp(data, "\x89PNG\x0D\x0A\x1A\x0A", 8) != 0)
    {
        printf("Invalid PNG signature for segment %d\n", segment->sequence_num);
        return -1;
    }

    chunk_p chunk = NULL;

    /* read chunks sequentially until we find IEND */
    while (offset < seg_size)
    {
        /****  read chunk ****/
        if(offset + 8 > seg_size) /* 8 for 4 bytes length + 4 bytes type */
        {
            printf("Error: Invalid PNG chunk size.\n");
            return -1;
        }
        /* get length (U32 for 4 bytes) */
        U32 length = ntohl(*(U32 *)(data + offset));
        offset += 4; /* move to type */

        char type[5] = {0};
        memcpy(type, data + offset, 4); /* copy 4 bytes from starter of type?data + offset? */
        offset += 4; /* move to data */

        chunk = malloc(sizeof(struct chunk));
        if (!chunk)
        {
            printf("Error: Failed to allocate memory for chunk.\n");
            return -1;
        }

        /* store chunk length to chunk */
        chunk->length = length;
        /* store chunk type to chunk */
        memcpy(chunk->type, type, 4);
        /* store chunk data to chunk */
        chunk->p_data = malloc(length);
        memcpy(chunk->p_data, data + offset, length);
        /* store chunk crc to chunk */
        chunk->crc = ntohl(*(U32 *)(data + offset + length));

        /* store chunks to corresponding chunks of png */
        if (memcmp(type, "IHDR", 4) == 0)
        {
            out->p_IHDR = chunk;
        }
        else if (memcmp(type, "IDAT", 4) == 0)
        {
            out->p_IDAT = chunk;
        }
        else if (memcmp(type, "IEND", 4) == 0)
        {
            out->p_IEND = chunk;
            break; /* exit loop after find IEND */
        }
        else
        {
            free_chunk(chunk);
        }

        offset += length + 4; /* add length of data and crc(4 bytes) */
    }
    return 0;
}

void load_png_data_IHDR(struct data_IHDR * data_ihdr, U8 * segment_data, size_t sig_size, int seek_set)
{
    /* get 8 bytes (4bytes width & 4bytes height) from start of IHDR  */
    U8 * ihdr_chunk_start = segment_data + sig_size + seek_set; 

    // Extract the width and height from the IHDR chunk (first 8 bytes of the data).
    data_ihdr->width = ntohl(*(U32 *)(ihdr_chunk_start + 8));   /* width offset is 8, and last for 4 bytes */
    data_ihdr->height = ntohl(*(U32 *)(ihdr_chunk_start + 12)); /* height offset is 12, and last for 4 bytes */
}

int catpng (int num_segments, image_segment_t * segments)
{
    // Your code for the catpng program
    if (num_segments > MAX_PNG_FILES)
    {
        printf("Too many PNG files. Maximum allowed is %d\n", MAX_PNG_FILES);
        return -1;
    }

    // if (num_segments < 3)
    // {
    //     printf("not enough png for concatenating\n");
    //     return -1;
    // }

    int i;
    U8 *buffer_all = malloc(MAX_T);
    if (!buffer_all)
    {
        printf("Failed to allocate memory for buffer_all\n");
        return -1;
    }

    U32 total_height = 0;
    size_t total_size = 0;

    chunk_p new_ihdr = malloc(sizeof(struct chunk));
    chunk_p new_idat = malloc(sizeof(struct chunk));
    chunk_p new_iend = malloc(sizeof(struct chunk));
    if (!new_ihdr || !new_idat || !new_iend)
    {
        printf("Memory allocation for chunks failed\n");
        free(buffer_all);
        return -1;
    }
    new_ihdr->p_data = NULL;
    new_idat->p_data = NULL;
    new_iend->p_data = NULL;
    // new_iend->length = 1; /* set to a wrong number for testing if it is copied */

    for (i = 0; i < num_segments; i++)
    {
        U8 *segment_data = segments[i].data;
        // size_t segment_size = segments[i].size;
        if (!segment_data)
        {
            printf("Error: Missing segment data for segment %d\n", i);
            return -1;
        }

        /* create allocated simple_PNG_p & get chunks with sections */
        simple_PNG_p png = mallocPNG();
        if (load_png_chunks(png, &segments[i]) != 0)
        {
            printf("Error: Failed to load chunks for segment %d\n", segments[i].sequence_num);
            free_png(png);
            return -1;
        }
        
        /* check for IHDR chunk of png */
        if (png->p_IHDR == NULL)
        {
            printf("Error: IHDR chunk is missing or corrupted in the segment %d\n", segments[i].sequence_num);
            free(buffer_all);
            free(new_ihdr);
            free(new_idat);
            free(new_iend);
            return -1;
        }

        /* check for IHDR data */
        if (new_ihdr->p_data == NULL)
        {
            /* allocate memory for IHDR chunk */
            new_ihdr->p_data = malloc(png->p_IHDR->length);
            if (new_ihdr->p_data == NULL)
            {
                printf("Failed to allocate memory for IHDR data\n");
                free(buffer_all);
                free(new_ihdr);
                free(new_idat);
                free(new_iend);
                return -1;
            }

            /* copy IHDR */
            memcpy(new_ihdr, png->p_IHDR, sizeof(struct chunk));
            
            if (new_ihdr->p_data == NULL)
            {
                printf("Failed to allocate memory for IHDR data\n");
                free(buffer_all);
                free(new_ihdr->p_data);
                free(new_ihdr); /* free the chunk structure */
                return -1;
            }
            memcpy(new_ihdr->p_data, png->p_IHDR->p_data, png->p_IHDR->length); /* copy the actual data */
        }
        // else
        // {
        //     printf("IHDR data is not null\n");
        // }

        /* check if iend is empty */
        if (new_iend->length != 0)
        {
            /* copy IEND */
            memcpy(new_iend, png->p_IEND, sizeof(struct chunk));
        }
        // else
        // {
        //     printf("IEND data is not null\n");
        // }

        /* get IHDR section data details & calculate raw data size */
        struct data_IHDR data_ihdr;
        load_png_data_IHDR(&data_ihdr, segment_data, PNG_SIG_SIZE, 0);

        U32 height = data_ihdr.height;
        U32 width = data_ihdr.width;
        U64 raw_data_size = (U64)height * ((U64)width * 4 + 1);
        total_height += height;

        /* uncompress the data of IDAT section */
        U8 *inf_data = malloc(raw_data_size);
        if (!inf_data)
        {
            printf("Memory allocation for inf_data failed\n");
            free(buffer_all);
            free(new_ihdr->p_data);
            free(new_ihdr);
            free(new_iend);
            return -1;
        }

        if (mem_inf(inf_data, &raw_data_size, png->p_IDAT->p_data, png->p_IDAT->length) != 0)
        {
            printf("Failed to inflate IDAT data from image %d\n", segments[i].sequence_num);
            free_png(png);
            free(buffer_all);
            free(inf_data);
            return -1;
        }
        // printf("i = %d, total size: %ld, raw_data_size: %ld\n", i, total_size, raw_data_size);

        /* copy each raw data to buffer */
        // printf("total size: %ld, raw data size: %ld\n", total_size, raw_data_size);
        if (total_size + raw_data_size > MAX_T)
        {
            printf("File size exceed max size accepted\n");
            free_png(png);
            free(buffer_all);
            free(inf_data);
            return -1;
        }
        memcpy(buffer_all + total_size, inf_data, raw_data_size);
        total_size += raw_data_size;

        free_png(png);
        free(inf_data);
    }

    /* compressed buffer for all png */
    U64 def_size = total_size;
    U8 *def_data = malloc(def_size);

    if (!def_data)
    {
        printf("Failed to allocate memory for def_data\n");
        free(buffer_all);
        return -1;
    }

    if (mem_def(def_data, &def_size, buffer_all, total_size, Z_DEFAULT_COMPRESSION) != 0)
    {
        printf("Failed to deflate concatenated data\n");
        free(buffer_all);
        free(def_data);
        return -1;
    }

    /* to make sure pointers are initialized */
    if (!new_ihdr || !new_iend)
    {
        printf("Memory allocation for new chunks failed\n");
        free(buffer_all);
        if (new_ihdr)
        {
            free(new_ihdr);
        }
        if (new_idat && new_idat->p_data)
        {
            free(new_idat->p_data);
        }
        free(new_idat);
        if (new_iend)
        {
            free(new_iend);
        }
        return -1;
    }

    /* copy and modify IHDR chunk */
    if (new_ihdr->p_data == NULL)
    {
        printf("Memory allocation for new IHDR data failed\n");
        free(buffer_all);
        free(def_data);
        return -1;
    }
    else
    {
        U32 width = *(U32 *)(new_ihdr->p_data);
        *(U32 *)(new_ihdr->p_data + 4) = htonl(total_height); /* update height in IHDR by add 4 bytes(skip width) to address of p_data */
        *(U32 *)(new_ihdr->p_data) = width;
    }
    new_ihdr->crc = calculate_chunk_crc(new_ihdr);

    /* create new IDAT chunk */
    new_idat->length = def_size;       /* change length of IDAT chunk */
    memcpy(new_idat->type, "IDAT", 4); /* type of IDAT chunk remain the same */
    new_idat->p_data = malloc(def_size);
    if (new_idat->p_data == NULL)
    {
        printf("Memory allocation for new IDAT data failed\n");
        free(buffer_all);
        free(def_data);
        return -1;
    }
    memcpy(new_idat->p_data, def_data, def_size);  /* data of IDAT chunk changed */
    new_idat->crc = calculate_chunk_crc(new_idat); /* crc of IDAT chunk calculated */

    /* copy the IEND chunk */
    /* copied above */

    /* write the new png file */
    FILE *output_file = fopen("all.png", "wb+");
    if (!output_file)
    {
        printf("Failed to open output.png for writing\n");
        free(buffer_all);
        free(def_data);
        return -1;
    }
    /* write PNG signature */
    U8 png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(png_signature, 1, 8, output_file);

    /* write IHDR, IDAT, and IEND chunks */
    write_chunk(output_file, new_ihdr);
    write_chunk(output_file, new_idat);
    write_chunk(output_file, new_iend);

    /* close the file and free resources */
    fclose(output_file);
    free(buffer_all);
    free(def_data);
    free(new_ihdr->p_data);
    free(new_ihdr);
    free(new_idat->p_data);
    free(new_idat);
    free(new_iend);

    printf("Concatenated PNG written to all.png\n");

    return 0;
}