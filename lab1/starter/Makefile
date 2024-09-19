CC = gcc       # compiler
LD = gcc       # linker
CFLAGS = -Wall -g -std=c99 -Ipng_util   # compilation flags
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz   # link with libz

# Include Directories
# INCLUDE = -I. -Ipng_util

# Source files and object files
#SRCS_PNGINFO = pnginfo.c png_util/crc.c png_util/zutil.c
#SRCS_CATPNG = catpng.c png_util/crc.c png_util/zutil.c
#SRCS_FINDPNG = findpng.c png_util/crc.c png_util/zutil.c
TARGET_SRCS = pnginfo.c catpng.c findpng.c
UTIL_SRCS = png_util/crc.c png_util/zutil.c

#OBJS_PNGINFO = $(SRCS_PNGINFO:.c=.o)
#OBJS_CATPNG = $(SRCS_CATPNG:.c=.o)
#OBJS_FINDPNG = $(SRCS_FINDPNG:.c=.o)
UTIL_OBJS = $(UTIL_SRCS:.c=.o)

# Target executables
TARGETS= pnginfo catpng findpng

all: ${TARGETS}

# Rules to build each program
$(TARGETS): %: %.o $(UTIL_OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

# Generic rule to compile .c files into .o files
%.o: %.c 
	$(CC) $(CFLAGS) -c $< -o $@

# Dependency generation
%.d: %.c
	$(CC) -MM -MF $@ $(CFLAGS) $(INCLUDE) $<

# Include dependency files
-include $(TARGET_SRCS:.c=.d) $(UTIL_SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f $(TARGETS)*.o png_util/*.o *.d 