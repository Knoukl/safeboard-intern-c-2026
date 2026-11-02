CC = gcc
CFLAGS = -std=c11 -Wall -Werror -Wextra

SOURCE_DIR = src/
SOURCES = mangen.c manifest.c sha256.c
SOURCE_FILES = $(addprefix $(SOURCE_DIR), $(SOURCES))
BUILD_DIR = build/
TEST_BUILD_DIR = $(BUILD_DIR)tests/
TEST_EXAMPLE_DIR = $(SOURCE_DIR)tests/example
TEST_SOURCE = $(SOURCE_DIR)tests/test_mangen.c
TEST_BINARY = $(TEST_BUILD_DIR)mangen_tests
TEST_OBJECTS = $(TEST_BUILD_DIR)test_mangen.o \
	$(TEST_BUILD_DIR)mangen_test.o \
	$(TEST_BUILD_DIR)manifest_test.o \
	$(TEST_BUILD_DIR)sha256_test.o
CHECK_CFLAGS = $(shell pkg-config --cflags check)
CHECK_LIBS = $(shell pkg-config --libs check)
COVERAGE_FLAGS = --coverage -O0
GIT_COMMIT_HASH = $(shell git rev-parse HEAD 2>/dev/null || echo unknown)

.PHONY: all clean test rebuild

all: mangen

mangen: 
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) \
		-DGIT_COMMIT_HASH='"$(GIT_COMMIT_HASH)"' \
		$(SOURCE_FILES) -o $(BUILD_DIR)mangen

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

$(TEST_BUILD_DIR)test_mangen.o: $(TEST_SOURCE) $(SOURCE_DIR)mangen.h | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CHECK_CFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)mangen_test.o: $(SOURCE_DIR)mangen.c $(SOURCE_DIR)mangen.h | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) \
		-Dmain=mangen_program_main \
		-DGIT_COMMIT_HASH='"test-commit-hash"' \
		-c $< -o $@

$(TEST_BUILD_DIR)manifest_test.o: $(SOURCE_DIR)manifest.c $(SOURCE_DIR)mangen.h | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) -c $< -o $@

$(TEST_BUILD_DIR)sha256_test.o: $(SOURCE_DIR)sha256.c $(SOURCE_DIR)mangen.h | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) -c $< -o $@

$(TEST_BINARY): $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) $(TEST_OBJECTS) $(CHECK_LIBS) -o $@

test: $(TEST_BINARY)
	rm -f $(TEST_BUILD_DIR)*.gcda
	rm -rf $(TEST_EXAMPLE_DIR)
	mkdir -p $(TEST_EXAMPLE_DIR)/level1/level2 \
		$(TEST_EXAMPLE_DIR)/skip-dir $(TEST_BUILD_DIR)
	printf 'root sample\n' > $(TEST_EXAMPLE_DIR)/root.txt
	printf 'level one sample\n' > $(TEST_EXAMPLE_DIR)/level1/one.txt
	printf 'level two sample\n' > $(TEST_EXAMPLE_DIR)/level1/level2/two.txt
	printf 'excluded by wildcard\n' > $(TEST_EXAMPLE_DIR)/skip-middle-end
	printf 'excluded by single-character wildcard\n' > $(TEST_EXAMPLE_DIR)/sXt
	printf 'excluded directory content\n' > $(TEST_EXAMPLE_DIR)/skip-dir/hidden.txt
	ln -s root.txt $(TEST_EXAMPLE_DIR)/root-link.txt
	ln -s level1 $(TEST_EXAMPLE_DIR)/level1-link
	mkfifo $(TEST_EXAMPLE_DIR)/sample-pipe
	$(TEST_BINARY)
	lcov --capture --directory $(TEST_BUILD_DIR) --base-directory . \
		--no-external --output-file $(TEST_BUILD_DIR)coverage.info
	lcov --summary $(TEST_BUILD_DIR)coverage.info --fail-under-lines 80

clean:
	rm -f $(BUILD_DIR)mangen
	rm -rf $(TEST_BUILD_DIR) $(TEST_EXAMPLE_DIR)

rebuild: clean all
