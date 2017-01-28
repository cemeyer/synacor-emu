#include <openssl/des.h>

#include "emu.h"
#include "instr.h"

static uint16_t
getinput(uint16_t instr, uint16_t literal)
{

	if (literal <= INT16_MAX)
		return (literal);
	if (literal <= 32775)
		return (regs[literal - 32768]);
	illins(instr);
}

void
instr_halt(struct instr_decode_common *idc __unused)
{

	halted = true;
}

void
instr_jmp(struct instr_decode_common *idc)
{
	uint16_t dst;

	dst = getinput(idc->instr, idc->args[0]);

	/* Decrement by size of jmp <a> instruction */
	pc = dst - 2;
}

void
instr_nop(struct instr_decode_common *idc __unused)
{
}

void
instr_out(struct instr_decode_common *idc)
{

	fputc((char)idc->args[0], outfile);
}

void
instr_unimp(struct instr_decode_common *idc __unused)
{

	unhandled(idc->instr);
}
