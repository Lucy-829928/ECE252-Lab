#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/sem.h>
#include "cURL_IPC/main_2proc.h"
#include "catpng.h"
#include "shm/shm_stack.h"

#define MAX_BUFFER_SIZE 50
#define MAX_PRODUCER 20
#define MAX_CONSUMER 20

typedef struct {
    image_segment_t segments[NUM_STRIPS];
    int segment_count;
} shared_memory_t;

typedef struct int_stack
{
    int size;               /* the max capacity of the stack */
    int pos;                /* position of last item pushed onto the stack */
    int *items;             /* stack of stored integers */
} ISTACK;

int shm_id;
shared_memory_t *shared_mem;
ISTACK *stack;

sem_t sem_full;
sem_t sem_empty;
sem_t sem_mutex;

void producer(int producer_id, int N) {
    CURL *curl;
    CURLcode res;

    for (int i = producer_id; i < NUM_STRIPS; i++) {
        char url[MAX_URL_LENGTH];
        snprintf(url, sizeof(url), "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d", (i % 3) + 1, N, i);

        RECV_BUF segment;
        recv_buf_init(&segment, DATA_SIZE);

        curl = curl_easy_init;

        if (!curl) {
            fprintf(stderr, "Failed to initialize cURL\n");
            exit(EXIT_FAILURE);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb_curl);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&segment);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb_curl);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&segment);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "cURL error: %s\n", curl_easy_strerror(res));
        } else {
            sem_wait(&sem_full);
            sem_wait(&sem_mutex);

            if (shared_mem->segment_count < NUM_STRIPS) {
                shared_mem->segments[shared_mem->segment_count].sequence_num = segment.seq;
                memcpy(shared_mem->segments[shared_mem->segment_count].data, segment.buf, segment.size);
                shared_mem->segments[shared_mem->segment_count].size = segment.size;
                shared_mem->segment_count++;
            } else {
                fprintf(stderr, "Error: segment_count exceeded MAX_SEGMENTS\n");
            }

            sem_post(&sem_mutex);
            sem_post(&sem_empty);
        }

        recv_buf_cleanup(&segment);
        curl_easy_cleanup(curl);
    }
}

void consumer(int consumer_id, int X) {
    while (1) {
        sem_wait(&sem_empty);
        sem_wait(&sem_mutex);

        if (shared_mem->segment_count == 0) {
            sem_post(&sem_mutex);
            sem_post(&sem_empty);
            break;
        }

        //image_segment_t segment = shared_mem->segments[shared_mem->segment_count - 1];
        if (shared_mem->segment_count > 0) {
            shared_mem->segment_count--;
        }

        sem_post(&sem_mutex);
        sem_post(&sem_full);

        usleep(X * 1000);
    }
}

void cleanup() {
    if (shmdt(shared_mem) == -1) {
        perror("Failed to detach shared memory");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Failed to remove shared memory segment");
    }
    sem_destroy(&sem_full);
    sem_destroy(&sem_empty);
    sem_destroy(&sem_mutex);
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <B> <P> <C> <X> <N>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int B = atoi(argv[1]);
    int P = atoi(argv[2]);
    int C = atoi(argv[3]);
    int X = atoi(argv[4]);
    int N = atoi(argv[5]);

    if (B <= 0 || B > MAX_BUFFER_SIZE || P <= 0 || P > MAX_PRODUCER || C <= 0 || C > MAX_CONSUMER || N <= 0 || N > 3 || X < 0 || X > 1000) {
        fprintf(stderr, "Invalid arguments\n");
        exit(EXIT_FAILURE);
    }

    shm_id = shmget(IPC_PRIVATE, sizeof(shared_memory_t), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Failed to allocate shared memory");
        exit(EXIT_FAILURE);
    }
    
    shared_mem = shmat(shm_id, NULL, 0);
    if (shared_mem == (void *) -1) {
        perror("Failed to attach shared memory");
        exit(EXIT_FAILURE);
    }
    shared_mem->segment_count = 0;

    // Initialize shared memory stack
    stack = (ISTACK *)shared_mem;
    if (init_shm_stack(stack, B) != 0) {
        fprintf(stderr, "Failed to initialize shared memory stack\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Initialize POSIX semaphores
    if (sem_init(&sem_full, 1, B) == -1 ||
        sem_init(&sem_empty, 1, 0) == -1 ||
        sem_init(&sem_mutex, 1, 1) == -1) {
        perror("Failed to initialize semaphores");
        cleanup();
        exit(EXIT_FAILURE);
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < P; i++) {
        if (fork() == 0) {
            producer(i, N);
            exit(0);
        }
    }

    for (int i = 0; i < C; i++) {
        if (fork() == 0) {
            consumer(i, X);
            exit(0);
        }
    }

    while (wait(NULL) > 0);

    image_segment_t segments[NUM_STRIPS];
    for (int i = 0; i < NUM_STRIPS; i++) {
        segments[i] = shared_mem->segments[i];
    }

    catpng(NUM_STRIPS, segments);

    gettimeofday(&end, NULL);
    double exec_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    printf("paster2 execution time: %.2f seconds\n", exec_time);

    cleanup();

    return 0;
}