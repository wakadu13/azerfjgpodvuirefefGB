# ZEFFALIE COME BACK - Operation HK416
# Usage :
#   make                         (raylib installee sur le systeme)
#   make RAYLIB_PATH=/chemin/raylib   (raylib compilee depuis les sources)

CC      ?= cc
TARGET  := zeffalie_come_back
SRC     := $(wildcard src/*.c)
HDR     := $(wildcard src/*.h)
CFLAGS  += -std=c99 -O2 -Wall -Wextra -Wno-missing-field-initializers

ifdef RAYLIB_PATH
    CFLAGS  += -I$(RAYLIB_PATH)/src
    LDFLAGS += -L$(RAYLIB_PATH)/src
endif

# Serveur de parties par defaut (liste mondiale + relais) :
#   make ZCB_SERVEUR=jeu.exemple.fr   (ou variable d'environnement ZCB_SERVEUR)
ifneq ($(ZCB_SERVEUR),)
    SERVER_DEF := -DONLINE_SERVER=\"$(ZCB_SERVEUR)\"
endif
CFLAGS += $(SERVER_DEF)

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
WINDRES  ?= x86_64-w64-mingw32-windres
windows: $(SRC) $(HDR) assets/icon.rc assets/icon.ico
	mkdir -p release
	# Icone de l'exe (poussin enchaine) : ressource Windows compilee en objet
	$(WINDRES) -I assets assets/icon.rc -O coff -o icon_res.o
	$(MINGW_CC) -std=c99 -O2 $(SERVER_DEF) -I$(RAYLIB_WIN_PATH)/src $(SRC) icon_res.o -o release/ZeffalieComeBack.exe \
		-L$(RAYLIB_WIN_PATH)/src -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -static -static-libgcc -mwindows

# Serveur de parties (Linux, macOS, BSD ; aucune dependance) : make serveur
serveur: zcb-server
zcb-server: server/zcb_server.c src/online_proto.h
	$(CC) -std=c99 -O2 -Wall -Wextra -o $@ server/zcb_server.c

# Tests du serveur et de la couche reseau du jeu (python3 necessaire)
test-serveur: zcb-server
	$(CC) -std=c99 -O2 -Isrc -o test_jeu_relais server/test_jeu_relais.c src/netsock.c src/online.c -lpthread
	python3 server/test_serveur.py ./zcb-server
	./zcb-server --port 28099 --www . > /dev/null & PID=$$!; sleep 0.3; ./test_jeu_relais 28099; R=$$?; kill $$PID; exit $$R

clean:
	rm -f $(TARGET) $(TARGET).exe icon_res.o zcb-server test_jeu_relais

.PHONY: all run clean windows serveur test-serveur
