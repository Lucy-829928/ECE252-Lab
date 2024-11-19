#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <search.h>
#include <time.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/uri.h> // for xmlBuildURI
#include "curl_xml/curl_xml.h"
#include "shm/shm_stack.h"
#include "lab_png.h"

// Constants
#define MAX_URL_NUM 500    // Maximum number of URLs to visit
#define MAX_PNG_URLS 50    // Maximum number of PNG URLs to find
#define MAX_URL_LENGTH 256 // Maximum length of a URL

// Shared data structures
pthread_mutex_t frontier_mutex; // Mutex to protect access to the frontier stack
pthread_mutex_t visited_mutex;  // Mutex to protect access to visited hash table
pthread_mutex_t png_mutex;      // Mutex to protect access to PNG count
pthread_cond_t frontier_cond;   // Condition variable for frontier stack availability
pthread_mutex_t exit_mutex;     // Mutex to protect the exit flag

ISTACK *frontier_stack;   // Stack to manage the frontier of URLs to visit
ISTACK *png_stack;        // Stack to track visited URLs
int total_png = 0;        // Total number of valid PNG URLs found
int visited = 0;          // Total number of URLs visited
int sleeping_threads = 0; // Number of threads waiting for URLs
int t = 1;                // Number of worker threads
int m = MAX_URL_NUM;      // User-specified maximum number of URLs to visit
int v = 0;                // Indicates if logging is requested
char log_entry[256];      // Name of the log file
int exit_flag = 0;        // Global exit flag, initialize to 0

typedef struct KeyNode
{
    char *key;            // dynamically allocated key
    struct KeyNode *next; // pointed to next node
} KeyNode;
KeyNode *key_list = NULL; // linked list head

int find_http_2(char *buf, int size, int follow_relative_links, const char *base_url)
{
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar *)"//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;

    if (buf == NULL || size == 0)
    {
        // printf("Error: Empty or NULL HTML buffer.\n");
        return -1;
    }

    doc = mem_getdoc(buf, size, base_url);
    if (doc == NULL)
    {
        // printf("Error: Unable to parse document.\n");
        return -1;
    }

    result = getnodeset(doc, xpath);
    if (result)
    {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++)
        {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if (follow_relative_links)
            {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *)base_url);
                xmlFree(old);
            }

            // skip if href is NULL
            if (href == NULL)
            {
                continue;
            }

            // Skip fragment links (e.g., #top)
            if (strchr((const char *)href, '#') != NULL)
            {
                xmlFree(href); // Free memory for ignored URL
                continue;
            }

            // Skip non-HTTP/HTTPS links
            if (strncmp((const char *)href, "http://", 7) != 0)
            {
                xmlFree(href);
                continue;
            }

            // printf(" ---- > Extracted URL: %s\n", href);

            // Push the URL to the frontier stack
            ENTRY entry = {.key = strdup((const char *)href)};
            pthread_mutex_lock(&visited_mutex);
            pthread_mutex_lock(&frontier_mutex);
            if (hsearch(entry, FIND) == NULL)
            {
                // hsearch(entry, ENTER);
                free(entry.key);
                struct UrlStackElement new_element = {.url_ptr = strdup((const char *)href)};
                push(frontier_stack, new_element);
                // printf("    ?? PUSH to frontier\n");
                pthread_cond_signal(&frontier_cond); // Signal waiting threads
                // printf("    ??? URL not exist visited: %s, add to frontier\n", href);
            }
            else
            {
                free(entry.key);
                // printf("    !!! URL already visited: %s, not add to frontier\n", href);
            }
            pthread_mutex_unlock(&frontier_mutex);
            pthread_mutex_unlock(&visited_mutex);

            xmlFree(href);
        }
        xmlXPathFreeObject(result);
    }
    xmlFreeDoc(doc);
    // xmlCleanupParser();
    return 0;
}

int process_html_2(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char *url = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    if (url == NULL)
    {
        return -1;
    }

    // printf("Processing HTML from URL: %s\n", url);

    // Extract and process links
    find_http_2(p_recv_buf->buf, p_recv_buf->size, 1, url);

    return 0;
}

int process_png_2(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char *eurl = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if (eurl == NULL)
    {
        return -1;
    }

    // Validate PNG signature (can use is_png() as well)
    if (p_recv_buf->size >= 8 && memcmp(p_recv_buf->buf, "\x89PNG\r\n\x1a\n", 8) == 0)
    {
        // printf("Valid PNG detected: %s\n", eurl);

        // Log the PNG URL to png_urls.txt
        pthread_mutex_lock(&png_mutex);

        // Exit when total png reach m
        if (total_png >= m)
        {
            pthread_mutex_lock(&exit_mutex);
            exit_flag = 1; // Set exit flag
            pthread_mutex_unlock(&exit_mutex);
            pthread_cond_broadcast(&frontier_cond); // Notify all threads
            pthread_mutex_unlock(&png_mutex);
            return -1; // Exit early if target reached
        }

        total_png++;
        // printf("  total png = %d\n", total_png);
        FILE *png_file = fopen("png_urls.txt", "a");
        if (png_file)
        {
            fprintf(png_file, "%s\n", eurl);
            fclose(png_file);
        }
        struct UrlStackElement png_url = {.url_ptr = strdup(eurl)};
        push(png_stack, png_url);
        // printf(">> PNG URL pushed to png_stack: %s\n", png_url.url_ptr);
        // printf(">>> PNG stack size: %d\n", png_stack->num_items);
        // free(png_url.url_ptr);

        // Exit when total png reach m
        if (total_png >= m)
        {
            pthread_mutex_lock(&exit_mutex);
            exit_flag = 1; // Set exit flag
            pthread_mutex_unlock(&exit_mutex);
            pthread_cond_broadcast(&frontier_cond); // Notify all threads
            pthread_mutex_unlock(&png_mutex);
            return -1; // Exit early if target reached
        }

        pthread_mutex_unlock(&png_mutex);

        // Save the PNG locally
        // char fname[256];
        // sprintf(fname, "./output_%d_%d.png", p_recv_buf->seq, getpid());
        // return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
        return 0;
    }
    else
    {
        // fprintf(stderr, "Invalid PNG detected at URL: %s\n", eurl);
        return -1;
    }
}

int process_data_2(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    // printf(" -- Processing data\n");
    char *ct = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    // printf(" -- - Content-Type: %s\n", ct);
    if (ct && strstr(ct, CT_HTML))
    {
        // printf(" -- > HTML processing\n");
        process_html_2(curl_handle, p_recv_buf);
        return 0; // Not PNG
    }
    else if (ct && strstr(ct, CT_PNG))
    {
        // printf(" -- > PNG processing\n");
        process_png_2(curl_handle, p_recv_buf);
        return 1; // PNG
    }
    // printf(" -- > Not processing\n");
    return 0; // Not PNG
}

// Worker thread function
void *do_work(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&frontier_mutex);
        pthread_mutex_lock(&png_mutex);

        // printf("Frontier stack item number = %d\n", frontier_stack->num_items);

        // Check global exit flag
        pthread_mutex_lock(&exit_mutex);
        if (exit_flag)
        {
            pthread_mutex_unlock(&exit_mutex);
            pthread_mutex_unlock(&png_mutex);
            pthread_mutex_unlock(&frontier_mutex);
            return NULL; // Exit thread
        }
        pthread_mutex_unlock(&exit_mutex);

        // Exit condition: frontier stack empty or total png reach max number
        if ((frontier_stack->num_items == 0 && sleeping_threads == t - 1) || total_png >= m)
        {
            pthread_mutex_lock(&exit_mutex);
            exit_flag = 1; // Set exit flag
            pthread_mutex_unlock(&exit_mutex);
            pthread_cond_broadcast(&frontier_cond); // wake all thread
            pthread_mutex_unlock(&png_mutex);
            pthread_mutex_unlock(&frontier_mutex);
            // printf("~~~ EXIT: All URLs processed or max PNGs found\n");
            return NULL; // exit thread
        }
        pthread_mutex_unlock(&png_mutex);
        pthread_mutex_unlock(&frontier_mutex);

        // Check if there are URLs in the frontier stack
        pthread_mutex_lock(&frontier_mutex);
        if (frontier_stack->num_items == 0)
        {
            sleeping_threads++;
            // If all threads are sleeping, signal completion and exit
            // printf("sleeping threads = %d\n", sleeping_threads);
            if (sleeping_threads == t)
            {
                pthread_mutex_lock(&exit_mutex);
                exit_flag = 1; // Set exit flag
                pthread_mutex_unlock(&exit_mutex);
                pthread_cond_broadcast(&frontier_cond);
                pthread_mutex_unlock(&frontier_mutex);
                // printf("~~~ EXIT: all threads sleeping\n");
                return NULL; // Exit when all threads are sleeping
            }

            // pthread_mutex_lock(&exit_mutex);
            // while (frontier_stack->num_items == 0 && !exit_flag)
            // {
                pthread_cond_wait(&frontier_cond, &frontier_mutex);
            // }
            // pthread_mutex_unlock(&exit_mutex);
            sleeping_threads--;
            // printf("waiting all threads waking\n");

            pthread_mutex_lock(&exit_mutex);
            if (exit_flag)
            {
                pthread_mutex_unlock(&exit_mutex);
                pthread_mutex_unlock(&frontier_mutex);
                return NULL; // Exit thread
            }
            pthread_mutex_unlock(&exit_mutex);
            // continue; // Check the condition again
        }
        pthread_mutex_unlock(&frontier_mutex);

        // Pop a URL from the frontier stack
        struct UrlStackElement url;
        pthread_mutex_lock(&frontier_mutex);
        pop(frontier_stack, &url);
        // printf(" == > POP URL from Frontier: %s\n", url.url_ptr);
        pthread_mutex_unlock(&frontier_mutex);

        // // Skip fragment links (e.g., #top)
        // if (strchr(url.url_ptr, '#') != NULL)
        // {
        //     free(url.url_ptr); // Free memory for ignored URL
        //     continue;
        // }

        // // Skip non-HTTP/HTTPS links
        // if (strncmp(url.url_ptr, "http://", 7) != 0)
        // {
        //     free(url.url_ptr);
        //     continue;
        // }

        // Initialize and perform a CURL request to process the URL
        CURL *curl_handle;
        CURLcode res;
        RECV_BUF recv_buf;
        long response_code = 0;
        char *final_url = NULL;

        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_handle = easy_handle_init(&recv_buf, url.url_ptr);

        if (curl_handle == NULL)
        {
            fprintf(stderr, "Error: Failed to initialize CURL for URL: %s\n", url.url_ptr);
            free(url.url_ptr);
            curl_global_cleanup();
            abort();
        }

        res = curl_easy_perform(curl_handle);
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &final_url);
        // printf(" > respond code %ld for link: %s\n", response_code, url.url_ptr);

        if (res != CURLE_OK)
        {
            // fprintf(stderr, "Error: CURL failed for URL: %s\n", url.url_ptr);
        }
        else if (response_code >= 200 && response_code < 300)
        {
            // Process the data and mark the final URL as visited
            pthread_mutex_lock(&visited_mutex);
            ENTRY entry = {.key = strdup(final_url)};

            if (hsearch(entry, FIND) == NULL)
            {
                hsearch(entry, ENTER);
                // Save key to linked list
                KeyNode *new_node = malloc(sizeof(KeyNode));
                new_node->key = entry.key; // key address
                new_node->next = key_list;
                key_list = new_node; // update list head
                // free(entry.key);
                pthread_mutex_unlock(&visited_mutex);

                // Process data
                process_data_2(curl_handle, &recv_buf);

                // Read the updated total_png
                pthread_mutex_lock(&png_mutex);
                if (total_png >= m)
                {
                    pthread_mutex_lock(&exit_mutex);
                    exit_flag = 1; // Set exit flag
                    pthread_mutex_unlock(&exit_mutex);
                    pthread_cond_broadcast(&frontier_cond); // Notify threads to exit
                }
                pthread_mutex_unlock(&png_mutex);

                // Write the URL to the log file if logging is enabled
                pthread_mutex_lock(&visited_mutex);
                if (v == 1)
                {
                    // pthread_mutex_lock(&png_mutex);
                    // int png_count = total_png;
                    // pthread_mutex_unlock(&png_mutex);
                    FILE *fp = fopen(log_entry, "a+");
                    if (fp)
                    {
                        // fprintf(fp, "[%ld] Thread %lu Visiting: %s\n", time(NULL), pthread_self(), url.url_ptr);
                        // fprintf(fp, "Total PNGs found: %d\n", png_count);
                        fprintf(fp, "%s\n", url.url_ptr);
                        fclose(fp);
                        // printf(" === Logged URL to log.txt: %s\n", url.url_ptr);
                    }
                    else
                    {
                        // perror(" === Failed to open log file\n");
                    }
                }
                visited++;
                pthread_mutex_unlock(&visited_mutex);
            }
            else
            {
                // printf("Exist in hash table\n");
                free(entry.key);
                pthread_mutex_unlock(&visited_mutex);
            }
        }
        else if (response_code >= 400 && response_code < 600)
        {
            // Mark the original URL as visited
            pthread_mutex_lock(&visited_mutex);
            ENTRY entry = {.key = strdup(url.url_ptr)};
            if (hsearch(entry, FIND) == NULL)
            {
                hsearch(entry, ENTER);
                // Save key to linked list
                KeyNode *new_node = malloc(sizeof(KeyNode));
                new_node->key = entry.key; // key address
                new_node->next = key_list;
                key_list = new_node; // update list head
                // free(entry.key);
                // Write the URL to the log file if logging is enabled
                if (v == 1)
                {
                    // pthread_mutex_lock(&png_mutex);
                    // int png_count = total_png;
                    // pthread_mutex_unlock(&png_mutex);
                    FILE *fp = fopen(log_entry, "a+");
                    if (fp)
                    {
                        // fprintf(fp, "Visiting: %s\n", url.url_ptr);
                        // fprintf(fp, "Total PNGs found: %d\n", png_count);
                        fprintf(fp, "%s\n", url.url_ptr);
                        fclose(fp);
                        // printf("Logged URL to log.txt: %s\n", url.url_ptr);
                    }
                    else
                    {
                        // perror("Failed to open log file\n");
                    }
                }
                visited++;
            }
            else
            {
                // printf("Exist in hash table\n");
                free(entry.key);
            }
            pthread_mutex_unlock(&visited_mutex);
            // printf("    HTTP error for URL %s: %ld\n", url.url_ptr, response_code);
        }
        free(url.url_ptr);
        cleanup(curl_handle, &recv_buf);
    }

    return NULL;
}

// Main function
int main(int argc, char *argv[])
{
    xmlInitParser();                          // Initialize the XML parser library
    char seed_url[MAX_URL_LENGTH];            // Seed URL
    char log_file_name[MAX_URL_LENGTH] = {0}; // Log file name

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "t:m:v:")) != -1)
    {
        switch (opt)
        {
        case 't': // Number of threads
            t = atoi(optarg);
            break;
        case 'm': // Maximum number of URLs to visit
            m = atoi(optarg);
            break;
        case 'v': // Log file name
            strncpy(log_file_name, optarg, MAX_URL_LENGTH - 1);
            strncpy(log_entry, log_file_name, MAX_URL_LENGTH - 1);
            if (strlen(optarg) <= 0)
            {
                v = 0;
            }
            else
            {
                v = 1;
            }
            break;
        default:
            return EXIT_FAILURE;
        }
    }
    if (optind < argc)
        strncpy(seed_url, argv[optind], MAX_URL_LENGTH - 1);

    // Validate the seed URL
    if (strlen(seed_url) == 0)
    {
        fprintf(stderr, "Usage: %s [-t NUM_THREADS] [-m MAX_URLS] [-v LOG_FILE] SEED_URL\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Create or overwrite the log file if logging is enabled
    if (v == 1)
    {
        FILE *log_file = fopen(log_file_name, "w");
        if (log_file == NULL)
        {
            // perror("Failed to create log file");
            exit(EXIT_FAILURE);
        }
        fclose(log_file);
    }

    // Initialize mutexes and condition variables
    pthread_mutex_init(&frontier_mutex, NULL);
    pthread_mutex_init(&visited_mutex, NULL);
    pthread_mutex_init(&png_mutex, NULL);
    pthread_mutex_init(&exit_mutex, NULL);
    pthread_cond_init(&frontier_cond, NULL);

    // Initialize hash table and stacks
    hcreate(MAX_URL_NUM);
    frontier_stack = create_stack(MAX_URL_NUM);
    png_stack = create_stack(MAX_URL_NUM);

    // Add the seed URL to the frontier stack
    struct UrlStackElement first_url = {.url_ptr = strdup(seed_url)};
    push(frontier_stack, first_url);

    // Create worker threads
    pthread_t threads[t];
    for (int i = 0; i < t; i++)
    {
        pthread_create(&threads[i], NULL, do_work, NULL);
    }

    // Wait for all threads to complete
    for (int i = 0; i < t; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // printf(">> png url in stack: %d\n", png_stack->num_items);
    // Write visited URLs to the output file
    FILE *png_file = fopen("png_urls.txt", "w");
    while (png_stack->num_items > 0)
    {
        struct UrlStackElement popped_url;
        pop(png_stack, &popped_url);
        char *url = popped_url.url_ptr;
        fprintf(png_file, "%s\n", url);
        free(url); // Free dynamically allocated memory for the URL
    }
    fclose(png_file);

    // Clean up resources
    destroy_stack_elements(frontier_stack);
    destroy_stack_elements(png_stack);
    destroy_stack(frontier_stack);
    destroy_stack(png_stack);

    // Clean up hash table keys
    KeyNode *current = key_list;
    while (current != NULL)
    {
        KeyNode *next = current->next;
        free(current->key); // Release key
        free(current);      // Release node
        current = next;
    }
    key_list = NULL;
    hdestroy();

    // // Clean up frontier_stack
    // while (frontier_stack->num_items > 0)
    // {
    //     struct UrlStackElement element;
    //     pop(frontier_stack, &element);
    //     free(element.url_ptr); // Free dynamically allocated URL
    // }

    // // Clean up png_stack
    // while (png_stack->num_items > 0)
    // {
    //     struct UrlStackElement element;
    //     pop(png_stack, &element);
    //     free(element.url_ptr); // Free dynamically allocated URL
    // }

    pthread_mutex_destroy(&frontier_mutex);
    pthread_mutex_destroy(&visited_mutex);
    pthread_mutex_destroy(&png_mutex);
    pthread_mutex_destroy(&exit_mutex);
    pthread_cond_destroy(&frontier_cond);
    xmlCleanupParser();

    // Calculate and print execution time
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("findpng2 execution time: %.6f seconds\n", elapsed);

    return 0;
}