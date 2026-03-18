CC = gcc
CFLAGS = -Wall -Werror -Wextra -O2
DEBUG_FLAGS = -Wall -Werror -Wextra -g -fsanitize=address -DDEBUG
INCLUDES = -Iinclude -Isrc

SRC_DIR = src
TEST_DIR = tests
INCLUDE_DIR = include

# Source files
MEMLIB_SRC = $(SRC_DIR)/memlib.c
MM_SRC = $(SRC_DIR)/mm.c
COMMON_SRC = $(MEMLIB_SRC) $(MM_SRC)

# Test executables
TEST_BASIC = test_basic
TEST_COALESCE = test_coalesce
TEST_STRESS = test_stress

.PHONY: all clean test debug benchmark

all: $(TEST_BASIC) $(TEST_COALESCE) $(TEST_STRESS)

$(TEST_BASIC): $(TEST_DIR)/test_basic.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

$(TEST_COALESCE): $(TEST_DIR)/test_coalesce.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

$(TEST_STRESS): $(TEST_DIR)/test_stress.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

debug: CFLAGS = $(DEBUG_FLAGS)
debug: clean all

test: all
	@echo "=== Running Basic Tests ==="
	./$(TEST_BASIC)
	@echo ""
	@echo "=== Running Coalesce Tests ==="
	./$(TEST_COALESCE)
	@echo ""
	@echo "=== Running Stress Tests ==="
	./$(TEST_STRESS)

benchmark: $(TEST_STRESS)
	@echo "=== Running Benchmarks ==="
	./scripts/benchmark.sh

clean:
	rm -f $(TEST_BASIC) $(TEST_COALESCE) $(TEST_STRESS)
	rm -rf *.dSYM
