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

static void
setreg(uint16_t instr, uint16_t dst, uint16_t src)
{

	if (dst < 32768 || dst > 32775)
		illins(instr);

	dst -= 32768;
	regs[dst] = src;
}

static inline uint16_t
modmath(uint16_t val)
{

	return (val & 0x7fff);
}

void
instr_add(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	setreg(idc->instr, dst, modmath(src1 + src2));
}

void
instr_eq(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	if (src1 == src2)
		setreg(idc->instr, dst, 1);
	else
		setreg(idc->instr, dst, 0);
}

void
instr_gt(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	if (src1 > src2)
		setreg(idc->instr, dst, 1);
	else
		setreg(idc->instr, dst, 0);
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
instr_jf(struct instr_decode_common *idc)
{
	uint16_t cnd, dst;

	cnd = getinput(idc->instr, idc->args[0]);
	dst = getinput(idc->instr, idc->args[1]);

	/* Decrement by size of jf <a> <b> instruction */
	if (cnd == 0)
		pc = dst - 3;
}

void
instr_jt(struct instr_decode_common *idc)
{
	uint16_t cnd, dst;

	cnd = getinput(idc->instr, idc->args[0]);
	dst = getinput(idc->instr, idc->args[1]);

	/* Decrement by size of jt <a> <b> instruction */
	if (cnd != 0)
		pc = dst - 3;
}

void
instr_ld(struct instr_decode_common *idc)
{
	uint16_t src, dst;

	dst = idc->args[0];
	src = getinput(idc->instr, idc->args[1]);

	setreg(idc->instr, dst, src);
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
instr_pop(struct instr_decode_common *idc)
{
	uint16_t dst;

	dst = idc->args[0];

	setreg(idc->instr, dst, stack[--stack_depth]);
}

void
instr_push(struct instr_decode_common *idc)
{
	uint16_t src;

	src = getinput(idc->instr, idc->args[0]);

	if (stack_depth == stack_alloc) {
		if (stack_alloc == 0)
			stack_alloc = 4096 / sizeof(*stack);
		else
			stack_alloc = stack_alloc * 2;

		stack = realloc(stack, stack_alloc * sizeof(*stack));
		ASSERT(stack != NULL, "realloc");
	}

	stack[stack_depth++] = src;
}

void
instr_unimp(struct instr_decode_common *idc __unused)
{

	unhandled(idc->instr);
}
