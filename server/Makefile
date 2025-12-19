CC = clang
CFLAGS = -Wall -g
BUILD_DIR = build

all: $(BUILD_DIR)/server

# 1.  LINKING:  Create final executable from object files ($a = target (e.g., build/server), $^ = dependencies (e.g. build/server.o))
$(BUILD_DIR)/server: $(BUILD_DIR)/server.o
	$(CC) $(CFLAGS) -o $@ $^

# 2.  COMPILING:  create object files from source files
$(BUILD_DIR)/server.o: src/server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 3.  BUILD_DIR:  creates the build directory if missing
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 4.  CLEAN:  removes build directory
clean:
	rm -rf $(BUILD_DIR)

# prevents conflicts if file named clean or all in dir
.PHONY: all clean
