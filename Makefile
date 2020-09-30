# Tools

CC = gcc

# Project

NAME ?= v4l2-bayer-client

# Directories

BUILD = build
OUTPUT = .

# Sources

SOURCES = v4l2.c v4l2-camera.c v4l2-bayer-protocol.c $(NAME).c
OBJECTS = $(SOURCES:.c=.o)
DEPS = $(SOURCES:.c=.d)

# Compiler

CFLAGS = -I. $(shell pkg-config --cflags libudev cairo)
LDFLAGS = $(shell pkg-config --libs libudev cairo)

# Produced files

BUILD_OBJECTS = $(addprefix $(BUILD)/,$(OBJECTS))
BUILD_DEPS = $(addprefix $(BUILD)/,$(DEPS))
BUILD_BINARY = $(BUILD)/$(NAME)
BUILD_DIRS = $(sort $(dir $(BUILD_BINARY) $(BUILD_OBJECTS)))

OUTPUT_BINARY = $(OUTPUT)/$(NAME)
OUTPUT_DIRS = $(sort $(dir $(OUTPUT_BINARY)))

all: client server standalone

server:
	@make -s NAME=v4l2-bayer-server build

HONY: build

client:
	@make -s NAME=v4l2-bayer-client build

HONY: build

standalone:
	@make -s NAME=v4l2-bayer-standalone build

HONY: build

build: $(OUTPUT_BINARY)

.PHONY: build

$(BUILD_DIRS):
	@mkdir -p $@

$(BUILD_OBJECTS): $(BUILD)/%.o: %.c | $(BUILD_DIRS)
	@echo " CC     $<"
	@$(CC) $(CFLAGS) -MMD -MF $(BUILD)/$*.d -c $< -o $@

$(BUILD_BINARY): $(BUILD_OBJECTS)
	@echo " LINK   $@"
	@$(CC) $(CFLAGS) -o $@ $(BUILD_OBJECTS) $(LDFLAGS)

$(OUTPUT_DIRS):
	@mkdir -p $@

$(OUTPUT_BINARY): $(BUILD_BINARY) | $(OUTPUT_DIRS)
	@echo " BINARY $@"
	@cp $< $@

.PHONY: clean
clean:
	@echo " CLEAN"
	@rm -rf $(foreach object,$(basename $(BUILD_OBJECTS)),$(object)*) $(basename $(BUILD_BINARY))*
	@rm -rf v4l2-bayer-standalone v4l2-bayer-client v4l2-bayer-server

.PHONY: distclean
distclean: clean
	@echo " DISTCLEAN"
	@rm -rf $(BUILD)

-include $(BUILD_DEPS)
