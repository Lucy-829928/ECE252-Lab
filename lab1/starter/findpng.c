#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lab_png.h"

typedef unsigned char U8;
typedef unsigned int U32;

int is_png (U8 *buf, size_t n) {
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
    if (!dir) return;

    struct dirent *entry;
    struct stat buf;
    char path[1000];
    int png_found = 0;
}