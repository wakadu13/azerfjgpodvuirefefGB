# BURGER RUSH - Operation HK416
# Usage :
#   make                         (raylib installee sur le systeme)
#   make RAYLIB_PATH=/chemin/raylib   (raylib compilee depuis les sources)

CC      ?= cc
TARGET  := burger_rush
SRC     := src/main.c
CFLAGS  += -std=c99 -O2 -Wall -Wextra -Wno-missing-field-initializers

ifdef RAYLIB_PATH
    CFLAGS  += -I$(RAYLIB_PATH)/src
    LDFLAGS += -L$(RAYLIB_PATH)/src
endif

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
    LDLIBS := -lraylib -lopengl32 -lgdi32 -lwinmm
else ifeq ($(UNAME_S),Darwin)
    LDLIBS := -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio
else
    LDLIBS := -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all run clean
