#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "lab_png.h"

#define PATH_MAX 4096

typedef unsigned char U8;

/* traverses directory and searches for PNG files */
void traverse_directory(const char *directory, int *png_found) {
    
    /* open directory */
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    struct stat buf;
    char path[PATH_MAX];

    /* read each entry in the directory */
    while ((entry = readdir(dir)) != NULL) {

        /* skip current and parent directories */
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            /* create full path of the entry */
            strcpy(path, directory);
            strcat(path, "/");
            strcat(path, entry->d_name);

            /* get status of the entry */
            if (stat(path, &buf) == -1) {
                perror("stat");
                continue;
            }

            /* if the entry is a directory, traverse it recursively */
            if (S_ISDIR(buf.st_mode)) {
                traverse_directory(path, png_found);
            } else {
                /* open file and read in binary mode */
                FILE *fp = fopen(path, "rb");

                if (fp == NULL) {
                    perror("fopen");
                    continue;
                }

                U8 header[PNG_SIG_SIZE];

                /* read first 8 bytes of the file */
                if (fread(header, 1, PNG_SIG_SIZE, fp) == PNG_SIG_SIZE) {
                    /* check if it is a PNG */
                    if (is_png(header, PNG_SIG_SIZE)) {
                        /* print path of PNG file */
                        printf("%s\n", path);
                       *png_found = 1;
                    }
                }

                /* close file */
                fclose(fp);
            }
        }
    }

    /* close directory */
    closedir(dir);
}


int main(int argc, char *argv[]) {

    /* check for correct number of arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    int png_found = 0;
    /* traverse directory */
    traverse_directory(argv[1], &png_found);

    /* if no PNG files in directory, print message */
    if (png_found == 0) {
        printf("findpng: No PNG file found\n");
    }

    return 0;
}