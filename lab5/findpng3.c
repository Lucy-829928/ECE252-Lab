#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <search.h>
#include <time.h>
#include <curl/curl.h>
#include <curl/multi.h>
#include <libxml/HTMLparser.h>
#include <libxml/uri.h> // for xmlBuildURI
#include "curl_xml/curl_xml.h"
#include "shm/shm_stack.h"

// Constants
#define MAX_URL_NUM 500             // Maximum number of URLs to visit
#define MAX_PNG_URLS 50             // Maximum number of PNG URLs to find
#define MAX_URL_LENGTH 256          // Maximum length of a URL
#define MAX_WAIT_MSECS 10 * 1000    // Wait maximum of 10 seconds

ISTACK *frontier_stack;             // Stack to manage the frontier of URLs to visit
ISTACK *png_stack;                  // Stack to track PNG URLs
int total_png = 0;                  // Total number of valid PNG URLs found
int visited = 0;                    // Total number of URLs visited
int t = 1;                          // Number of concurrent connections (default = 1)
int m = MAX_PNG_URLS;               // User-specified maximum number of URLs to visit (default = 50)
int v = 0;                          // Indicates if logging is requested
__thread char log_entry[256];       // Name of the log file

typedef struct KeyNode
{
    char *key;            // dynamically allocated key
    struct KeyNode *next; // pointed to next node
} KeyNode;
KeyNode *key_list = NULL; // linked list head

int find_http_2(char *buf, int size, int follow_relative_links, const char *base_url) {
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar *)"//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;

    if (buf == NULL || size == 0) {
        return -1;
    }

    doc = mem_getdoc(buf, size, base_url);
    if (doc == NULL) {
        return -1;
    }

    printf("Extracted URLS: \n");
    result = getnodeset(doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if (follow_relative_links) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *)base_url);
                xmlFree(old);
            }

            if (href == NULL) {
                continue;
            }

            printf("URL Found: %s\n", href);

            ENTRY entry = {.key = strdup((const char *)href)};
            if (hsearch(entry, FIND) == NULL) {
                struct UrlStackElement new_element = {.url_ptr = strdup((const char *)href)};
                push(frontier_stack, new_element); // Push to the stack
                printf("URL Added to Frontier Stack: %s\n", href);
            }
            free(entry.key);
            xmlFree(href);
        }
        xmlXPathFreeObject(result);
    }
    xmlFreeDoc(doc);
    return 0;
}

int process_html_2(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *url = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    if (url == NULL) {
        return -1;
    }

    find_http_2(p_recv_buf->buf, p_recv_buf->size, 1, url);
    return 0;
}

int process_png_2(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *eurl = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if (eurl == NULL) {
        return -1;
    }

    if (p_recv_buf->size >= 8 && memcmp(p_recv_buf->buf, "\x89PNG\r\n\x1a\n", 8) == 0) {
        if (total_png >= m) {
            return -1;
        }

        total_png++;
        FILE *png_file = fopen("png_urls.txt", "a");
        if (png_file) {
            fprintf(png_file, "%s\n", eurl);
            fclose(png_file);
        }

        struct UrlStackElement png_url = {.url_ptr = strdup(eurl)};
        push(png_stack, png_url);
        return 0;
    } else {
        return -1;
    }
}

int process_data_2(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *ct = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);

    if (ct && strstr(ct, CT_HTML)) {
        process_html_2(curl_handle, p_recv_buf);
        return 0; // Not PNG
    } else if (ct && strstr(ct, CT_PNG)) {
        process_png_2(curl_handle, p_recv_buf);
        return 1; // PNG
    }
    return 0; // Not PNG
}

void do_work() {
    curl_global_init(CURL_GLOBAL_ALL); // Initialize the CURL library
    CURLM* cm = curl_multi_init(); // Initialize the CURL multi handle
    RECV_BUF *recv_buf_array = (RECV_BUF *)malloc(t * sizeof(RECV_BUF)); // Array of receive buffers

    if (recv_buf_array == NULL) {
        fprintf(stderr, "Failed to allocate memory for recv_buf_array\n");
        return;
    }
    
    while (1) {
        printf("Frontier stack size: %d, Total PNGs: %d\n", frontier_stack->num_items, total_png);

        // EXIT CONDITION 1: no more task in frontier stack
        if (frontier_stack->num_items <= 0) {
            printf("Exiting: No more URLs to visit\n");
            break;
        }
    
        CURL *eh; // CURL easy handle
        CURLMsg *msg = NULL; // CURL message
        int still_running = 0; // Number of running handles
        int msgs_left = 0; // Number of messages left
        long http_status_code;
        const char *szUrl; // URL to visit
        
        for (int i = 0; i < t; i++) {
            if (frontier_stack->num_items > 0) {
                struct UrlStackElement popped_url;
                pop(frontier_stack, &popped_url);
                printf("Popped URL: %s\n", popped_url.url_ptr);
                eh = easy_handle_init(&recv_buf_array[i], popped_url.url_ptr, i);
                if (eh == NULL) {
                    fprintf(stderr, "Failed to initialize CURL handle for URL: %s\n", popped_url.url_ptr);
                    free(popped_url.url_ptr);
                    recv_buf_cleanup(&recv_buf_array[i]);
                    continue; // Skip this iteration
                }
                curl_multi_add_handle(cm, eh);
                free(popped_url.url_ptr);
            }
        }

        curl_multi_perform(cm, &still_running);

        do {
            int numfds = 0;
            int res = curl_multi_wait(cm, NULL, 0, MAX_WAIT_MSECS, &numfds);
            if (res != CURLM_OK) {
                fprintf(stderr, "error: curl_multi_wait() returned %d\n", res);
                break;
            }

            curl_multi_perform(cm, &still_running);
        } while(still_running);

        while ((msg = curl_multi_info_read(cm, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                eh = msg->easy_handle;
                
                http_status_code = 0;
                szUrl = NULL;

                intptr_t private_index;
                curl_easy_getinfo(eh, CURLINFO_PRIVATE, &private_index);
                int index = (int)private_index;

                curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
                curl_easy_getinfo(eh, CURLINFO_EFFECTIVE_URL, &szUrl);

                printf("Processing URL: %s, Status Code: %ld\n", szUrl, http_status_code);

                if (msg->data.result != CURLE_OK) {
                    fprintf(stderr, "CURL error code: %d\n", msg->data.result);\
                    curl_multi_remove_handle(cm, eh);
                    curl_easy_cleanup(eh);
                    continue;
                } 
                ENTRY entry = {.key = strdup(szUrl)};
                if (hsearch(entry, FIND) == NULL) {
                    hsearch(entry, ENTER);

                    KeyNode *new_node = malloc(sizeof(KeyNode));
                    new_node->key = entry.key;
                    new_node->next = key_list;
                    key_list = new_node;

                    if (v == 1) {
                        FILE *fp = fopen(log_entry, "+a");
                        if (fp) {
                            fprintf(fp, "%s\n", szUrl);
                            fclose(fp);
                        }
                    }

                    process_data_2(eh, &recv_buf_array[index]);
                } else {
                    free(entry.key);
                }
            
                curl_multi_remove_handle(cm, eh);
                curl_easy_cleanup(eh);
                if (recv_buf_array[index].buf) {
                    recv_buf_cleanup(&recv_buf_array[index]);
                }
                
                // EXIT CONDITION 2: enough PNGs found
                if (total_png >= m) {
                    printf("Exiting: Reached PNG limit (%d)\n", total_png);

                    break;
                }
            } else {
            fprintf(stderr, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
            }
        }    
    }

    for (int i = 0; i < t; i++) {
        recv_buf_cleanup(&recv_buf_array[i]); // Cleanup buffers
    }
    if (recv_buf_array) {
        free(recv_buf_array);
    }
    curl_multi_cleanup(cm);
    curl_global_cleanup();

    return;
}

int main (int argc, char *argv[]) {
    xmlInitParser(); // Initialize the XML parser library
    char seed_url[MAX_URL_LENGTH]; // Seed URL
    char log_file_name[MAX_URL_LENGTH] = {0}; // Log file name

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "t:m:v:")) != -1)
    {
        switch (opt)
        {
        case 't': // Number of concurrent connections
            t = atoi(optarg);
            if (t <= 0) {
                fprintf(stderr, "Error: Number of concurrent connections must be greater than 0.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'm': // Maximum number of URLs to visit
            m = atoi(optarg);
            if (m <= 0) {
                fprintf(stderr, "Error: Maximum number of URLs must be greater than 0.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'v': // Log file name
            strncpy(log_file_name, optarg, MAX_URL_LENGTH - 1);
            strncpy(log_entry, log_file_name, MAX_URL_LENGTH - 1);
            v = 1; // Enable logging
            break;
        default: // Default option
            fprintf(stderr, "Usage: %s [-t NUM_CONNECTIONS] [-m MAX_URLS] [-v LOG_FILE] SEED_URL\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind < argc)
        strncpy(seed_url, argv[optind], MAX_URL_LENGTH - 1);

    // Validate the seed URL
    if (strlen(seed_url) == 0)
    {
        fprintf(stderr, "Usage: %s [-t NUM_CONNECTIONS] [-m MAX_URLS] [-v LOG_FILE] SEED_URL\n", argv[0]);
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

    // Overwrite or create png_urls.txt
    FILE *png_file = fopen("png_urls.txt", "w");
    if (png_file == NULL)
    {
        perror("Failed to create png_urls.txt");
        exit(EXIT_FAILURE);
    }
    fclose(png_file);

    // Initialize hash table and stacks
    hcreate(MAX_URL_NUM);
    frontier_stack = create_stack(MAX_URL_NUM);
    png_stack = create_stack(MAX_URL_NUM);

    // Add the seed URL to the frontier stack
    struct UrlStackElement first_url = {.url_ptr = strdup(seed_url)};
    push(frontier_stack, first_url);

    // Call do_work to crawl through URLs
    do_work();

    // Write visited URLs to the output file
    png_file = fopen("png_urls.txt", "w");
    while (png_stack->num_items > 0)
    {
        struct UrlStackElement popped_url;
        pop(png_stack, &popped_url);
        fprintf(png_file, "%s\n", popped_url.url_ptr);
        free(popped_url.url_ptr);
    }
    fclose(png_file);

    // Clean up resources
    destroy_stack_elements(frontier_stack);
    destroy_stack_elements(png_stack);
    destroy_stack(frontier_stack);
    destroy_stack(png_stack);

    // Clean up hash table
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

    xmlCleanupParser();

    // Calculate and print execution time
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("findpng3 execution time: %.6f seconds\n", elapsed);

    return 0;
}