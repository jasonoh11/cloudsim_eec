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
VERBOSE ?= 0
INPUT ?= ./tmp/canvas

run: $(TARGET)
	./$(TARGET) -v $(VERBOSE) $(INPUT)

run-0: VERBOSE=0
run-0: INPUT=./tmp/tall_short
run-0: run

run-1: VERBOSE=1
run-1: INPUT=./tmp/canvas
run-1: run

run-3: VERBOSE=3
run-3: INPUT=./tmp/submitted_input
run-3: run

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
