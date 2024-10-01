#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include "catpng.h"
#include "cURL/main_write_header_cb.h"

/* global data structures to share*/
image_segment_t segments[NUM_STRIPS];              /* segments array for store multiple(50) image segments */
bool segment_received[NUM_STRIPS];                 /* bool array for determining if the image segment is received */
int segments_get_num = 0;                          /* number of segments got */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; /* initialize mutex variable */

/* thread function: a routine that can run as a thread by pthreads */
void *do_work(void *arg)
{
    struct thread_args *p_in = arg;                               /* cast the argument to thread_args */
    struct thread_ret *p_out = malloc(sizeof(struct thread_ret)); /* allocate memory for the return value */
   
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    // pid_t pid = getpid();
    // int retries = 3; // Number of retries for each request
    // for (int i = 0; i < retries; i++)
    // {
    //     res = curl_easy_perform(curl_handle);
    //     if (res == CURLE_OK)
    //     {
    //         // Successful fetch
    //         break;
    //     }
    //     else
    //     {
    //         fprintf(stderr, "Attempt %d failed: %s\n", i + 1, curl_easy_strerror(res));
    //     }
    // }
    // if (res != CURLE_OK)
    // {
    //     fprintf(stderr, "curl_easy_perform() failed after %d retries: %s\n", retries, curl_easy_strerror(res));
    //     p_out->done_status = -1;
    // }

    recv_buf_init(&recv_buf, BUF_SIZE); /* initialize the receive buffer */
    if (recv_buf.buf == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for recv_buf\n");
        p_out->done_status = -1;
        return p_out;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT); /* initialize cURL globally */

    curl_handle = curl_easy_init(); /* initialize a cURL handle */

    if (curl_handle == NULL)
    {
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

    printf("thread started URL: %s\n", p_in->url); // Debugging print

    /* perform the cURL request */
    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK)
    {
        /* if the request failed, set the done_status to -1 */
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        p_out->done_status = -1;
        return p_out;
    }
    else
    {
        /* if the request succeeded, process the received data */
        int sequence_num = recv_buf.seq; /* extract the sequence number from the header */
        pthread_mutex_lock(p_in->mutex); /* lock the mutex to synchronize access to shared data */

        /* if the image segment has not been received before, store it */
        if (p_in->image_segments[sequence_num].data == NULL)
        {
            printf("Received segment %d of size %zu from URL: %s\n", sequence_num, recv_buf.size, p_in->url);

            p_in->image_segments[sequence_num].data = malloc(recv_buf.size);
            memcpy(p_in->image_segments[sequence_num].data, recv_buf.buf, recv_buf.size);
            p_in->image_segments[sequence_num].size = recv_buf.size;
            p_in->image_segments[sequence_num].sequence_num = sequence_num;
            (*p_in->total_pngs_read)++;

            printf("thread fetched segment %d from URL: %s\n", sequence_num, p_in->url); // Debugging print
        }
        else
        {
            printf("Duplicate segment %d detected.\n", sequence_num);
        }
    }

    pthread_mutex_unlock(p_in->mutex); /* unlock the mutex */
    p_out->done_status = 0;            /* set the done_status to 0 (success) */

    /* clean up cURL resources */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    free(recv_buf.buf);

    printf("thread finished URL: %s\n", p_in->url); // Debugging print

    return p_out; /* return the result */
}

int main(int argc, char **argv)
{
    /* main function implementation goes here
    need to parse command-line arguments
    need to initialize and create threads
    need to concatenate image segments and save to all.png
    use catpng for this, make sure to modify catpng to meet requirements for lab2 */

    int num_threads = DEFAULT_THREAD;                      /* default thread num */
    int image_num = DEFAULT_PNG;                           /* default image num */
    memset(segments, 0, sizeof(segments));                 /* initialize the segments array */
    memset(segment_received, 0, sizeof(segment_received)); /* initialize the segment_received array */
    segments_get_num = 0;

    /* get user data from command line */
    for (int i = 1; i < argc; i++) /* skip the call-program command */
    {
        if (strncmp(argv[i], "-t", 2) == 0 && i + 1 < argc)
        {
            num_threads = atoi(argv[i + 1]); /* convert the num after "-t" to int, it is for the thread number */
        }
        else if (strncmp(argv[i], "-n", 2) == 0 && i + 1 < argc)
        {
            image_num = atoi(argv[i + 1]); /* convert the num after "-n" to int, it is for the image number */
        }
    }

    printf("thread num: %d, image_num: %d\n", num_threads, image_num);

    /* allocate space for threads and their input arguments */
    pthread_t threads[num_threads];
    struct thread_args args[num_threads];

    /* get data for args and create threads */
    for (int i = 0; i < num_threads; i++)
    {http://ece252-%d.uwaterloo.ca:2520/image?img=%d
        snprintf(args[i].url, MAX_URL_LENGTH, "http://ece252-%d.uwaterloo.ca:2520/image?img=%d", (i % 3) + 1, image_num); /* assign server and image num to URL */
        args[i].image_num = image_num;
        args[i].total_pngs_read = &segments_get_num;
        args[i].image_segments = segments;
        args[i].mutex = &mutex;

        pthread_create(&threads[i], NULL, do_work, (void *)&args[i]);
    }

    /* wait for all threads to complete */
    for (int i = 0; i < num_threads; i++)
    {
        struct thread_ret *ret;
        pthread_join(threads[i], (void **)&ret); /* let thread wait for return value */
        free(ret);
    }

    /* concatenate segments to full png */
    if (catpng(NUM_STRIPS, segments) != 0)
    {
        fprintf(stderr, "Failed to concatenate PNG\n");
    }

    pthread_mutex_destroy(&mutex);

    return 0;
}