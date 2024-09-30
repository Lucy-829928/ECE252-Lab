#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include "catpng.c"
#include "cURL/main_write_header_cb.c"

#define NUM_STRIPS 50
#define MAX_URL_LENGTH 256

/* struct to hold image segment data */
typedef struct {
    int sequence_num; /* sequence number of the image segment */
    unsigned char *data; /* pointer to data of the image segment */
    size_t size; /* size of the image segment */
} image_segment_t;

/* thread input parameters struct */
struct thread_args {
    char url[MAX_URL_LENGTH]; /* url to fetch image segments */
    int image_num; /* image segment number */
    int *total_pngs_read; /* pointer to total number of image segments read */
    image_segment_t *image_segments; /* pointer to array of image segments */
    pthread_mutex_t *mutex; /* pointer to mutex for synchronizing access to shared data */
};

/* thread return values struct */
struct thread_ret {
    int done_status; /* status of thread */
};

/* thread function: a routine that can run as a thread by pthreads */
void *do_work (void *arg) {
    struct thread_args *p_in = arg; /* cast the argument to thread_args */
    struct thread_ret *p_out = malloc(sizeof(struct thread_ret)); /* allocate memory for the return value */

    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    pid_t pid = getpid();

    recv_buf_init(&recv_buf, BUF_SIZE); /* initialize the receive buffer */

    curl_global_init(CURL_GLOBAL_DEFAULT); /* initialize cURL globally */

    curl_handle = curl_easy_init(); /* initialize a cURL handle */

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* set cURL options */
    curl_easy_setopt(curl_handle, CURLOPT_URL, p_in->url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* perform the cURL request */
    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        /* if the request failed, set the done_status to -1 */
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        p_out->done_status = -1;
    } else {
        /* if the request succeeded, process the received data */
        int sequence_num = recv_buf.seq; /* extract the sequence number from the header */
        pthread_mutex_lock(p_in->mutex); /* lock the mutex to synchronize access to shared data */

        /* if the image segment has not been received before, store it */
        if (p_in->image_segments[sequence_num].data == NULL) {
            p_in->image_segments[sequence_num].data = malloc(recv_buf.size);
            memcpy(p_in->image_segments[sequence_num].data, recv_buf.buf, recv_buf.size);
            p_in->image_segments[sequence_num].size = recv_buf.size;
            p_in->image_segments[sequence_num].sequence_num = sequence_num;
            (*p_in->total_pngs_read)++;
        }

        pthread_mutex_unlock(p_in->mutex); /* unlock the mutex */
        p_out->done_status = 0; /* set the done_status to 0 (success) */
    }

    /* clean up cURL resources */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    free(recv_buf.buf);

    return p_out; /* return the result */
}

int main (int argc, char **argv) {
    /* main function implementation goes here
    need to parse command-line arguments
    need to initialize and create threads
    need to concatenate image segments and save to all.png
    use catpng for this, make sure to modify catpng to meet requirements for lab2 */

    return 0;
}

int main(int argc, char **argv) {
    int num_threads = 1;
    int image_num = 1;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-t=", 3) == 0) {
            num_threads = atoi(argv[i] + 3);
        } else if (strncmp(argv[i], "-n=", 3) == 0) {
            image_num = atoi(argv[i] + 3);
        }
    }

    pthread_t threads[num_threads];
    struct thread_args thread_data[num_threads];
    image_segment_t image_segments[NUM_STRIPS] = {0};
    pthread_mutex_t segment_mutex = PTHREAD_MUTEX_INITIALIZER;
    int total_pngs_read = 0;

    // Initialize and create threads
    for (int i = 0; i < num_threads; i++) {
        snprintf(thread_data[i].url, MAX_URL_LENGTH, "http://ece252-%d.uwaterloo.ca:2520/image?img=%d", i % 3 + 1, image_num);
        thread_data[i].image_num = image_num;
        thread_data[i].image_segments = image_segments;
        thread_data[i].mutex = &segment_mutex;
        thread_data[i].total_pngs_read = &total_pngs_read;
        pthread_create(&threads[i], NULL, do_work, (void *)&thread_data[i]);
    }

    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++) {
        struct thread_ret *ret_val;
        pthread_join(threads[i], (void **)&ret_val);
        if (ret_val->done_status != 0) {
            fprintf(stderr, "Thread %d encountered an error\n", i);
        }
        free(ret_val);
    }

    // Concatenate image segments and save the final image
    FILE *output_file = fopen("all.png", "wb");
    if (output_file == NULL) {
        fprintf(stderr, "Failed to open output file\n");
        return 1;
    }

    // Use catpng to concatenate the image segments
    if (catpng(image_segments, NUM_STRIPS, output_file) != 0) {
        fprintf(stderr, "Failed to concatenate image segments\n");
        fclose(output_file);
        return 1;
    }

    fclose(output_file);

    return 0;
}