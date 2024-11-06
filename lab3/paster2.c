#define _DEFAULT_SOURCE   /* open GNU C library */
#define _XOPEN_SOURCE 500 /* include usleep */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h> /* add for usleep */
#include <curl/curl.h>
#include <sys/time.h>
#include <semaphore.h>   /* add for using process */
#include <sys/shm.h>     /* for shared memory */
#include <sys/wait.h>    /* for waitpid */
#include <fcntl.h>       /* for O_CREAT */
#include <sys/utsname.h> /* for the name of host server */
#include "catpng.h"
#include "cURL_IPC/main_2proc.h"

// #define MAX_THREAD 20
#define MIN_BUFFER_SEG_NUM 1  /* setting the max buffer size, 1 <= B <= 50 */
#define MIN_PRODUCER 1        /* setting the max producer number, 1 <= P <= 20 */
#define MIN_CONSUMER 1        /* setting the max consumer number, 1 <= C <= 20 */
#define MAX_BUFFER_SEG_NUM 50 /* setting the max buffer size, 1 <= B <= 50 */
#define MAX_PRODUCER 20       /* setting the max producer number, 1 <= P <= 20 */
#define MAX_CONSUMER 20       /* setting the max consumer number, 1 <= C <= 20 */
#define RUN_TIME 1

/* POSIX semaphore */
char sem_name_empty[50];
char sem_name_full[50];
char sem_name_mutex[50];
sem_t *empty; /* semaphore for empty buffer slots */
sem_t *full;  /* semaphore for full buffer slots */
sem_t *mutex; /* semaphore for critical section access */
// sem_t *produced;
// sem_t *consumed;
// pthread_mutex_t num_lock = PTHREAD_MUTEX_INITIALIZER; /* mutex for locking global variables */

int buffer_seg_num;      /* size of the buffer (from input) */
int producer_num;        /* number of producer threads */
int consumer_num;        /* number of consumer threads */
int consumer_sleep_time; /* consumer sleep time (ms) */
int image_num;           /* image number to download */

int shm_id_segments_buffer; /* shared memory ID for image segments buffer */
int shm_id_segments;        /* shared memory ID for image segments */
// int shm_id_segment_received; /* shared memory ID for segment received array */
// int shm_id_segments_get_num; /* shared memory ID for number of segments got in shared memory */
int shm_id_produced_count; /* shared memory ID for number of produced segment */
int shm_id_consumed_count; /* shared memory ID for number of consumed segment */
int shm_id_producer_index; /* Shared memory ID for producer index */
int shm_id_consumer_index; /* Shared memory ID for consumer index */
int shm_id_producer_exit_flag;      /* shared memory ID for producer exit flag */
int shm_id_consumer_exit_flag;      /* shared memory ID for consumer exit flag */

/* global data structures in shared memory */
image_segment_t *segments_buffer; /* point to the segments buffer array for store buffer_seg_num of image segments in shared memory */
image_segment_t *segments;        /* point to the segments array for store 50 image segments in shared memory */
// processed_image_segment_t *processed_segment;

int *produced_count;          /* point to number of produced segment */
int *consumed_count;          /* point to number of consumed segment */
int *producer_index;          /* point to index of producer segment buffer */
int *consumer_index;          /* point to index of consumer segment buffer */
int *producer_exit_flag;      /* point to producer exit flag */
int *consumer_exit_flag;      /* point to consumer exit flag */

void print_sem()
{
    int empty_val, full_val, mutex_val;
    sem_getvalue(empty, &empty_val);
    sem_getvalue(full, &full_val);
    sem_getvalue(mutex, &mutex_val);
    printf("here print: empty = %d, full = %d, mutex = %d\n", empty_val, full_val, mutex_val);
}

/* Producer function: a routine that can run as a process */
void producer(char *url_template)
{
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    while (1)
    {
        if (*producer_exit_flag)
        {
            printf("Producer exit: ");
            break;
        }

        printf("Producer: start ");
        print_sem();

        sem_wait(empty);
        sem_wait(mutex);

        printf("Producer: start after wait ");
        print_sem();

        // if (*exit_flag)
        // {
        //     sem_post(mutex);
        //     sem_post(empty);
        //     printf("Producer exit: ");
        //     print_sem();
        //     break;
        // }

        // int seg_num_producer = *produced_count;
        /* check if get all segments */
        if (*produced_count >= NUM_STRIPS)
        {
            *producer_exit_flag = 1;
            sem_post(mutex);
            sem_post(full); /* allow consumers to exit if necessary */
            break;
        }
        // sem_post(mutex); /* exit critical section */

        /* make producer wait if the count for producer and consumer is larger than buffer's seg num */
        int distance = (*produced_count - *consumed_count);
        if (distance >= buffer_seg_num)
        {
            sem_post(mutex);
            sem_post(full);
            printf("!! Producer: consumer haven't get to the index\n");
            continue;
        }

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

        char url[256];
        snprintf(url, sizeof(url), url_template, *produced_count);
        // printf(" --- Fetching URL: %s\n", url);

        /* set cURL options */
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK)
        {
            printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            continue;
        }

        memcpy(segments_buffer[*producer_index].data, recv_buf.buf, recv_buf.size);
        segments_buffer[*producer_index].size = recv_buf.size;
        segments_buffer[*producer_index].sequence_num = recv_buf.seq;

        printf(">> Producer store segment %d into index %d\n", recv_buf.seq, *producer_index);

        printf("??? producer_index = %d, buffer_seg_num = %d\n", *producer_index, buffer_seg_num);
        if (*producer_index == buffer_seg_num - 1)
        {
            printf("producer_index == buffer_seg_num\n");
            if (catpng(buffer_seg_num, segments_buffer, "producer.png") != 0)
            {
                printf("Error: Failed to concatenate PNG\n");
            }
            else
            {
                printf("Concatenated to producer.png\n");
            }
        }

        *producer_index = (*producer_index + 1) % buffer_seg_num;

        /* print out the buffer's segments' signature */
        for (int i = 0; i < buffer_seg_num; i++)
        {
            if (segments_buffer[i].data)
            {
                printf("Producer read segment %d with signature %02x %02x %02x %02x\n",
                       i,
                       segments_buffer[i].data[0], segments_buffer[i].data[1],
                       segments_buffer[i].data[2], segments_buffer[i].data[3]);
            }
            else
            {
                printf("Producer read segment %d with NULL data\n", i);
            }
        }

        (*produced_count)++;
        sem_post(mutex);
        sem_post(full); /* inform consumer that a new segment is ready */

        printf("Producer: end after post ");
        print_sem();
    }

    /* clean up cURL resources */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    if (recv_buf.buf != NULL)
    {
        free(recv_buf.buf);
        recv_buf.buf = NULL;
    }

    /* close process */
}

void consumer()
{
    while (1)
    {
        printf("Consumer: start ");
        print_sem();

        if (*consumer_exit_flag)
        {
            printf("Consumer exit: ");
            print_sem();
            break;
        }

        /* wait */
        sem_wait(full);
        sem_wait(mutex);

        printf("Consumer: start after wait ");
        print_sem();
        // if (*exit_flag)
        // {
        //     sem_post(mutex);
        //     printf("Consumer exit: ");
        //     print_sem();
        //     break;
        // }

        /* check if get all segments */
        if (*consumed_count >= NUM_STRIPS)
        {
            *consumer_exit_flag = 1;
            sem_post(mutex);
            sem_post(empty); // Allow producers to exit if necessary
            break;
        }
        // sem_post(mutex);

        printf("here7\n");

        // sem_wait(mutex);
        int seg_num_consumer = segments_buffer[*consumer_index].sequence_num;
        memcpy(segments[seg_num_consumer].data, segments_buffer[*consumer_index].data, segments_buffer[*consumer_index].size);
        segments[seg_num_consumer].size = segments_buffer[*consumer_index].size;
        segments[seg_num_consumer].sequence_num = segments_buffer[*consumer_index].sequence_num;

        printf(">> Consumer read segment %d from buffer index %d\n", seg_num_consumer, *consumer_index);
        *consumer_index = (*consumer_index + 1) % buffer_seg_num;

        for (int i = 0; i <= seg_num_consumer; i++)
        {
            if (segments[i].data)
            {
                printf("Consumer read segment %d with signature %02x %02x %02x %02x\n",
                       i,
                       segments[i].data[0], segments[i].data[1],
                       segments[i].data[2], segments[i].data[3]);
            }
            else
            {
                printf("Consumer read segment %d with NULL data\n", i);
            }
        }

        (*consumed_count)++;
        /* post */
        sem_post(mutex);
        sem_post(empty);

        printf("Consumer: end after post ");
        print_sem();
        /* delay */
        usleep(consumer_sleep_time * 1000);
    }
}



/* Set up shared memory for communication between processes */
void setup_shared_memory()
{
    /* create space */
    shm_id_segments_buffer = shmget(IPC_PRIVATE, buffer_seg_num * sizeof(image_segment_t), IPC_CREAT | 0666);
    shm_id_segments = shmget(IPC_PRIVATE, NUM_STRIPS * sizeof(image_segment_t), IPC_CREAT | 0666);
    // shm_id_segment_received = shmget(IPC_PRIVATE, NUM_STRIPS * sizeof(bool), IPC_CREAT | 0666);
    shm_id_produced_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_consumed_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_producer_index = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_consumer_index = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_producer_exit_flag = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_consumer_exit_flag = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    /* link space with name */
    segments_buffer = (image_segment_t *)shmat(shm_id_segments_buffer, NULL, 0);
    segments = (image_segment_t *)shmat(shm_id_segments, NULL, 0);
    // segment_received = (bool *)shmat(shm_id_segment_received, NULL, 0);
    produced_count = (int *)shmat(shm_id_produced_count, NULL, 0);
    consumed_count = (int *)shmat(shm_id_consumed_count, NULL, 0);
    producer_index = (int *)shmat(shm_id_producer_index, NULL, 0);
    consumer_index = (int *)shmat(shm_id_consumer_index, NULL, 0);
    producer_exit_flag = (int *)shmat(shm_id_producer_exit_flag, NULL, 0);
    consumer_exit_flag = (int *)shmat(shm_id_consumer_exit_flag, NULL, 0);

    for (int i = 0; i < buffer_seg_num; i++)
    {
        segments_buffer[i].size = 0;
        segments_buffer[i].sequence_num = -1;
    }

    for (int i = 0; i < NUM_STRIPS; i++)
    {
        segments[i].size = 0;
        segments[i].sequence_num = -1;
    }

    *produced_count = 0;
    *consumed_count = 0;
    *producer_index = 0;
    *consumer_index = 0;
    *producer_exit_flag = 0;
    *consumer_exit_flag = 0;

    /* unlink */
    sem_unlink(sem_name_empty);
    sem_unlink(sem_name_full);
    sem_unlink(sem_name_mutex);
    // if (sem_unlink(sem_name_mutex) == -1)
    // {
    //     perror("sem_unlink failed");
    // }

    /* initialize semaphores for shared memory */
    empty = sem_open(sem_name_empty, O_CREAT | O_EXCL, 0644, buffer_seg_num);
    if (empty == SEM_FAILED)
    {
        perror("Failed to open semaphore empty");
        exit(1);
    }

    full = sem_open(sem_name_full, O_CREAT | O_EXCL, 0644, 0);
    if (full == SEM_FAILED)
    {
        perror("Failed to open semaphore full");
        exit(1);
    }

    mutex = sem_open(sem_name_mutex, O_CREAT | O_EXCL, 0644, 1);
    if (mutex == SEM_FAILED)
    {
        perror("Failed to open semaphore mutex");
        exit(1);
    }
    printf("here2.5\n");
    print_sem();
    printf("Semaphore setup complete.\n");
}

/* Clean up shared memory and semaphores */
void cleanup()
{
    printf("Cleaning up shared memory and semaphores...\n");
    if (shm_id_segments_buffer != -1)
    {
        shmctl(shm_id_segments_buffer, IPC_RMID, NULL);
    }
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
    if (shm_id_producer_index != -1)
    {
        shmctl(shm_id_producer_index, IPC_RMID, NULL);
    }
    if (shm_id_consumer_index != -1)
    {
        shmctl(shm_id_consumer_index, IPC_RMID, NULL);
    }
    if (shm_id_producer_exit_flag != -1)
    {
        shmctl(shm_id_producer_exit_flag, IPC_RMID, NULL);
    }
    if (shm_id_consumer_exit_flag != -1)
    {
        shmctl(shm_id_consumer_exit_flag, IPC_RMID, NULL);
    }

    sem_close(empty);
    sem_close(full);
    sem_close(mutex);
    sem_unlink(sem_name_empty);
    sem_unlink(sem_name_full);
    sem_unlink(sem_name_mutex);
}

void signal_handler(int signum)
{
    printf("Caught signal %d\n", signum);
    cleanup();
    exit(signum);
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
        if (catpng(NUM_STRIPS, segments, "all.png") != 0)
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
    snprintf(sem_name_empty, sizeof(sem_name_empty), "/sem_empty_%s_%d", getenv("USER"), getpid());
    snprintf(sem_name_full, sizeof(sem_name_full), "/sem_full_%s_%d", getenv("USER"), getpid());
    snprintf(sem_name_mutex, sizeof(sem_name_mutex), "/sem_mutex_%s_%d", getenv("USER"), getpid());
    // atexit(cleanup);
    signal(SIGINT, signal_handler); /* call the clean up when ctrl+c*/

    if (argc != 6)
    {
        fprintf(stderr, "Usage: %s <buffer_size> <producer_num> <consumer_num> <consumer_sleep_time> <image_num>\n", argv[0]);
        return -1;
    }

    buffer_seg_num = atoi(argv[1]);      /* the second argument string (B) is for buffer size = BUF_SIZE * buffer_seg_num */
    producer_num = atoi(argv[2]);        /* the third argument string (P) is for number of producer */
    consumer_num = atoi(argv[3]);        /* the fourth argument string (C) is for number of consumer */
    consumer_sleep_time = atoi(argv[4]); /* the fifth argument string (X) is for time(ms) for consumer to sleep before it starts to process image data */
    image_num = atoi(argv[5]);           /* the sixth argument string (N) is for image number we want to get from server */
    // printf("command: %d,%d,%d,%d\n", buffer_seg_num, producer_num, consumer_num, consumer_sleep_time);
    // sem_init(&empty, 0, buffer_size);
    // sem_init(&full, 0, 0);

    printf("here1\n");
    /* store the host info */
    struct utsname uts;
    uname(&uts);

    double total_time = 0.0;
    printf("here2\n");
    for (int i = 0; i < RUN_TIME; i++)
    {
        double time_spent = execute_experiment(buffer_seg_num, producer_num, consumer_num, consumer_sleep_time, image_num);
        total_time += time_spent;
        printf("Run %d: %.6f seconds\n", i + 1, time_spent);
    }
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
    double avg_time = total_time / RUN_TIME;
    fprintf(fp, "%d,%d,%d,%d,%.6f\n", buffer_seg_num, producer_num, consumer_num, consumer_sleep_time, avg_time);
    printf("paster2 execution time: %.6f seconds\n", avg_time);

    fclose(fp);

    return 0;
}