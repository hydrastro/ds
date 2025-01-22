CC = gcc
AR = ar
CFLAGS = -c -fPIC -std=c89
CFLAGS += -O2 -march=native -mtune=native
CFLAGS += -Wall -Wextra -Werror -pedantic -pedantic-errors
CFLAGS += -fno-common -Wl,--gc-sections -Wredundant-decls -Wno-unused-parameter
CFLAGS += -fstack-protector-strong #-fPIE
CFLAGS += -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security
CFLAGS += -fstack-clash-protection -z noexecstack -z relro -z now
CFLAGS += -Wl,-z,relro,-z,now -Wl,-pie #-fpie
CFLAGS += -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual -Wdeclaration-after-statement
CLFAGS += -Wfloat-equal -Wlogical-op -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes -Wnested-externs
CFLAGS += -Wpointer-arith -Wredundant-decls -Wsequence-point -Wstrict-prototypes
CFLAGS += -Wswitch -Wundef -Wunreachable-code -Wwrite-strings -Wconversion

CFLAGS_DEBUG = -g -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize=leak

CFLAGS_SAFE = -D DS_THREAD_SAFE
LDFLAGS = -shared

LIB_DIR = lib
INCLUDE_DIR = include
PREFIX ?= /usr/local
OUT_LIB_DIR = $(PREFIX)/lib
OUT_INCLUDE_DIR = $(PREFIX)/include/lib

SRC_FILES = $(wildcard $(LIB_DIR)/*.c)
OBJ_FILES = $(SRC_FILES:.c=.o)
OBJ_FILES_SAFE = $(SRC_FILES:.c=_safe.o)

all: libds.a libds.so libds_safe.a libds_safe.so

libds.a: $(OBJ_FILES)
	$(AR) rcs $@ $^

libds.so: $(OBJ_FILES)
	$(CC) $(LDFLAGS) -o $@ $^

libds_safe.a: $(OBJ_FILES_SAFE)
	$(AR) rcs $@ $^

libds_safe.so: $(OBJ_FILES_SAFE)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

%_safe.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_SAFE) $< -o $@

install: all
	mkdir -p $(OUT_LIB_DIR)
	mkdir -p $(OUT_INCLUDE_DIR)
	cp libds.a libds.so $(OUT_LIB_DIR)/
	cp libds_safe.a libds_safe.so $(OUT_LIB_DIR)/
	cp $(LIB_DIR)/*.h $(OUT_INCLUDE_DIR)/
	cp ds.h $(PREFIX)/include/

uninstall:
	rm -f $(OUT_LIB_DIR)/libds.a $(OUT_LIB_DIR)/libds.so
	rm -f $(OUT_LIB_DIR)/libds_safe.a $(OUT_LIB_DIR)/libds_safe.so
	rm -rf $(OUT_INCLUDE_DIR)
	rm -f $(PREFIX)/include/ds.h

clean:
	rm -f $(OBJ_FILES) $(OBJ_FILES_SAFE) libds.a libds.so libds_safe.a libds_safe.so
