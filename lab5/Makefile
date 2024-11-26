CC = gcc       # compiler
LD = gcc       # linker
CFLAGS = -Wall -g -std=c99 -I./curl_xml $(shell xml2-config --cflags) $(shell curl-config --cflags) -I./curl_multi $(shell pkg-config --cflags libxml-2.0 libcurl) # compilation flags
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz -lcurl -lpthread -lxml2 $(shell pkg-config --libs libxml-2.0 libcurl) # link with libz

# Source files and object files
TARGET_SRCS = findpng3.c
# PRE_SRCS = pnginfo.c
UTIL_SRCS = lab_png.c \
			curl_xml/curl_xml.c \
			shm/shm_stack.c \
			curl_multi/curl_multi_test.c
# PRE_USE_SRCS = crc.c zutil.c

UTIL_OBJS = $(UTIL_SRCS:.c=.o)
TARGET_OBJS = $(TARGET_SRCS:.c=.o)
# PRE_OBJS = $(PRE_USE_SRCS:.c=.o)


# Target executables
TARGETS = findpng3
# PRE_TARGETS = pnginfo

all: ${TARGETS}


# Rules to build each program
$(TARGETS): %: %.o $(UTIL_OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

# $(PRE_TARGETS): %: %.o $(PRE_OBJS) 
#	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

# Generic rule to compile .c files into .o files
%.o: %.c 
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to generate dependency files (.d files)
%.d: %.c
	$(CC) -MM -MF $@ $(CFLAGS) $(INCLUDE) $<

# Dependency generation (ENABLED)
-include $(TARGET_SRCS:.c=.d) $(UTIL_SRCS:.c=.d)
# Include dependency files
# -include $(addsuffix .d, $(basename $(TARGET_SRCS)) $(basename $(UTIL_SRCS)))

.PHONY: clean
clean:
	rm -f $(TARGETS) *.o *.d *.out *.html *.txt *.dat
#	rm -f $(PRE_TARGETS) *.o *.d *.out
	rm -f curl_xml/*.o curl_xml/*.d curl_xml/*.out
	rm -f shm/*.o shm/*.d shm/*.out
	rm -f curl_multi/*.o curl_multi/*.d curl_multi/*.out