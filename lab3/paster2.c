#define _DEFAULT_SOURCE   /* open GNU C library */
#define _XOPEN_SOURCE 500 /* include usleep */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h> /* add for usleep */
#include <curl/curl.h>
#include <pthread.h>
#include <sys/time.h>
#include <semaphore.h>   /* add for using process */
#include <sys/shm.h>     /* for shared memory */
#include <sys/wait.h>    /* for waitpid */
#include <fcntl.h>       /* for O_CREAT */
#include <sys/utsname.h> /* for the name of host server */
#include "catpng.h"
#include "cURL_IPC/main_2proc.h"

// #define MAX_THREAD 20
#define MAX_BUFFER_SIZE 50 /* setting the max buffer size, 1 <= B <= 50 */
#define MAX_PRODUCER 20    /* setting the max producer number, 1 <= P <= 20 */
#define MAX_CONSUMER 20    /* setting the max consumer number, 1 <= C <= 20 */
#define RUN_TIME 1

/* POSIX semaphore */
sem_t *empty; /* semaphore for empty buffer slots */
sem_t *full;  /* semaphore for full buffer slots */
sem_t *mutex; /* semaphore for critical section access */
// pthread_mutex_t num_lock = PTHREAD_MUTEX_INITIALIZER; /* mutex for locking global variables */

int buffer_size;         /* size of the buffer (from input) */
int producer_num;        /* number of producer threads */
int consumer_num;        /* number of consumer threads */
int consumer_sleep_time; /* consumer sleep time (ms) */
int image_num;           /* image number to download */

int shm_id_segments; /* shared memory ID for image segments */
// int shm_id_segment_received; /* shared memory ID for segment received array */
// int shm_id_segments_get_num; /* shared memory ID for number of segments got in shared memory */
int shm_id_produced_count; /* shared memory ID for number of produced segment */
int shm_id_consumed_count; /* shared memory ID for number of consumed segment */
int shm_id_exit_flag;      /* shared memory ID for consumer exit flag */

/* global data structures in shared memory */
image_segment_t *segments; /* point to the segments array for store multiple(50) image segments in shared memory */
// bool *segment_received;    /* point to bool array for determining if the image segment is received in shared memory */
// int *segments_get_num; /* point to number of segments got in shared memory */
int *produced_count;   /* point to number of produced segment */
int *consumed_count;   /* point to number of consumed segment */
int *exit_flag;        /* point to consumer exit flag */

void print_sem()
{
    int empty_val, full_val, mutex_val;
    sem_getvalue(empty, &empty_val);
    sem_getvalue(full, &full_val);
    sem_getvalue(mutex, &mutex_val);
    printf("here: empty = %d, full = %d, mutex = %d\n", empty_val, full_val, mutex_val);
}

/* Producer (thread) function: a routine that can run as a thread by pthreads */
void producer(char *url_template)
{
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    memset(&recv_buf, 0, sizeof(RECV_BUF));
    recv_buf_init(&recv_buf, BUF_SIZE); /* initialize the receive buffer */
    if (recv_buf.buf == NULL)
    {
        printf("Failed to allocate memory for recv_buf\n");
        exit(-1);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT); /* initialize cURL globally */
    curl_handle = curl_easy_init();        /* initialize a cURL handle */

    if (curl_handle == NULL)
    {
        printf("curl_easy_init: returned NULL\n");
        free(recv_buf.buf);
        exit(-1);
    }

    for (int seq_num = 0; seq_num < NUM_STRIPS; seq_num++)
    {
        if (*exit_flag)
        {
            printf("Producer exit: ");
            print_sem();
            break;
        }

        printf("Producer: ");
        print_sem();

        sem_wait(empty);
        sem_wait(mutex);
        printf("here6\n");

        if (*exit_flag)
        {
            sem_post(mutex);
            sem_post(empty);
            printf("Producer exit: ");
            print_sem();
            break;
        }

        char url[256];
        snprintf(url, sizeof(url), url_template, seq_num);
        // printf("Fetching URL: %s\n", url);

        /* set cURL options */
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK)
        {
            printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            continue;
        }

        // if (seq_num == 0)
        // {
        //     printf("Producer: Storing Segment 0 at address %p, size: %zu\n", segments[0].data, recv_buf.size);
        // }

        /* store the segment directly */
        // segments[seq_num].data = malloc(recv_buf.size);
        memcpy(segments[seq_num].data, recv_buf.buf, recv_buf.size);
        segments[seq_num].size = recv_buf.size;
        segments[seq_num].sequence_num = seq_num;

        // printf("Producer stored segment %d with signature %02x %02x %02x %02x\n",
        //        seq_num,
        //        segments[seq_num].data[0], segments[seq_num].data[1],
        //        segments[seq_num].data[2], segments[seq_num].data[3]);
        // printf("Segment %d: Stored in shared memory, size %zu\n", seq_num, segments[seq_num].size);

        // sem_wait(mutex);
        (*produced_count)++;

        sem_post(mutex); /* exit critical section */
        sem_post(full);  /* inform consumer that a new segment is ready */
    }

    /* clean up cURL resources */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    if (recv_buf.buf != NULL)
    {
        free(recv_buf.buf);
        recv_buf.buf = NULL;
    }
}

void consumer()
{
    while (1)
    {
        printf("Consumer: ");
        print_sem();

        if (*exit_flag)
        {
            printf("Consumer exit: ");
            print_sem();
            break;
        }

        /* wait */
        sem_wait(full);
        sem_wait(mutex);

        if (*exit_flag)
        {
            sem_post(mutex);
            printf("Consumer exit: ");
            print_sem();
            break;
        }

        printf("here7\n");

        // for (int i = 0; i < NUM_STRIPS; i++)
        // {
        //     if (segments[i].data)
        //     {
        //         printf("Consumer read segment %d with signature %02x %02x %02x %02x\n",
        //                i,
        //                segments[i].data[0], segments[i].data[1],
        //                segments[i].data[2], segments[i].data[3]);
        //     }
        //     else
        //     {
        //         printf("Consumer read segment %d with NULL data\n", i);
        //     }
        // }

        /* check if get all segments */
        if (*consumed_count == NUM_STRIPS)
        {
            *exit_flag = 1;
            for (int i = 0; i < consumer_num; i++)
            {
                sem_post(full);
            }
            sem_post(mutex);
            printf("Consumer exit after check: ");
            print_sem();
            break;
        }
        if (*consumed_count < *produced_count)  // *consumed_count < *produced_count (= NUM_STRIPS)
        {
            printf("Consumer read segment %d\n", *consumed_count);
            (*consumed_count)++;
        }
        
        // bool all_received = true;
        // for (int i = 0; i < NUM_STRIPS; i++)
        // {
        //     if (segments[i].size == 0)
        //     {
        //         all_received = false;
        //         printf("Consumer: segment not full yet\n");
        //         // sem_post(mutex);
        //         // sem_post(full);
        //         break;
        //     }
        // }
        // if (all_received)
        // {
        //     printf("Consumer: segment already full\n");
        //     sem_post(mutex);
        //     sem_post(full);
        //     break;
        // }

        // printf("Consumer: Accessing segment 0 at address %p, size: %zu\n", segments[0].data, segments[0].size);

        /* post */
        sem_post(mutex);
        sem_post(empty);

        /* delay */
        usleep(consumer_sleep_time * 1000);
    }
}

/* Set up shared memory for communication between processes */
void setup_shared_memory()
{
    shm_id_segments = shmget(IPC_PRIVATE, NUM_STRIPS * sizeof(image_segment_t), IPC_CREAT | 0666);
    // shm_id_segment_received = shmget(IPC_PRIVATE, NUM_STRIPS * sizeof(bool), IPC_CREAT | 0666);
    shm_id_produced_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_consumed_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_exit_flag = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    segments = (image_segment_t *)shmat(shm_id_segments, NULL, 0);
    // segment_received = (bool *)shmat(shm_id_segment_received, NULL, 0);
    produced_count = (int *)shmat(shm_id_produced_count, NULL, 0);
    consumed_count = (int *)shmat(shm_id_consumed_count, NULL, 0);
    exit_flag = (int *)shmat(shm_id_exit_flag, NULL, 0);

    *produced_count = 0;
    *consumed_count = 0;
    *exit_flag = 0;

    sem_unlink("empty");
    sem_unlink("full");
    sem_unlink("mutex");

    /* initialize semaphores for shared memory */
    empty = sem_open("empty", O_CREAT, 0644, buffer_size);
    full = sem_open("full", O_CREAT, 0644, 0);
    mutex = sem_open("mutex", O_CREAT, 0644, 1);

    print_sem();
    printf("Semaphore setup complete.\n");
}

/* Clean up shared memory and semaphores */
void cleanup()
{
    printf("Cleaning up shared memory and semaphores...\n");
    if (shm_id_segments != -1)
    {
        shmctl(shm_id_segments, IPC_RMID, NULL);
    }
    // if (shm_id_segment_received != -1)
    // {
    //     shmctl(shm_id_segment_received, IPC_RMID, NULL);
    // }
    if (shm_id_produced_count != -1)
    {
        shmctl(shm_id_produced_count, IPC_RMID, NULL);
    }
    if (shm_id_consumed_count != -1)
    {
        shmctl(shm_id_consumed_count, IPC_RMID, NULL);
    }
    if (shm_id_exit_flag != -1) 
    {
        shmctl(shm_id_exit_flag, IPC_RMID, NULL);
    }

    sem_close(empty);
    sem_close(full);
    sem_close(mutex);
    sem_unlink("/empty");
    sem_unlink("/full");
    sem_unlink("/mutex");
}

bool all_segments_received()
{
    for (int i = 0; i < NUM_STRIPS; i++)
    {
        if (segments[i].size == 0)
        {
            printf("Segment %d data is missing\n", i);
            return false;
        }
    }
    return true;
}

double execute_experiment(int B, int P, int C, int X, int N)
{
    setup_shared_memory();
    printf("here3\n");
    struct timeval start, end;
    /* start timer */
    gettimeofday(&start, NULL);

    /* Create producer processes */
    for (int i = 0; i < P; i++)
    {
        if (fork() == 0)
        {
            char url_template[256];
            snprintf(url_template, sizeof(url_template), "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%%d", (i % 3) + 1, image_num);
            producer(url_template);
            exit(0);
        }
    }

    /* Create consumer processes */
    for (int i = 0; i < C; i++)
    {
        if (fork() == 0)
        {
            consumer();
            exit(0);
        }
    }
    printf("here4\n");
    /* Wait for all child processes to finish */
    for (int i = 0; i < P + C; i++)
    {
        wait(NULL);
    }

    /* end timer */
    gettimeofday(&end, NULL);
    double time_spent = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

    for (int i = 0; i < NUM_STRIPS; i++)
    {
        if (segments[i].data == NULL)
        {
            printf("Segment %d data is NULL\n", i);
        }
        else
        {
            printf("Segment %d loaded with size %zu\n", i, segments[i].size);
        }
    }

    if (all_segments_received())
    {
        if (catpng(NUM_STRIPS, segments) != 0)
        {
            printf("Error: Failed to concatenate PNG\n");
        }
        else
        {
            printf("Concatenated to all.png\n");
        }
    }
    else
    {
        printf("Error: Missing segment data.\n");
    }

    /* clean */
    cleanup();
    return time_spent;
}

// void print_segment_received()
// {
//     printf("Segments received: \n");
//     for (int i = 0; i < NUM_STRIPS; i++)
//     {
//         printf("Segment %d: %s\n", i, segment_received[i] ? "Received" : "Missing");
//     }
// }

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: %s <buffer_size> <producer_num> <consumer_num> <consumer_sleep_time> <image_num>\n", argv[0]);
        return -1;
    }

    buffer_size = atoi(argv[1]);         /* the second argument string (B) is for buffer size */
    producer_num = atoi(argv[2]);        /* the third argument string (P) is for number of producer */
    consumer_num = atoi(argv[3]);        /* the fourth argument string (C) is for number of consumer */
    consumer_sleep_time = atoi(argv[4]); /* the fifth argument string (X) is for time(ms) for consumer to sleep before it starts to process image data */
    image_num = atoi(argv[5]);           /* the sixth argument string (N) is for image number we want to get from server */

    // sem_init(&empty, 0, buffer_size);
    // sem_init(&full, 0, 0);

    printf("here1\n");
    /* store the host info */
    struct utsname uts;
    uname(&uts);

    /* create .csv */
    char csv_filename[256];
    snprintf(csv_filename, sizeof(csv_filename), "lab3_%s.csv", uts.nodename);

    FILE *fp = fopen(csv_filename, "w");
    if (fp == NULL)
    {
        perror("Error opening file");
        return -1;
    }

    fprintf(fp, "B,P,C,X,Avg Time\n");

    double total_time = 0.0;
    printf("here2\n");
    for (int i = 0; i < RUN_TIME; i++)
    {
        double time_spent = execute_experiment(buffer_size, producer_num, consumer_num, consumer_sleep_time, image_num);
        total_time += time_spent;
        printf("Run %d: %.6f seconds\n", i + 1, time_spent);
    }

    double avg_time = total_time / RUN_TIME;
    fprintf(fp, "%d,%d,%d,%d,%.6f\n", buffer_size, producer_num, consumer_num, consumer_sleep_time, avg_time);
    printf("paster2 execution time: %.6f seconds\n", avg_time);

    fclose(fp);

    return 0;
}
