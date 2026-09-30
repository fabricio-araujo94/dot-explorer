CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -g -I./include -I./src
LDFLAGS = -lncursesw -pthread

SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/utils/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = dot-explorer

TEST_TARGETS = test_fs test_state test_utils test_task test_input

all: $(TARGET)

test: $(TEST_TARGETS)
	./test_fs
	./test_state
	./test_utils
	./test_task
	./test_input

test_fs: tests/test_fs.c src/fs.c src/state.c src/task.c src/utils.c
	$(CC) $(CFLAGS) $^ -pthread -o $@

test_state: tests/test_state.c src/state.c src/fs.c src/utils.c
	$(CC) $(CFLAGS) $^ -o $@

test_utils: tests/test_utils.c src/utils.c src/utils/theme.c
	$(CC) $(CFLAGS) $^ -o $@

test_task: tests/test_task.c src/task.c src/fs.c src/utils.c
	$(CC) $(CFLAGS) $^ -pthread -o $@

test_input: tests/test_input.c src/input.c src/fs.c src/state.c src/utils.c
	$(CC) $(CFLAGS) $^ -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)/utils

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TEST_TARGETS)

.PHONY: all test clean $(TEST_TARGETS)
