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

static uint16_t
popval(uint16_t instr)
{

	if (stack_depth == 0)
		illins(instr);
	return (stack[--stack_depth]);
}

static void
pushval(uint16_t val)
{

	if (stack_depth == stack_alloc) {
		if (stack_alloc == 0)
			stack_alloc = 4096 / sizeof(*stack);
		else
			stack_alloc = stack_alloc * 2;

		stack = realloc(stack, stack_alloc * sizeof(*stack));
		ASSERT(stack != NULL, "realloc");
	}
	stack[stack_depth++] = val;
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
instr_and(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	setreg(idc->instr, dst, src1 & src2);
}

void
instr_call(struct instr_decode_common *idc)
{
	uint16_t dst;

	dst = getinput(idc->instr, idc->args[0]);

	pushval(pc + 2);

	/* Decrement by size of jmp <a> instruction */
	pc = dst - 2;
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
instr_in(struct instr_decode_common *idc)
{
	int rc;

	rc = fgetc(infile);
	if (rc == EOF) {
		fprintf(stderr, "Cannot proceed without input.\n");
		halted = true;
	}

	setreg(idc->instr, idc->args[0], (char)rc);
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
instr_mod(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	setreg(idc->instr, dst, src1 % src2);
}

void
instr_mult(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	setreg(idc->instr, dst, modmath(src1 * src2));
}

void
instr_nop(struct instr_decode_common *idc __unused)
{
}

void
instr_not(struct instr_decode_common *idc)
{
	uint16_t src, dst;

	dst = idc->args[0];
	src = getinput(idc->instr, idc->args[1]);

	setreg(idc->instr, dst, modmath(~src));
}

void
instr_or(struct instr_decode_common *idc)
{
	uint16_t src1, src2, dst;

	dst = idc->args[0];
	src1 = getinput(idc->instr, idc->args[1]);
	src2 = getinput(idc->instr, idc->args[2]);

	setreg(idc->instr, dst, src1 | src2);
}

void
instr_out(struct instr_decode_common *idc)
{
	char c;

	c = getinput(idc->instr, idc->args[0]);
	fputc(c, outfile);
}

void
instr_pop(struct instr_decode_common *idc)
{
	uint16_t dst;

	dst = idc->args[0];
	setreg(idc->instr, dst, popval(idc->instr));
}

void
instr_push(struct instr_decode_common *idc)
{
	uint16_t src;

	src = getinput(idc->instr, idc->args[0]);
	pushval(src);
}

void
instr_ret(struct instr_decode_common *idc)
{
	uint16_t dst;

	dst = popval(idc->instr);
	/* Decrement by size of ret instruction */
	pc = dst - 1;
}

void
instr_rmem(struct instr_decode_common *idc)
{
	uint16_t src, dst;

	dst = idc->args[0];
	src = getinput(idc->instr, idc->args[1]);

	ASSERT(src < ARRAYLEN(memory), "overflow");
	setreg(idc->instr, dst, memory[src]);
}

void
instr_wmem(struct instr_decode_common *idc)
{
	uint16_t src, dst;

	dst = getinput(idc->instr, idc->args[0]);
	src = getinput(idc->instr, idc->args[1]);

	ASSERT(dst < ARRAYLEN(memory), "overflow");
	memory[dst] = src;
}

void
instr_unimp(struct instr_decode_common *idc __unused)
{

	unhandled(idc->instr);
}
