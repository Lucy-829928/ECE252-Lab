/* The code is 
 * Copyright(c) 2018-2019 Yiqing Huang, <yqhuang@uwaterloo.ca>.
 *
 * This software may be freely redistributed under the terms of the X11 License.
 */
/**
 * @brief  stack to push/pop URL elements, API header  
 * @author yqhuang@uwaterloo.ca
 */

typedef struct UrlStackElement {
    char url[256];        /* Static storage for URLs */
    char *url_ptr;        /* Dynamically allocated URL string */
    size_t size;          /* Size of the URL data */
} UrlStackElement;

typedef struct int_stack
{
    int size;               /* The max capacity of the stack */
    int pos;                /* Position of the last item pushed onto the stack */
    int num_items;          /* Number of items in the stack */
    UrlStackElement *items; /* Stack of stored UrlStackElements */
} ISTACK;

int sizeof_shm_stack(int size);
int init_shm_stack(struct int_stack *p, int stack_size);
struct int_stack *create_stack(int size);
void destroy_stack(struct int_stack *p);
void destroy_stack_elements(struct int_stack *p); /* Free all dynamic elements */
int is_full(struct int_stack *p);
int is_empty(struct int_stack *p);
int push(struct int_stack *p, UrlStackElement item);
int pop(struct int_stack *p, UrlStackElement *p_item);