# this will detect the OS and use the appropriate commands
ifeq ($(OS),Windows_NT)
    RM = del /Q
    EXE = .exe
	MKDIR = mkdir 2>NUL || exit 0
	SEP = \\
else
    RM = rm -f
    EXE =
	MKDIR = mkdir -p
	SEP = /
endif


CC = gcc
CFLAGS = -c -Wall
LFLAGS = -lm

BINDIR = bin
TARGET = $(BINDIR)$(SEP)VMCacheSim$(EXE)
EXAMPLE = $(BINDIR)$(SEP)VMCacheSim_v1.0$(EXE)
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

# for the test target
TRACEFILES := $(foreach f,$(FILES),-f .\trace_files\$(f))

#default
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(MKDIR) $(BINDIR)
	$(CC) $(OBJECTS) -o $(TARGET) $(LFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) *.o
	$(RM) $(TARGET)


run: $(TARGET)
	@echo.
	@echo ==============================================================================================
	@echo Running $(TARGET) with arguments: $(ARGS) $(TRACEFILES)
	@echo ==============================================================================================
	@-$(TARGET) $(ARGS) $(TRACEFILES)
	@echo.

# this will test the programs with the same arguments
# usage 'make test ARGS="arg1 arg2 arg3"'
# only use for windows
test: $(TARGET)
	@echo.
	@echo ==============================================================================================
	@echo Running $(TARGET) with arguments: $(ARGS) $(TRACEFILES)
	@echo ==============================================================================================
	@-$(TARGET) $(ARGS) $(TRACEFILES)
	@echo.
	@echo ==============================================================================================
	@echo Running example with arguments: $(ARGS) $(TRACEFILES)
	@echo ==============================================================================================
	@-$(EXAMPLE) $(ARGS) $(TRACEFILES)
	@echo.