#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <lab_png.h>
#include <crc.h>

#define IMAGE_DIR "images/"

/* we assume that the bytes we got is always 8 bytes, the other possibility is discussed in main() */
bool is_png(U8 *buf, size_t n)
{
    if (n < PNG_SIG_SIZE)
    {
        printf("Not enough bytes for png\n");
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
    printf("location2: %ld\n", ftell(fp));

    /* verify that this is the 'IHDR' chunk */
    if (header[4] != 'I' || header[5] != 'H' || header[6] != 'D' || header[7] != 'R')
    {
        printf("Invalid IHDR chunk.\n");
        return -1; /* not IHDR chunk */
    }

    U8 buffer[DATA_IHDR_SIZE];
    read_count = fread(buffer, 1, DATA_IHDR_SIZE, fp);
    if (read_count != DATA_IHDR_SIZE)
    {
        return -1; /* read error */
    }
    printf("location3: %ld\n", ftell(fp));

    out->width = ((U32)buffer[0] << 24) | ((U32)buffer[1] << 16) | ((U32)buffer[2] << 8) | (U32)buffer[3];  /* 4 bytes */
    out->height = ((U32)buffer[4] << 24) | ((U32)buffer[5] << 16) | ((U32)buffer[6] << 8) | (U32)buffer[7]; /* 4 bytes */
    out->bit_depth = buffer[8];
    out->color_type = buffer[9];
    out->compression = buffer[10];
    out->filter = buffer[11];
    out->interlace = buffer[12];

    return 0; /* success */
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
        printf("location1: %ld\n", ftell(file));

        struct data_IHDR ihdr;
        if (get_png_data_IHDR(&ihdr, file, 8, SEEK_CUR) != 0) /* use `SEEK_CUR` since the current location is at 9th byte*/
        {
            printf("location failed: %ld\n", ftell(file));
            printf("Failed to read IHDR chunk\n"); /* fail to get IHDR data */
            fclose(file);
            free(filepath);
            continue;
        }
        printf("location final: %ld\n", ftell(file));
        
        printf("%u x %u\n", ihdr.width, ihdr.height);

        fclose(file);
        free(filepath);
    }

    return 0;
}
