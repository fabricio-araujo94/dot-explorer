CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -g -I./src
LDFLAGS = -lncursesw -pthread

SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/utils/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

ifdef OS
    EXE = .exe
    MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
    RM = if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
    RM_FILES = $(foreach f,$(1),if exist "$(f)" del /q "$(f)" &) ver > NUL
    TEST_PREFIX =
    # The native ncurses build cannot resolve its terminfo database outside a
    # MSYS2 shell, so point it at the toolchain's own copy when available.
    GCC_PREFIX := $(firstword $(subst /bin/../lib/gcc/, ,$(shell gcc -print-prog-name=cc1.exe)))
    ifneq ($(wildcard $(GCC_PREFIX)/share/terminfo),)
        export TERMINFO := $(subst \,/,$(GCC_PREFIX))/share/terminfo
    endif
else
    EXE =
    MKDIR = mkdir -p $1
    RM = rm -rf $1
    RM_FILES = rm -f $(1)
    TEST_PREFIX = ./
endif

TARGET = dot-explorer$(EXE)
TEST_NAMES = test_fs test_state test_utils test_task test_input test_process test_ui
TEST_TARGETS = $(addsuffix $(EXE),$(TEST_NAMES))

all: $(TARGET)

test: $(TEST_TARGETS)
	$(foreach t,$(TEST_TARGETS),$(TEST_PREFIX)$(t) &&) echo All tests passed

test_fs$(EXE): tests/test_fs.c src/fs.c src/state.c src/task.c src/utils.c
	$(CC) $(CFLAGS) $^ -pthread -o $@

test_state$(EXE): tests/test_state.c src/state.c src/fs.c src/utils.c
	$(CC) $(CFLAGS) $^ -o $@

test_utils$(EXE): tests/test_utils.c src/utils.c src/utils/theme.c
	$(CC) $(CFLAGS) $^ -o $@

test_task$(EXE): tests/test_task.c src/task.c src/fs.c src/utils.c
	$(CC) $(CFLAGS) $^ -pthread -o $@

test_input$(EXE): tests/test_input.c src/input.c src/process.c src/fs.c src/state.c src/utils.c
	$(CC) $(CFLAGS) $^ -o $@

test_process$(EXE): tests/test_process.c src/process.c
	$(CC) $(CFLAGS) $^ -o $@

test_ui$(EXE): tests/test_ui.c src/ui.c src/input.c src/process.c src/fs.c src/state.c src/task.c src/utils.c src/utils/theme.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(call MKDIR,$(dir $@))
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	$(call MKDIR,$(OBJ_DIR)/utils)

clean:
	$(call RM,$(OBJ_DIR))
	$(call RM_FILES,$(TARGET) $(TEST_TARGETS))

.PHONY: all test clean $(TEST_TARGETS)
