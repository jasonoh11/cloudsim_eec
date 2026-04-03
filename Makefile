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
INPUT ?= ./tmp/Input
TIMESTAMP = $(shell date +%Y%m%d_%H%M%S)
LOG_FILE = $(LOG_DIR)/$(LABEL)_$(TIMESTAMP).log

# Run with verbose flag set to 3
run-v:
	./simulator -v 3 $(INPUT)

# Run with verbose flag set to 4 so scheduler-level messages are visible.
run-v4:
	./simulator -v 4 $(INPUT)

# Run and capture all console output to a labeled, timestamped log file.
# Usage examples:
#   make run-log
#   make run-log LABEL=baseline
#   make run-v-log LABEL=phase2
run-log: $(TARGET)
	@mkdir -p $(LOG_DIR)
	@echo "Writing log to $(LOG_FILE)"
	@./$(TARGET) $(INPUT) > $(LOG_FILE) 2>&1
	@echo "Completed. Log: $(LOG_FILE)"

run-v-log: $(TARGET)
	@mkdir -p $(LOG_DIR)
	@echo "Writing log to $(LOG_FILE)"
	@./$(TARGET) -v 3 $(INPUT) > $(LOG_FILE) 2>&1
	@echo "Completed. Log: $(LOG_FILE)"

# Higher-verbosity log target for easier hang-vs-running diagnosis.
# Usage examples:
#   make run-v4-log
#   make run-v4-log LABEL=debug
run-v4-log: $(TARGET)
	@mkdir -p $(LOG_DIR)
	@echo "Writing log to $(LOG_FILE)"
	@./$(TARGET) -v 4 $(INPUT) > $(LOG_FILE) 2>&1
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

.PHONY: all scheduler run-v run-v4 run-log run-v-log run-v4-log
