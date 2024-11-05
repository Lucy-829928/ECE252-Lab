#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <arpa/inet.h> /* add for htonl() */
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"

#define NUM_STRIPS 50
#define MAX_URL_LENGTH 256
#define DEFAULT_THREAD 1
#define DEFAULT_PNG 1

#define DATA_SIZE 10240 /* set a fixed size so that we don't need malloc, because for different process, the malloc address is not shared */

/* struct to hold image segment data */
typedef struct image_segment
{
    int sequence_num;    /* sequence number of the image segment */
    unsigned char data[DATA_SIZE]; /* fixed data size array of the image segment */
    size_t size;         /* size of the image segment */
} image_segment_t;

/* thread input parameters struct */
struct thread_args
{
    char url[MAX_URL_LENGTH];        /* url to fetch image segments */
    int image_num;                   /* image segment number */
    int *total_pngs_read;            /* pointer to total number of image segments read */
    image_segment_t *image_segments; /* pointer to array of image segments */
    pthread_mutex_t *mutex;          /* pointer to mutex for synchronizing access to shared data */
};

/* thread return values struct */
struct thread_ret
{
    int done_status; /* status of thread */
};

/* struct for consumer process image segment in lab 3 */
typedef struct processed_image_segment
{
    int sequence_num;              /* Sequence number of the image segment */
    unsigned char *processed_data; /* Pointer to decompressed image data */
    size_t processed_size;         /* Size of the decompressed image data */
    unsigned char *ihdr_data;      /* Pointer to IHDR data */
    size_t ihdr_length;            /* Length of IHDR data */
    struct chunk *iend_chunk;      /* Pointer to IEND chunk data */
    uint32_t height;               /* Height of the image segment */
    uint32_t width;                /* Width of the image segment */
} processed_image_segment_t;

// int process_segment(const image_segment_t *segment, processed_image_segment_t *processed_segment);
// int cat(int total_height, const U8 *consumer_buffer, size_t buffer_size, const char *png_name);

int catpng(int num_segments, image_segment_t *segments, const char *png_name);
int load_png_chunks(simple_PNG_p out, image_segment_t *segment);
void load_png_data_IHDR(struct data_IHDR *data_ihdr, U8 *segment_data, size_t sig_size, int seek_set);

int write_segment_to_png(const char *filename, image_segment_t *segment);