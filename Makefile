# Compiler
CXX = g++
# Compiler flags
CXXFLAGS = -Wall -std=c++17
# Include directories
INCLUDES = -I.

# Source files
SRC = Init.cpp Machine.cpp main.cpp Scheduler.cpp Simulator.cpp Task.cpp VM.cpp

# Object files
OBJ = $(SRC:.cpp=.o)

# Executable
TARGET = simulator
LOG_DIR = tmp/run_logs
LABEL ?= run
TIMESTAMP = $(shell date +%Y%m%d_%H%M%S)
LOG_FILE = $(LOG_DIR)/$(LABEL)_$(TIMESTAMP).log

# Run with verbose flag set to 3
run-v:
	./simulator -v 3 ./tmp/Input

# Run and capture all console output to a labeled, timestamped log file.
# Usage examples:
#   make run-log
#   make run-log LABEL=baseline
#   make run-v-log LABEL=phase2
run-log: $(TARGET)
	@mkdir -p $(LOG_DIR)
	@echo "Writing log to $(LOG_FILE)"
	@./$(TARGET) ./tmp/Input > $(LOG_FILE) 2>&1
	@echo "Completed. Log: $(LOG_FILE)"

run-v-log: $(TARGET)
	@mkdir -p $(LOG_DIR)
	@echo "Writing log to $(LOG_FILE)"
	@./$(TARGET) -v 3 ./tmp/Input > $(LOG_FILE) 2>&1
	@echo "Completed. Log: $(LOG_FILE)"

# Default target
all: $(TARGET)

# Default target
scheduler: $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o scheduler $(OBJ)

# Build target
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(OBJ)

# Compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: all scheduler run-v run-log run-v-log
