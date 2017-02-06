PROG=		synacor-emu
SRCS=		main.c instr.c
HDRS=		emu.h
CHECK_SRCS=	check_emu.c check_instr.c test_main.c
CHECK_HDRS=	test.h

WARNFLAGS=	-Wall -Wextra -std=gnu11 -Wno-unused-function -Wno-unused-variable -Wno-missing-field-initializers
OTHERFLAGS=	-fexceptions -Wp,-D_FORTIFY_SOURCE=2
OPTFLAGS=	-O3 -g -pipe -m64 -mtune=native -march=native

# Platform
CC=		cc
CC_VER=		$(shell $(CC) --version)
ifneq (,$(findstring GCC,$(CC_VER)))
    # Perhaps a check for a recent version belongs here.
    NEWGCCFLAGS=	-grecord-gcc-switches -fstack-protector-strong --param=ssp-buffer-size=4
endif
ifneq (,$(findstring clang,$(CC_VER)))
    WARNFLAGS+=		-Wno-unknown-attributes
endif

FLAGS=		$(WARNFLAGS) $(OTHERFLAGS) $(OPTFLAGS) $(NEWGCCFLAGS) $(CFLAGS)
LDLIBS=		$(LDFLAGS)

$(PROG): $(SRCS) $(HDRS)
	$(CC) $(FLAGS) $(SRCS) -o $@ -lz $(LDLIBS)

checkrun: checktests
	./checktests

checkall: checktests $(PROG)
checktests: $(CHECK_SRCS) $(SRCS) $(CHECK_HDRS) $(HDRS)
	$(CC) $(FLAGS) -DEMU_CHECK $(CHECK_SRCS) $(SRCS) -o $@ -lcheck -lz $(LDLIBS)

clean:
	rm -f checktests synacor-emu
