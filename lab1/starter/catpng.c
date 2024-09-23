#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h> /* add for htonl() */
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"

#define MAX_T 9999999
#define T 99999

int main(int argc, char *argv[])
{
    // Your code for the catpng program
    if (argc < 3)
    {
        printf("not enough png for concatenating\n");
        return -1;
    }

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
    new_iend->length = 1; /* set to a wrong number for testing if it is copied */

    for (i = 1; i < argc; i++)
    {
        FILE *file = fopen(argv[i], "rb");
        if (!file)
        {
            printf("%s: unable to open file\n", argv[i]);
            return -1;
        }

        U8 header[PNG_SIG_SIZE];
        /* read first 8 bytes of the file */
        if (fread(header, 1, PNG_SIG_SIZE, file) != PNG_SIG_SIZE)
        {
            printf("%s: unable to read file\n", argv[i]);
            free(buffer_all);
            free(new_ihdr);
            free(new_idat);
            free(new_iend);
            fclose(file);
            return -1;
        }

        /* check if it is a PNG */
        if (!is_png(header, PNG_SIG_SIZE))
        {
            printf("%s: not a PNG file\n", argv[i]);
            free(buffer_all);
            free(new_ihdr);
            free(new_idat);
            free(new_iend);
            fclose(file);
            return -1;
        }

        /* create allocated simple_PNG_p & get chunks with sections */
        simple_PNG_p png = mallocPNG();
        get_png_chunks(png, file, 8, SEEK_SET);
        if (png->p_IHDR == NULL || png->p_IEND == NULL || png->p_IDAT == NULL)
        {
            printf("%s: is corrupted or required chunk is missing\n", argv[i]);
            free_png(png);
            free(buffer_all);
            free(new_ihdr);
            free(new_idat);
            free(new_iend);
            return -1;
        }
        if (new_ihdr->p_data == NULL)
        {
            /* allocate and copy IHDR */
            memcpy(new_ihdr, png->p_IHDR, sizeof(struct chunk));
            new_ihdr->p_data = malloc(png->p_IHDR->length); /* allocate memory for the chunk data */
            if (new_ihdr->p_data == NULL)
            {
                printf("Failed to allocate memory for IHDR data\n");
                free(buffer_all);
                free(new_ihdr->p_data);
                free(new_ihdr); /* free the chunk structure */
                fclose(file);
                return -1;
            }
            memcpy(new_ihdr->p_data, png->p_IHDR->p_data, png->p_IHDR->length); /* copy the actual data */
        }
        // else
        // {
        //     printf("IHDR data is not null\n");
        // }
        
        if (new_iend->length != 0)
        {
            /* allocate and copy IEND */
            memcpy(new_iend, png->p_IEND, sizeof(struct chunk));
        }
        // else
        // {
        //     printf("IEND data is not null\n");
        // }

        /* get IHDR section data details & calculate raw data size */
        struct data_IHDR data_ihdr;
        get_png_data_IHDR(&data_ihdr, file, 8, SEEK_SET);
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
            printf("Failed to inflate IDAT data from %s\n", argv[i]);
            free_png(png);
            free(inf_data);
            fclose(file);
            return -1;
        }
        // printf("i = %d, total size: %ld, raw_data_size: %ld\n", i, total_size, raw_data_size);

        /* copy each raw data to buffer */
        if (total_size + raw_data_size > MAX_T)
        {
            printf("File size exceed max size, no png can be added\n");
            free_png(png);
            free(inf_data);
            fclose(file);
            break;
        }
        memcpy(buffer_all + total_size, inf_data, raw_data_size);
        total_size += raw_data_size;

        free_png(png);
        free(inf_data);
        fclose(file);
    }

    /* compressed buffer for all png */
    U64 def_size = T;
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
