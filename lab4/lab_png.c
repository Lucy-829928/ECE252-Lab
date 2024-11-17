#include <stdbool.h>
#include <stddef.h>
#include "lab_png.h"

/* return true(png file) or false(not png file), input parameters are binary array's pointer(first 8 bytes to compared with signature) read from file and the length of read bytes */
bool is_png(U8 *buf, size_t n)
{
    if (n < PNG_SIG_SIZE)
    {
        return false;
    }
    U8 png_signature[PNG_SIG_SIZE] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}; /* first 8 bytes signature for png */

    int i;
    for (i = 0; i < 8; i++)
    {
        if (buf[i] != png_signature[i])
        {
            return false; /* mismatch, not png file */
        }
    }
    return true; /* matched, png file */
}