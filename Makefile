LIB_PTYTTY := $(shell find /usr/lib /usr/local/lib /lib -name "libptytty.so*" -o -name "libptytty.a" 2>/dev/null | head -n1)
LIB_VTERM := $(shell find /usr/lib /usr/local/lib /lib -name "libvterm.so*" -o -name "libvterm.a" 2>/dev/null | head -n1)
ifeq ($(LIB_PTYTTY),)
$(info could not find libptytty; you can install it by)
$(info sudo apt install libptytty-dev)
$(info or)
$(info sudo dnf install libptytty-devel)
$(info or)
$(info yay -S libptytty)
$(error build aborted)
endif
ifeq ($(LIB_VTERM),)
$(info could not find libvterm; you can install it by)
$(info sudo apt install libvterm-dev)
$(info or)
$(info sudo dnf install libvterm-devel)
$(info or)
$(info yay -S libvterm)
$(error build aborted)
endif

CXX     := gcc
RM      := rm -rf
MKDIR   := mkdir -p

SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin

TARGET_NAME := vtemu

CXXFLAGS := -Wall -Wextra -O2 -g -fPIE
LDFLAGS  := -Wl,-z,relro,-z,now -pie
LDLIBS   := -lptytty -lvterm

TARGET := $(BIN_DIR)/$(TARGET_NAME)
HDRS   := $(wildcard $(SRC_DIR)/*.h)
SRCS   := $(wildcard $(SRC_DIR)/*.c)
OBJS   := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR):
	$(MKDIR) $@

$(BIN_DIR):
	$(MKDIR) $@

DEPS := $(OBJS:.o=.d)
-include $(DEPS)

clean:
	$(RM) $(OBJ_DIR)

run: all
	$(TARGET)

debug: CXXFLAGS += --DDEBUG -ggdb3 -O0
debug: $(TARGET)
