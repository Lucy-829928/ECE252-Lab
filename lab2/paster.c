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
image_segment_t segments[NUM_STRIPS];                 /* segments array for store multiple(50) image segments */
bool segment_received[NUM_STRIPS];                    /* bool array for determining if the image segment is received */
int segments_get_num = 0;                             /* number of segments got */
pthread_mutex_t num_lock = PTHREAD_MUTEX_INITIALIZER; /* mutex for locking global variables */

/* thread function: a routine that can run as a thread by pthreads */
void *do_work(void *arg)
{
    struct thread_args *p_in = arg;                               /* cast the argument to thread_args */
    struct thread_ret *p_out = malloc(sizeof(struct thread_ret)); /* allocate memory for the return value */

    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    int sequence_num = 0;

    memset(&recv_buf, 0, sizeof(RECV_BUF));
    recv_buf_init(&recv_buf, BUF_SIZE); /* initialize the receive buffer */
    if (recv_buf.buf == NULL)
    {
        printf("Failed to allocate memory for recv_buf\n");
        p_out->done_status = -1;
        return p_out;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT); /* initialize cURL globally */
    curl_handle = curl_easy_init();        /* initialize a cURL handle */

    if (curl_handle == NULL)
    {
        printf("curl_easy_init: returned NULL\n");
        free(recv_buf.buf);
        return p_out;
    }

    /* set cURL options */
    curl_easy_setopt(curl_handle, CURLOPT_URL, p_in->url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    while (1)
    {
        pthread_mutex_lock(&num_lock); /* Lock mutex to check shared state */
        if (segments_get_num == NUM_STRIPS)
        {
            pthread_mutex_unlock(&num_lock);
            p_out->done_status = 0; /* set the done_status to 0 (success) */
            break;
        }
        pthread_mutex_unlock(&num_lock);

        // printf("Thread started URL: %s\n", p_in->url);

        /* perform the cURL request */
        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK)
        {
            /* if the request failed, set the done_status to -1 */
            printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            p_out->done_status = -1;
            return p_out;
        }

        pthread_mutex_lock(&num_lock); /* lock the mutex to synchronize access to shared data */
        sequence_num = recv_buf.seq;   /* extract the sequence number from the header */
        // printf("Thread %ld is processing sequence number %d\n", pthread_self(), sequence_num);

        /* check if the segment is received before */
        if (segment_received[sequence_num])
        {
            // printf("Duplicate segment %d detected. Skipping...\n", sequence_num);
            pthread_mutex_unlock(&num_lock); /* unlock and skip further processing */
            /* free buf so that it won't hold same data */
            if (recv_buf.buf != NULL)
            {
                free(recv_buf.buf);
                recv_buf.buf = NULL;
            }
            recv_buf_init(&recv_buf, BUF_SIZE);
            continue;                        /* skip to the next iteration without processing */
        }

        /* if the image segment has not been received before, store it */
        // printf("Received segment %d of size %zu from URL: %s\n", sequence_num, recv_buf.size, p_in->url);

        p_in->image_segments[sequence_num].data = malloc(recv_buf.size);
        if (!p_in->image_segments[sequence_num].data)
        {
            printf("Failed to allocate memory for segment %d\n", sequence_num);
            pthread_mutex_unlock(&num_lock); /* unlock the mutex before returning */
            continue;                        /* move on to the next iteration */
        }

        memcpy(p_in->image_segments[sequence_num].data, recv_buf.buf, recv_buf.size);
        p_in->image_segments[sequence_num].size = recv_buf.size;
        p_in->image_segments[sequence_num].sequence_num = sequence_num;
        segment_received[sequence_num] = true;

        // /* Construct the PNG filename for this segment */
        // char filename[64];
        // snprintf(filename, sizeof(filename), "%d_inside_thread_segment_%d.png",segments_get_num, sequence_num);

        // /* Write the segment to a PNG file */
        // int write_status = write_segment_to_png(filename, &p_in->image_segments[sequence_num]);
        // if (write_status == 0)
        // {
        //     printf("Segment %d successfully written to %s\n", sequence_num, filename);
        // }

        segments_get_num++;
        // printf("Thread fetched segment %d from URL: %s; seg_get_num = %d\n", sequence_num, p_in->url, segments_get_num);

        if (segments_get_num == NUM_STRIPS)
        {
            pthread_mutex_unlock(&num_lock); /* unlock mutex after check */
            p_out->done_status = 0;          /* set the done_status to 0 (success) */
            break;                           /* exit loop */
        }

        pthread_mutex_unlock(&num_lock); /* unlock mutex after done */

        if (recv_buf.buf != NULL)
        {
            free(recv_buf.buf);
            recv_buf.buf = NULL;
        }
        recv_buf_init(&recv_buf, BUF_SIZE);
        // printf("Thread finished URL: %s\n", p_in->url);
    }

    /* clean up cURL resources */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    if (recv_buf.buf != NULL)
    {
        free(recv_buf.buf);
        recv_buf.buf = NULL;
    }

    free(p_out);
    p_out = NULL;

    return p_out; /* return the result */
}

void print_segment_received()
{
    printf("Segments received: \n");
    for (int i = 0; i < NUM_STRIPS; i++)
    {
        printf("Segment %d: %s\n", i, segment_received[i] ? "Received" : "Missing");
    }
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

    if(!(1 <= num_threads && num_threads <= 6) || !(1 <= image_num && image_num <= 3))
    {
        printf("Invalid input for '-n' and '-t'\n");
        return -1;
    }

    // printf("thread num: %d, image_num: %d\n", num_threads, image_num);

    /* allocate space for threads and their input arguments */
    pthread_t threads[num_threads];
    struct thread_args args[num_threads];
    memset(args, 0, sizeof(args));

    /* get data for args and create threads */
    // while (segments_get_num < NUM_STRIPS)
    // {
    for (int i = 0; i < num_threads; i++)
    {
        // pthread_mutex_lock(&mutex); /* lock the mutex for thread */
        // if (segments_get_num >= NUM_STRIPS)
        // {
        //     pthread_mutex_unlock(&mutex);
        //     break; /* exit if all segments fetched */
        // }
        // pthread_mutex_unlock(&mutex);

        snprintf(args[i].url, MAX_URL_LENGTH, "http://ece252-%d.uwaterloo.ca:2520/image?img=%d", (i % 3) + 1, image_num); /* assign server and image num to URL */
        args[i].image_num = image_num;
        args[i].total_pngs_read = &segments_get_num;
        args[i].image_segments = segments;
        args[i].mutex = &num_lock;

        pthread_create(&threads[i], NULL, do_work, (void *)&args[i]);
        pthread_t pthread_self(void);
        // printf("i = %d\n", i);
    }

    // printf("here\n");

    /* wait for all threads to complete */
    for (int i = 0; i < num_threads; i++)
    {
        // printf("here1\n");
        struct thread_ret *ret;
        pthread_join(threads[i], (void **)&ret); /* let thread wait for return value */
        free(ret);
    }
    // printf("!!!! num of png get: %d\n", segments_get_num);

    // for (int i = 0; i < 50; i++)
    // {
    //     char filename[50];
    //     sprintf(filename, "segment_%d.png", i);
    //     printf("segment number of the png is %d ", segments[i].sequence_num);
    //     if (write_segment_to_png(filename, &segments[i]) != 0)
    //     {
    //         printf("Failed to write segment %d to PNG\n", i);
    //     }
    // }

    /* check if all segments are received */
    if (segments_get_num != NUM_STRIPS)
    {
        printf("Error: Missing segment data. Only %d/%d segments received.\n", segments_get_num, NUM_STRIPS);
        return -1;
    }
    // printf("here2.5\n");

    // printf("Order of segments stored:\n");
    // for (int i = 0; i < segments_get_num; i++)
    // {
    //     printf("Segment %d stored at position %d\n", segments[i].sequence_num, i);
    // }

    // void print_segment_received();

    /* concatenate segments to full png */
    if (segments_get_num == NUM_STRIPS)
    {
        if (catpng(NUM_STRIPS, segments) != 0)
        {
            printf("Error: Failed to concatenate PNG\n");
        }
    }
    else
    {
        printf("Error: Missing segment data. Only %d/%d segments received.\n", segments_get_num, NUM_STRIPS);
    }

    // printf("here3\n");
    for (int i = 0; i < NUM_STRIPS; i++)
    {
        if (segments[i].data != NULL)
        {
            free(segments[i].data); 
            segments[i].data = NULL; 
        }
    }
    pthread_mutex_destroy(&num_lock);

    return 0;
}