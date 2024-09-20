CC = gcc       # compiler
LD = gcc       # linker
CFLAGS = -Wall -g -std=c99  # compilation flags
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz   # link with libz

# Source files and object files
TARGET_SRCS = catpng.c findpng.c 
PRE_SRCS = pnginfo.c
UTIL_SRCS = crc.c zutil.c lab_png.c
PRE_USE_SRCS = crc.c zutil.c

UTIL_OBJS = $(UTIL_SRCS:.c=.o)
PRE_OBJS = $(PRE_USE_SRCS:.c=.o)

# Target executables
TARGETS= catpng findpng
PRE_TARGETS = pnginfo

all: ${TARGETS} ${PRE_TARGETS}


# Rules to build each program
$(TARGETS): %: %.o $(UTIL_OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

$(PRE_TARGETS): %: %.o $(PRE_OBJS) 
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
	rm -f $(PRE_TARGETS) *.o *.d *.out