CC     = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
INC    = -I../common/include

COMMON_DIR = ../common
BUILD_DIR  = build

COMMON_SRCS = utils.c config_parser.c mcs_table.c crc.c \
              polar_rate_match.c modulation.c fft.c ofdm.c \
              channel.c channel_estimation.c dmrs.c \
              ldpc.c polar.c csi_rs.c srs.c \
              tdl_channel.c harq.c tbs.c mimo.c

LLS_SRCS    = main.c pdsch.c pdcch.c pbch.c

COMMON_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
LLS_OBJS    = $(patsubst %.c,$(BUILD_DIR)/%.o,$(LLS_SRCS))
ALL_OBJS    = $(COMMON_OBJS) $(LLS_OBJS)

.PHONY: all clean

all: lls_sim

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(COMMON_DIR)/src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

lls_sim: $(ALL_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -rf $(BUILD_DIR) lls_sim
