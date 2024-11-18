#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <search.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include "curl_xml/curl_xml.h"
#include "shm/shm_stack.h"
#include "lab_png.h"

// Constants
#define MAX_URL_NUM 500       // Maximum number of URLs to visit
#define MAX_PNG_URLS 50       // Maximum number of PNG URLs to find
#define MAX_URL_LENGTH 256    // Maximum length of a URL

// Shared data structures
pthread_mutex_t frontier_mutex;  // Mutex to protect access to the frontier stack
pthread_mutex_t visited_mutex;   // Mutex to protect access to visited hash table
pthread_mutex_t png_mutex;       // Mutex to protect access to PNG count
pthread_cond_t frontier_cond;    // Condition variable for frontier stack availability

ISTACK *frontier_stack;          // Stack to manage the frontier of URLs to visit
ISTACK *visited_stack;           // Stack to track visited URLs
int total_png = 0;               // Total number of valid PNG URLs found
int visited = 0;                 // Total number of URLs visited
int sleeping_threads = 0;        // Number of threads waiting for URLs
int t = 1;                       // Number of worker threads
int m = MAX_URL_NUM;             // User-specified maximum number of URLs to visit
int v = 0;                       // Indicates if logging is requested
char log_entry[256];             // Name of the log file

// Worker thread function
void *do_work(void *arg) {
    while (1) {
        // Check if enough PNGs have been found
        pthread_mutex_lock(&png_mutex);
        if (total_png >= MAX_PNG_URLS) {
            pthread_mutex_unlock(&png_mutex);
            return NULL;
        }
        pthread_mutex_unlock(&png_mutex);

        // Check if there are URLs in the frontier stack
        pthread_mutex_lock(&frontier_mutex);
        if (frontier_stack->num_items <= 0) {
            sleeping_threads++;
            // If all threads are sleeping, signal completion and exit
            if (sleeping_threads == t) {
                pthread_cond_broadcast(&frontier_cond);
                pthread_mutex_unlock(&frontier_mutex);
                return NULL;
            }
            pthread_cond_wait(&frontier_cond, &frontier_mutex);
            pthread_mutex_unlock(&frontier_mutex);
            sleeping_threads--;
        } else {
            // Pop a URL from the frontier stack
            struct UrlStackElement url;
            pop(frontier_stack, &url);
            pthread_mutex_unlock(&frontier_mutex);

            ENTRY entry;
            entry.key = url.url_ptr;

            // Check if the URL has already been visited
            pthread_mutex_lock(&visited_mutex);
            if (hsearch(entry, FIND) == NULL) {
                // Mark URL as visited and add to the visited stack
                hsearch(entry, ENTER);

                struct UrlStackElement visited_url = {.url_ptr = url.url_ptr};
                push(visited_stack, visited_url);

                // Write the URL to the log file if logging is enabled
                if (v == 1) {
                    FILE *fp = fopen(log_entry, "a+");
                    if (fp) {
                        fprintf(fp, "%s\n", url.url_ptr);
                        fclose(fp);
                    }
                }
                pthread_mutex_unlock(&visited_mutex);
                visited++;

                // Initialize and perform a CURL request to process the URL
                CURL *curl_handle;
                CURLcode res;
                RECV_BUF recv_buf;

                curl_global_init(CURL_GLOBAL_DEFAULT);
                curl_handle = easy_handle_init(&recv_buf, url.url_ptr);

                if (curl_handle == NULL) {
                    fprintf(stderr, "Error: Failed to initialize CURL for URL: %s\n", url.url_ptr);
                    curl_global_cleanup();
                    abort();
                }

                res = curl_easy_perform(curl_handle);

                if (res != CURLE_OK) {
                    fprintf(stderr, "Error: CURL failed for URL: %s\n", url.url_ptr);
                    cleanup(curl_handle, &recv_buf);
                } else {
                    // Process the downloaded data
                    process_data(curl_handle, &recv_buf);
                    cleanup(curl_handle, &recv_buf);
                }
            } else {
                // URL was already visited
                pthread_mutex_unlock(&visited_mutex);
            }
        }
    }

    return NULL;
}

// Main function
int main(int argc, char *argv[]) {
    xmlInitParser(); // Initialize the XML parser library
    char seed_url[MAX_URL_LENGTH];  // Seed URL
    char log_file_name[MAX_URL_LENGTH] = {0}; // Log file name

    // Parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "t:m:v:")) != -1) {
        switch (opt) {
            case 't': // Number of threads
                t = atoi(optarg);
                break;
            case 'm': // Maximum number of URLs to visit
                m = atoi(optarg);
                break;
            case 'v': // Log file name
                strncpy(log_file_name, optarg, MAX_URL_LENGTH - 1);
                if (strlen(optarg) <= 0) {
                    v = 0;
                } else {
                    v = 1;
                }
                break;
            default:
                return EXIT_FAILURE;
        }
    }
    if (optind < argc) strncpy(seed_url, argv[optind], MAX_URL_LENGTH - 1);

    // Validate the seed URL
    if (strlen(seed_url) == 0) {
        fprintf(stderr, "Usage: %s [-t NUM_THREADS] [-m MAX_URLS] [-v LOG_FILE] SEED_URL\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Create or overwrite the log file if logging is enabled
    if (v == 1) {
        FILE *log_file = fopen(log_file_name, "w");
        fclose(log_file);
    }

    // Initialize mutexes and condition variables
    pthread_mutex_init(&frontier_mutex, NULL);
    pthread_mutex_init(&visited_mutex, NULL);
    pthread_mutex_init(&png_mutex, NULL);
    pthread_cond_init(&frontier_cond, NULL);

    // Initialize hash table and stacks
    hcreate(MAX_URL_NUM);
    frontier_stack = create_stack(MAX_URL_NUM);
    visited_stack = create_stack(MAX_URL_NUM);

    // Add the seed URL to the frontier stack
    struct UrlStackElement first_url = {.url_ptr = strdup(seed_url)};
    push(frontier_stack, first_url);

    // Create worker threads
    pthread_t threads[t];
    for (int i = 0; i < t; i++) {
        pthread_create(&threads[i], NULL, do_work, NULL);
    }

    // Wait for all threads to complete
    for (int i = 0; i < t; i++) {
        pthread_join(threads[i], NULL);
    }

    // Write visited URLs to the output file
    FILE *png_file = fopen("png_urls.txt", "w");
    while (visited_stack->num_items > 0) {
        struct UrlStackElement popped_url;
        pop(visited_stack, &popped_url);
        char *url = popped_url.url_ptr;
        fprintf(png_file, "%s\n", url);
        free(url); // Free dynamically allocated memory for the URL
    }
    fclose(png_file);

    // Clean up resources
    destroy_stack_elements(frontier_stack);
    destroy_stack_elements(visited_stack);
    destroy_stack(frontier_stack);
    destroy_stack(visited_stack);
    hdestroy();

    pthread_mutex_destroy(&frontier_mutex);
    pthread_mutex_destroy(&visited_mutex);
    pthread_mutex_destroy(&png_mutex);
    pthread_cond_destroy(&frontier_cond);
    xmlCleanupParser();

    return 0;
}