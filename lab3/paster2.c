#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <sys/time.h>
#include <semaphore.h>      /* add for using process */
#include "catpng.h"
#include "cURL_IPC/main_2proc.h"

// #define MAX_THREAD 20
#define MAX_BUFFER_SIZE 50    /* setting the max buffer size, 1 <= B <= 50 */
#define MAX_PRODUCER 20    /* setting the max producer number, 1 <= P <= 20 */
#define MAX_CONSUMER 20    /* setting the max consumer number, 1 <= C <= 20 */

sem_t empty;        /* record the empty buffer number */
sem_t full;         /* record the full buffer number */

int buffer_size;         /* size of the buffer (from input) */
int producer_num;        /* number of producer threads */
int consumer_num;        /* number of consumer threads */
int consumer_sleep_time; /* consumer sleep time (ms) */
int image_num;           /* image number to download */

/* global data structures to share*/
image_segment_t segments[NUM_STRIPS];                 /* segments array for store multiple(50) image segments */
bool segment_received[NUM_STRIPS];                    /* bool array for determining if the image segment is received */
int segments_get_num = 0;                             /* number of segments got */
pthread_mutex_t num_lock = PTHREAD_MUTEX_INITIALIZER; /* mutex for locking global variables */

/* Producer (thread) function: a routine that can run as a thread by pthreads */
void * producer(void *arg)
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
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl);
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

        /* wait */
        sem_wait(&empty);

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
            continue; /* skip to the next iteration without processing */
        }

        /* if the image segment has not been received before, store it */
        // printf("Received segment %d of size %zu from URL: %s\n", sequence_num, recv_buf.size, p_in->url);

        p_in->image_segments[sequence_num].data = malloc(recv_buf.size);
        if (!p_in->image_segments[sequence_num].data)
        {
            printf("Failed to allocate memory for segment %d\n", sequence_num);
            pthread_mutex_unlock(&num_lock); /* unlock the mutex before returning */
            /* free buf so that it won't hold same data */
            if (recv_buf.buf != NULL)
            {
                free(recv_buf.buf);
                recv_buf.buf = NULL;
            }
            recv_buf_init(&recv_buf, BUF_SIZE);
            continue; /* move on to the next iteration */
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

        /* post */
        sem_post(&full);

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

void * consumer(void * arg)
{
    while (1)
    {
        /* wait */
        sem_wait(&full);
        pthread_mutex_lock(&num_lock);

        /* check if get all segments */
        if (segments_get_num >= NUM_STRIPS)
        {
            pthread_mutex_unlock(&num_lock);
            break;
        }

        usleep(consumer_sleep_time * 1000);

        pthread_mutex_unlock(&num_lock);

        /* post */
        sem_post(&empty);  
    }
    return NULL;
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
    if (argc != 6)
    {
        fprintf(stderr, "Usage: %s <buffer_size> <producer_num> <consumer_num> <consumer_sleep_time> <image_num>\n", argv[0]);
        return -1;
    }

    buffer_size = atoi(argv[1]);    /* the second argument string (B) is for buffer size */
    producer_num = atoi(argv[2]);   /* the third argument string (P) is for number of producer */
    consumer_num = atoi(argv[3]);   /* the fourth argument string (C) is for number of consumer */
    consumer_sleep_time = atoi(argv[4]); /* the fifth argument string (X) is for time(ms) for consumer to sleep before it starts to process image data */
    image_num = atoi(argv[5]);           /* the sixth argument string (N) is for image number we want to get from server */

    sem_init(&empty, 0, buffer_size); 
    sem_init(&full, 0, 0);            

    struct timeval start, end;
    /* start timer */
    gettimeofday(&start, NULL);

    // int num_threads = DEFAULT_THREAD;                      /* default thread num */
    // int image_num = DEFAULT_PNG;                           /* default image num */
    // memset(segments, 0, sizeof(segments));                 /* initialize the segments array */
    // memset(segment_received, 0, sizeof(segment_received)); /* initialize the segment_received array */
    // segments_get_num = 0;

    pthread_t producers[producer_num], consumers[consumer_num];
    struct thread_args args[producer_num];

    for (int i = 0; i < producer_num; i++)
    {
        snprintf(args[i].url, MAX_URL_LENGTH, "http://ece252-%d.uwaterloo.ca:2520/image?img=%d", (i % 3) + 1, image_num);
        args[i].image_segments = segments;
        pthread_create(&producers[i], NULL, producer, &args[i]);
    }

    for (int i = 0; i < consumer_num; i++)
    {
        pthread_create(&consumers[i], NULL, consumer, NULL);
    }

    for (int i = 0; i < producer_num; i++)
    {
        struct thread_ret *ret;
        pthread_join(producers[i], (void **)&ret);
        free(ret);
    }

    for (int i = 0; i < consumer_num; i++)
    {
        pthread_join(consumers[i], NULL);
    }

    if (segments_get_num == NUM_STRIPS)
    {
        if (catpng(NUM_STRIPS, segments) != 0)
        {
            printf("Error: Failed to concatenate PNG\n");
        }
    }
    else
    {
        printf("Error: Missing segment data.\n");
    }

    for (int i = 0; i < NUM_STRIPS; i++)
    {
        if (segments[i].data != NULL)
        {
            free(segments[i].data);
            segments[i].data = NULL;
        }
    }
    pthread_mutex_destroy(&num_lock);
    sem_destroy(&empty);
    sem_destroy(&full);

    /* end timer */
    gettimeofday(&end, NULL);
    double time_spent = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    printf("Spent wall-clock time: %.6f seconds\n", time_spent);

    return 0;
}
