CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pthread -Iinclude
BENCHFLAGS := -std=c++20 -O3 -march=native -Wall -Wextra -pthread -Iinclude
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/test_suite
BENCH_TGT := $(BIN_DIR)/benchmark

all: $(TARGET)

$(TARGET): tests/test_suite.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

bench_build: tests/benchmark.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(BENCHFLAGS) $^ -o $(BENCH_TGT)

bench: bench_build
	./$(BENCH_TGT)

test: $(TARGET)
	./$(TARGET)

asan: CXXFLAGS += -fsanitize=address,undefined -g
asan: reconfig $(TARGET)
	./$(TARGET)

tsan: CXXFLAGS += -fsanitize=thread -g
tsan: reconfig $(TARGET)
	./$(TARGET)

reconfig:
	@rm -rf $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test bench bench_build asan tsan reconfig clean
