#ifndef __EMU_H__
#define __EMU_H__

#include <sys/cdefs.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#define	likely(cond)	__builtin_expect ((cond), 1)
#define	unlikely(cond)	__builtin_expect ((cond), 0)
#ifndef	__unused
#define	__unused	__attribute__((unused))
#endif
#ifndef	__dead2
#define	__dead2		__attribute__((__noreturn__))
#endif

#define	ARRAYLEN(arr)	((sizeof(arr)) / sizeof((arr)[0]))

#define	min(x, y)	({						\
	typeof(x) _min1 = (x);						\
	typeof(y) _min2 = (y);						\
	(void) (&_min1 == &_min2);					\
	_min1 < _min2 ? _min1 : _min2; })

typedef unsigned int uns;

#define	sec		1000000ULL
#define	ptr(X)		((void*)((uintptr_t)X))

#define ASSERT(cond, args...) do {					\
	if (likely(!!(cond)))						\
		break;							\
	printf("%s:%u: ASSERT %s failed: ", __FILE__, __LINE__, #cond);	\
	printf(args);							\
	printf("\n");							\
	abort_nodump();							\
} while (0)

extern uint32_t		 pc;
extern uint32_t		 pc_start;
extern uint32_t		 instr_size;
extern bool		 halted;
extern uint16_t		 regs[8];
extern uint16_t		 memory[0x10000 / sizeof(uint16_t)];
extern bool		 replay_mode;
extern uint64_t		 insns;
extern uint64_t		 insnreplaylim;
extern uint64_t		 insnlimit;
extern FILE		*outfile;

void		 abort_nodump(void) __dead2;
void		 init(void);
void		 destroy(void);
void		 emulate(void);
void		 emulate1(void);
#define	unhandled(instr)	_unhandled(__FILE__, __LINE__, instr)
void		 _unhandled(const char *f, unsigned l, uint16_t instr) __dead2;
#define	illins(instr)		_illins(__FILE__, __LINE__, instr)
void		 _illins(const char *f, unsigned l, uint16_t instr) __dead2;
void		 print_regs(void);
/* Microseconds: */
uint64_t	 now(void);
#ifndef	EMU_CHECK
void		 getsn(uint16_t addr, uint16_t len);
#endif

void		 print_ips(void);

#endif
