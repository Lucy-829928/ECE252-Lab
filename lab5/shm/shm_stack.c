/*
 * The code is derived from 
 * Copyright(c) 2018-2019 Yiqing Huang, <yqhuang@uwaterloo.ca>.
 *
 * This software may be freely redistributed under the terms of the X11 License.
 */

/**
 * @brief  stack to push/pop URL elements.   
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shm_stack.h"

/**
 * @brief calculate the total memory that the ISTACK structure and its items require
 * @param int size maximum number of items the stack can hold
 * @return the sum of ISTACK size and the size of the data that items points to.
 */
int sizeof_shm_stack(int size)
{
    return (sizeof(ISTACK) + sizeof(UrlStackElement) * size);
}

/**
 * @brief initialize the ISTACK member fields
 * @param ISTACK *p pointer to the ISTACK structure
 * @param int stack_size max. number of items the stack can hold
 * @return 0 on success; non-zero on failure
 */
int init_shm_stack(ISTACK *p, int stack_size)
{
    if (p == NULL || stack_size <= 0) {
        return 1;
    }

    p->size = stack_size;
    p->pos = -1;
    p->items = (UrlStackElement *)((char *)p + sizeof(ISTACK));
    p->num_items = 0;
    return 0;
}

/**
 * @brief create a stack to hold `size` number of UrlStackElement items
 * @param int size maximum number of elements the stack can hold
 * @return pointer to the ISTACK structure, or NULL on failure
 */
ISTACK *create_stack(int size)
{
    if (size <= 0) {
        return NULL;
    }

    int mem_size = sizeof_shm_stack(size);
    ISTACK *pstack = (ISTACK *)malloc(mem_size);

    if (pstack == NULL) {
        perror("malloc");
        return NULL;
    }

    if (init_shm_stack(pstack, size) != 0) {
        free(pstack);
        return NULL;
    }

    return pstack;
}

/**
 * @brief release all memory used by the stack
 * @param ISTACK *p pointer to the ISTACK structure
 */
void destroy_stack(ISTACK *p)
{
    if (p != NULL) {
        free(p);
    }
}

/**
 * @brief release all dynamically allocated URL pointers in the stack
 * @param ISTACK *p pointer to the ISTACK structure
 */
void destroy_stack_elements(ISTACK *p)
{
    if (p != NULL) {
        for (int i = 0; i <= p->pos; ++i) {
            free(p->items[i].url_ptr);
        }
    }
}

/**
 * @brief check if the stack is full
 * @param ISTACK *p pointer to the ISTACK structure
 * @return non-zero if the stack is full; zero otherwise
 */
int is_full(ISTACK *p)
{
    if (p == NULL) {
        return 0;
    }
    return (p->pos == (p->size - 1));
}

/**
 * @brief check if the stack is empty
 * @param ISTACK *p pointer to the ISTACK structure
 * @return non-zero if the stack is empty; zero otherwise
 */
int is_empty(ISTACK *p)
{
    if (p == NULL) {
        return 0;
    }
    return (p->pos == -1);
}

/**
 * @brief push one UrlStackElement onto the stack
 * @param ISTACK *p pointer to the ISTACK structure
 * @param UrlStackElement item the item to be pushed onto the stack
 * @return 0 on success; non-zero otherwise
 */
int push(ISTACK *p, UrlStackElement item)
{
    if (p == NULL || is_full(p)) {
        return -1;
    }

    ++(p->pos);
    p->items[p->pos] = item;
    p->num_items++;
    // printf("push(): Pushing %s to stack %p\n", item.url_ptr, (void *)p);
    return 0;
}

/**
 * @brief pop one UrlStackElement off the stack
 * @param ISTACK *p pointer to the ISTACK structure
 * @param UrlStackElement *p_item output parameter to store the popped item
 * @return 0 on success; non-zero otherwise
 */
int pop(ISTACK *p, UrlStackElement *p_item)
{
    if (p == NULL || is_empty(p)) {
        return 1;
    }

    *p_item = p->items[p->pos];
    (p->pos)--;
    p->num_items--;
    return 0;
}
