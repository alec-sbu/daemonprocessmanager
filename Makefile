CC := gcc
SRCD := src
TSTD := tests
BLDD := build
BIND := bin
INCD := include
LIBD := lib
SRVD := daemons

ALL_SRCF := $(shell find $(SRCD) -type f -name *.c)
ALL_LIBF := $(shell find $(LIBD) -type f -name *.o)
ALL_OBJF := $(patsubst $(SRCD)/%,$(BLDD)/%,$(ALL_SRCF:.c=.o) $(ALL_LIBF))
FUNC_FILES := $(filter-out build/main.o, $(ALL_OBJF))

TEST_SRC := $(shell find $(TSTD) -type f -name *.c)

INC := -I $(INCD) -I $(LIBD)

CFLAGS := -Wall -Werror -Wno-unused-function -MMD
COLORF := -DCOLOR
DFLAGS := -g -DDEBUG -DCOLOR -DNO_DEBUG_SOURCE_INFO
PRINT_STAMENTS := -DERROR -DSUCCESS -DWARN -DINFO

STD := -std=c99
POSIX := -D_POSIX_SOURCE
BSD := -D_DEFAULT_SOURCE
GNU := -D_GNU_SOURCE
TEST_LIB := -lcriterion
LIBS :=

CFLAGS += $(STD) $(POSIX) $(GNU)

EXEC := legion
TEST := $(EXEC)_tests

.PHONY: clean
.PHONY: all setup debug

all: setup $(BIND)/$(EXEC) $(BIND)/$(WORKER_EXEC) $(BIND)/$(TEST) daemons

debug: CFLAGS += $(DFLAGS) $(PRINT_STAMENTS) $(COLORF)
debug: all

TRACK_FLAG:= -DTRACKING

setup: $(BIND) $(BLDD)
$(BIND):
	mkdir -p $(BIND)
$(BLDD):
	mkdir -p $(BLDD)

$(BIND)/$(EXEC): $(ALL_OBJF)
	$(CC) $(BLDD)/main.o $(FUNC_FILES) -o $@ $(LIBS)

$(BIND)/$(TEST): $(FUNC_FILES) $(TEST_SRC)
	$(CC) $(CFLAGS) $(INC) $(TEST_SRC) $(TEST_LIB) $(LIBS) -o $@

$(BLDD)/%.o: $(SRCD)/%.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(LIBD)/%.o: $(LIBD)/%.c
	$(CC) $(CFLAGS) $(INC) $(TRACK_FLAG) -c -o $@ $<

$(SRVD)/%: $(SRVD)/%.c
	$(CC) $(CFLAGS) $(INC) -o $@ $<

clean:
	rm -rf $(BLDD) $(BIND)

.PRECIOUS: $(BLDD)/*.d
-include $(BLDD)/*.d
