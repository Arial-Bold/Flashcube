# PC test build (no devkitPPC needed): runs Flashcube on a PC, drawing
# through gxemu.c, for screenshots and tests.
SRC = ../source
CFLAGS = -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -I inc -I $(SRC) -I ../third_party/stb -DPC_TEST
# the secure-connection code needs mbedTLS and the GameCube's network library
APP = $(filter-out $(SRC)/sound.c $(SRC)/conn.c $(SRC)/castore.c $(SRC)/gc_entropy.c $(SRC)/netio.c $(SRC)/https.c, $(wildcard $(SRC)/*.c))
shots: shots.c gxemu.c $(SRC)/gfx.c $(SRC)/ui.c $(SRC)/cube.c $(SRC)/logo.c $(SRC)/storage.c
	gcc $(CFLAGS) -o $@ $^ -lm -lpthread
apptest: apptest.c gxemu.c pc_sound.c $(APP)
	gcc $(CFLAGS) -o $@ $^ -lm -lpthread
sfxpreview: sfxpreview.c $(SRC)/sfxsynth.c $(SRC)/musicsynth.c
	gcc $(CFLAGS) -o $@ $^ -lm
