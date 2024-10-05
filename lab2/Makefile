CC = gcc       # compiler
LD = gcc       # linker
CFLAGS = -Wall -g -std=c99 # compilation flags
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz -lcurl -lpthread # link with libz

# Source files and object files
TARGET_SRCS = paster.c
UTIL_SRCS = catpng.c crc.c zutil.c lab_png.c \
			cURL/main_write_header_cb.c

TARGET_OBJS = $(TARGET_SRCS:.c=.o)
UTIL_OBJS = $(UTIL_SRCS:.c=.o)

# Target executables
TARGETS= paster

all: ${TARGETS}


# Rules to build each program
$(TARGETS): %: %.o $(UTIL_OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

# Generic rule to compile .c files into .o files
%.o: %.c 
	$(CC) $(CFLAGS) -c $< -o $@

# Dependency generation (ENABLED)
-include $(TARGET_SRCS:.c=.d) $(UTIL_SRCS:.c=.d)

# Rule to generate dependency files (.d files)
%.d: %.c
	$(CC) -MM -MF $@ $(CFLAGS) $(INCLUDE) $<

# Include dependency files
-include $(addsuffix .d, $(basename $(TARGET_SRCS)) $(basename $(UTIL_SRCS)))

.PHONY: clean
clean:
	rm -f $(TARGETS) *.o *.d *.out 
	rm -f $(UTIL_OBJS) $(UTIL_SRCS:.c=.d) $(TARGET_SRCS:.c=.d)
