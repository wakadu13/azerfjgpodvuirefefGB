# BURGER RUSH - Operation HK416
# Usage :
#   make                         (raylib installee sur le systeme)
#   make RAYLIB_PATH=/chemin/raylib   (raylib compilee depuis les sources)

CC      ?= cc
TARGET  := burger_rush
SRC     := $(wildcard src/*.c)
HDR     := $(wildcard src/*.h)
CFLAGS  += -std=c99 -O2 -Wall -Wextra -Wno-missing-field-initializers

ifdef RAYLIB_PATH
    CFLAGS  += -I$(RAYLIB_PATH)/src
    LDFLAGS += -L$(RAYLIB_PATH)/src
endif

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
    LDLIBS := -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32
else ifeq ($(UNAME_S),Darwin)
    LDLIBS := -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio
else
    LDLIBS := -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# Exe Windows autonome (cross-compilation MinGW, raylib compilee pour Windows)
#   make windows RAYLIB_WIN_PATH=/chemin/raylib-windows
MINGW_CC ?= x86_64-w64-mingw32-gcc
windows: $(SRC) $(HDR)
	mkdir -p release
	$(MINGW_CC) -std=c99 -O2 -I$(RAYLIB_WIN_PATH)/src $(SRC) -o release/BurgerRush.exe \
		-L$(RAYLIB_WIN_PATH)/src -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -static -static-libgcc -mwindows

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all run clean windows
