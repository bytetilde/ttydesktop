CC := clang
CFLAGS := -Wall -Wextra -Iinclude -MMD -MP -fPIC -pthread -O0 -g
LDFLAGS := -fuse-ld=lld -pthread -ldl -rdynamic

TARGET := ttydesktop

SRC_DIR := src
APP_DIR := src/apps
OBJ_DIR := obj
BIN_DIR := bin
INC_DIR := include

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)
APP_SOURCES := $(wildcard $(APP_DIR)/*.c)
APP_TARGETS := $(APP_SOURCES:$(APP_DIR)/%.c=$(BIN_DIR)/%.so)

all: all-desktop all-apps
all-desktop: $(BIN_DIR)/$(TARGET)
all-apps: $(APP_TARGETS)

$(BIN_DIR)/$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(BIN_DIR)/%.so: $(APP_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -shared $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

run: all-desktop
	./$(BIN_DIR)/$(TARGET)

valgrind: all-desktop
	valgrind --leak-check=full --show-leak-kinds=all ./$(BIN_DIR)/$(TARGET)

-include $(DEPS)

.PHONY: all all-desktop all-apps clean run valgrind
