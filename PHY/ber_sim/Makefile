CC     = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
INC    = -I../common/include

COMMON_DIR = ../common
BUILD_DIR  = build

COMMON_SRCS = utils.c config_parser.c modulation.c channel.c

BER_SRCS    = ber_sim.c

COMMON_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
BER_OBJS    = $(patsubst %.c,$(BUILD_DIR)/%.o,$(BER_SRCS))
ALL_OBJS    = $(COMMON_OBJS) $(BER_OBJS)

.PHONY: all clean

all: ber_sim

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(COMMON_DIR)/src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

ber_sim: $(ALL_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -rf $(BUILD_DIR) ber_sim
