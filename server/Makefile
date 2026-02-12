CC = clang
CFLAGS = -Wall -g
BUILD_DIR = build
SRC_DIR = src

SRCS = $(SRC_DIR)/server.c $(SRC_DIR)/protocol.c $(SRC_DIR)/game.c
OBJS = $(BUILD_DIR)/server.o $(BUILD_DIR)/protocol.o $(BUILD_DIR)/game.o

all: $(BUILD_DIR)/server

# 1.  LINKING:  Create final executable from object files
$(BUILD_DIR)/server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 2.  COMPILING:  create object files from source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 3.  BUILD_DIR:  creates the build directory if missing
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 4.  CLEAN:  removes build directory
clean:
	rm -rf $(BUILD_DIR)

# prevents conflicts if file named clean or all in dir
.PHONY: all clean
