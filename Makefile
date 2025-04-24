CC = gcc
CFLAGS = -Wall -Wextra -pedantic -O2 -pthread
INCLUDES = -Iinclude
LIBS = -lpthread -lpq -lz

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SERVER = $(BIN_DIR)/cserver

SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC_FILES))

.PHONY: all clean

all: dirs $(SERVER)

dirs:
	mkdir -p $(OBJ_DIR) $(BIN_DIR)

$(SERVER): $(OBJ_FILES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

