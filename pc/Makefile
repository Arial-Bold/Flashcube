# PC test build (no devkitPPC needed): runs Flashcube on a PC, drawing
# through gxemu.c, for screenshots and tests.
SRC = ../source
CFLAGS = -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -I inc -I $(SRC) -I ../third_party/stb -DPC_TEST
APP = $(filter-out $(SRC)/sound.c, $(wildcard $(SRC)/*.c))
shots: shots.c gxemu.c $(SRC)/gfx.c $(SRC)/ui.c $(SRC)/cube.c $(SRC)/logo.c $(SRC)/storage.c
	gcc $(CFLAGS) -o $@ $^ -lm -lpthread
apptest: apptest.c gxemu.c pc_sound.c $(APP)
	gcc $(CFLAGS) -o $@ $^ -lm -lpthread
