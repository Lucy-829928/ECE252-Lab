#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lab_png.h"

typedef unsigned char U8;
typedef unsigned int U32;

/* int is_pn (U8 *buf, size_t n) {
    FILE *fp;
    U8 png_header[8];

    fp = fopen(buf, "rb");
    fread(png_header, sizeof(unsigned char), 8, fp);
    fclose(fp);

    if (png_header[0] == 0x89 && png_header[1] == 0x50 && png_header[2] == 0x4E && png_header[3] == 0x47 && png_header[4] == 0x0D && png_header[5] == 0x0A && png_header[6] == 0x1A && png_header[7] == 0x0A) {
        return 1;
    } else {
        return 0;
    }
}

*/

// bool is_png(U8 *buf, size_t n)
// {
//     if (n < PNG_SIG_SIZE)
//     {
//         return false;
//     }
//     U8 png_signature[PNG_SIG_SIZE] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}; /* first 8 bytes signature for png */

//     int i;
//     for (i = 0; i < 8; i++)
//     {
//         if (buf[i] != png_signature[i])
//         {
//             return false; /* mismatch, not png file */
//         }
//     }
//     return true; /* matched, png file */
// }

void check_file (const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return;

    U8 png_header[PNG_SIG_SIZE];

    if (fread(png_header, 1, PNG_SIG_SIZE, fp) == PNG_SIG_SIZE) {
        if (is_png(png_header, PNG_SIG_SIZE)) {
            printf("%s\n", filepath);
        }
    }

    fclose(fp);
}

void traverse_directory (const char *directory) {
    DIR *dir = opendir(directory);
    struct dirent *entry;
    struct stat buf;
    char path[4096];
    int png_found = 0;

    while((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        if (lstat(path, &buf) == -1) {
            continue;
        }

        if (S_ISREG(buf.st_mode)) {
            check_file(path);
            png_found = 1;
        } else if (S_ISDIR(buf.st_mode)) {
            traverse_directory(path);
        }
    }

    closedir(dir);

    if (!png_found) {
        printf("findpng: No PNG file found\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    traverse_directory(argv[1]);
    return 0;
}