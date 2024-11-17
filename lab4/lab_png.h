/**
 * @brief  micros and structures for a simple PNG file 
 *
 * Copyright 2018-2020 Yiqing Huang
 * Updated 2024 m27ma
 *
 * This software may be freely redistributed under the terms of MIT License
 */
#pragma once

/******************************************************************************
 * INCLUDE HEADER FILES
 *****************************************************************************/
#include <stdbool.h>

/******************************************************************************
 * DEFINED MACROS 
 *****************************************************************************/

//note: these sizes are for accessing PNG files, not for accessing data structure types (might be padded)
#define PNG_SIG_SIZE    8 /* number of bytes of png image signature data */

/******************************************************************************
 * STRUCTURES and TYPEDEFS 
 *****************************************************************************/
typedef unsigned char U8;

/******************************************************************************
 * FUNCTION PROTOTYPES 
 *****************************************************************************/
/* this is one possible way to structure the PNG manipulation functions */

bool is_png(U8 *buf, size_t n); //check if PNG signature is present