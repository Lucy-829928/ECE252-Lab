#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>       // For intptr_t
#include <search.h>       // For hash table functions
#include <curl/curl.h>    // For HTTP requests
#include <libxml/HTMLparser.h> // For parsing HTML
#include <libxml/xpath.h> // For working with XML/HTML paths
#include "curl_xml/curl_xml.h"
#include "shm/shm_stack.h"
#include "lab_png.h"

// Constants
#define MAX_URL_LENGTH 256
#define MAX_PNG_URLS 50
#define MAX_URL_NUM 500

// Shared data structures
pthread_mutex_t frontier_mutex;
pthread_mutex_t visited_mutex;
pthread_mutex_t png_mutex;
pthread_cond_t frontier_cond;

ISTACK *url_stack;              // Frontier stack
ENTRY *visited_table = NULL;    // Visited URLs hash table
char *png_urls[MAX_PNG_URLS];   // Found PNG URLs
int png_count = 0;              // Number of PNG URLs found
int visited_count = 0;          // Number of URLs visited
int active_threads = 0;         // Number of active threads
int sleeping_threads = 0;       // Number of threads waiting for URLs
FILE *log_file = NULL;          // Log file for visited URLs

// Function prototypes
void process_url(const char *url);
void *crawl_thread(void *arg);
void add_visited(const char *url);
int is_visited(const char *url);

// Add a URL to the stack
void push_url(const char *url) {
    pthread_mutex_lock(&frontier_mutex);
    if (push(url_stack, (intptr_t)strdup(url)) == 0) { // Convert pointer to integer
        pthread_cond_signal(&frontier_cond);           // Notify waiting threads
    }
    pthread_mutex_unlock(&frontier_mutex);
}

// Get the next URL from the stack
char *pop_url() {
    pthread_mutex_lock(&frontier_mutex);
    while (is_empty(url_stack) && sleeping_threads < active_threads) {
        sleeping_threads++;
        pthread_cond_wait(&frontier_cond, &frontier_mutex);
        sleeping_threads--;
    }
    if (is_empty(url_stack)) { // No URLs left
        pthread_mutex_unlock(&frontier_mutex);
        return NULL;
    }

    int url_as_int; // Store as an int for compatibility
    pop(url_stack, (int *)&url_as_int); // Cast to int * to match function signature
    pthread_mutex_unlock(&frontier_mutex);
    return (char *)(intptr_t)url_as_int; // Convert back to pointer
}

// Crawl thread function
void *crawl_thread(void *arg) {
    char *url;
    while ((url = pop_url()) != NULL) {
        pthread_mutex_lock(&visited_mutex);
        if (visited_count >= MAX_URL_NUM) { // Check if max URLs limit reached
            pthread_mutex_unlock(&visited_mutex);
            free(url);
            break;
        }
        visited_count++;
        pthread_mutex_unlock(&visited_mutex);

        if (!is_visited(url)) {
            add_visited(url);
            process_url(url);
        }
        free(url);

        pthread_mutex_lock(&png_mutex);
        if (png_count >= MAX_PNG_URLS) { // Check if max PNGs limit reached
            pthread_mutex_unlock(&png_mutex);
            break;
        }
        pthread_mutex_unlock(&png_mutex);
    }
    pthread_exit(NULL);
}

// Process a URL: download and parse
void process_url(const char *url) {
    RECV_BUF recv_buf;
    CURL *curl_handle = easy_handle_init(&recv_buf, url);
    if (!curl_handle) return;

    if (curl_easy_perform(curl_handle) == CURLE_OK) {
        if (is_png((U8 *)recv_buf.buf, recv_buf.size)) {
            pthread_mutex_lock(&png_mutex);
            if (png_count < MAX_PNG_URLS) {
                png_urls[png_count++] = strdup(url);
                printf("PNG found: %s\n", url);
            }
            pthread_mutex_unlock(&png_mutex);
        } else {
            find_http(recv_buf.buf, recv_buf.size, 1, url);
        }
    }
    cleanup(curl_handle, &recv_buf);
}

// Check if URL is visited
int is_visited(const char *url) {
    pthread_mutex_lock(&visited_mutex);
    ENTRY entry = {.key = (char *)url, .data = NULL};
    ENTRY *result = hsearch(entry, FIND);
    pthread_mutex_unlock(&visited_mutex);
    return result != NULL;
}

// Mark a URL as visited
void add_visited(const char *url) {
    pthread_mutex_lock(&visited_mutex);
    ENTRY entry = {.key = strdup(url), .data = NULL};
    hsearch(entry, ENTER);
    if (log_file) {
        fprintf(log_file, "%s\n", url);
    }
    pthread_mutex_unlock(&visited_mutex);
}

// Main function
int main(int argc, char *argv[]) {
    xmlInitParser();

    int num_threads = 1; // Default threads
    int max_pngs = 50;   // Default PNGs
    char seed_url[MAX_URL_LENGTH] = {0};
    char log_file_name[MAX_URL_LENGTH] = {0};

    pthread_mutex_init(&frontier_mutex, NULL);
    pthread_mutex_init(&visited_mutex, NULL);
    pthread_mutex_init(&png_mutex, NULL);
    pthread_cond_init(&frontier_cond, NULL);

    // Parse command-line arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-t NUM_THREADS] [-m MAX_PNGS] [-v LOG_FILE] SEED_URL\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-t=", 3) == 0) {
            num_threads = atoi(argv[i] + 3);
        } else if (strncmp(argv[i], "-m=", 3) == 0) {
            max_pngs = atoi(argv[i] + 3);
        } else if (strncmp(argv[i], "-v=", 3) == 0) {
            strcpy(log_file_name, argv[i] + 3);
        } else {
            strncpy(seed_url, argv[i], MAX_URL_LENGTH);
        }
    }

    if (strlen(seed_url) == 0) {
        fprintf(stderr, "Usage: %s [OPTIONS] SEED_URL\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Open log file if provided
    if (log_file_name[0]) {
        log_file = fopen(log_file_name, "w");
        if (!log_file) {
            perror("Failed to open log file");
            return EXIT_FAILURE;
        }
    }

    // Initialize structures
    hcreate(MAX_URL_NUM);
    url_stack = create_stack(MAX_URL_NUM);
    push_url(seed_url);

    // Start threads
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, crawl_thread, NULL);
    }

    // Wait for threads to finish
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Write PNG URLs to file
    FILE *png_file = fopen("png_urls.txt", "w");
    for (int i = 0; i < png_count; i++) {
        fprintf(png_file, "%s\n", png_urls[i]);
        free(png_urls[i]);
    }
    fclose(png_file);

    // Cleanup
    if (log_file) fclose(log_file);
    hdestroy();
    destroy_stack(url_stack);

    pthread_mutex_destroy(&frontier_mutex);
    pthread_mutex_destroy(&visited_mutex);
    pthread_mutex_destroy(&png_mutex);
    pthread_cond_destroy(&frontier_cond);

    xmlCleanupParser();

    return 0;
}