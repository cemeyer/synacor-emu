#include <check.h>

#include "emu.h"
#include "test.h"

#define	PC_START		0
#define	REG(x)	(32768 + x)

/* Make it harder to forget to add tests to suite. */
#pragma GCC diagnostic error "-Wunused-function"

static void
install_words(uint16_t *code, uint32_t addr, size_t sz)
{

	memcpy(&memory[addr], code, sz);
}

START_TEST(test_halt)
{
	uint16_t code[] = {
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, PC_START + 1);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_nop)
{
	uint16_t code[] = {
		21,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, PC_START + 1);
	emulate1();
	ck_assert_uint_eq(pc, PC_START + 2);
	ck_assert_uint_eq(halted, true);
}
END_TEST

/* Assumes outfile is a seekable stream, e.g., fmemopen(3). */
static void
check_out_contents(size_t pos, const char *bytes)
{
	char *buf;
	off_t off;
	size_t rd;
	int rc;

	/* Verify position */
	off = ftello(outfile);
	ck_assert_int_ge(off, 0);

	ck_assert_uint_eq((size_t)off, pos);

	/* Verify contents */
	rc = fseeko(outfile, 0, SEEK_SET);
	ck_assert_int_eq(rc, 0);

	buf = malloc(pos);
	ck_assert_ptr_ne(buf, NULL);

	rd = fread(buf, 1, pos, outfile);
	ck_assert_uint_eq(rd, pos);

	rc = memcmp(buf, bytes, pos);
	ck_assert_int_eq(rc, 0);

	free(buf);

	/* Rewind back to end of stream */
	rc = fseeko(outfile, pos, SEEK_SET);
	ck_assert_int_eq(rc, 0);
}

START_TEST(test_out)
{
	uint16_t code[] = {
		19, 'h',
		19, 'i',
		19, '\n',
		19, REG(0),
		0,
	};

	install_words(code, PC_START, sizeof(code));

	outfile = fmemopen(NULL, 10, "w+");
	check_out_contents(0, "");

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 2);
	check_out_contents(1, "h");

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 4);
	check_out_contents(2, "hi");

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 6);
	check_out_contents(3, "hi\n");

	regs[0] = 'a';
	emulate1();
	ck_assert_uint_eq(pc, PC_START + 8);
	check_out_contents(4, "hi\na");

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 9);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_in)
{
	uint16_t code[] = {
		20, REG(0),
		20, REG(1),
		20, REG(2),
		0,
	};
	size_t wr;
	int rc;

	install_words(code, PC_START, sizeof(code));

	infile = fmemopen(NULL, 10, "w+");
	wr = fwrite("hi\n", 1, 3, infile);
	ck_assert_uint_eq(wr, 3);

	rc = fseeko(infile, 0, SEEK_SET);
	ck_assert_int_eq(rc, 0);

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 2);
	ck_assert_uint_eq(regs[0], 'h');

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 4);
	ck_assert_uint_eq(regs[1], 'i');

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 6);
	ck_assert_uint_eq(regs[2], '\n');

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 7);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_jmp)
{
	uint16_t code[] = {
		6, 3,
		21,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 3);
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_jmp_reg)
{
	uint16_t code[] = {
		6, REG(0),
		21,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	regs[0] = 3;
	emulate1();
	ck_assert_uint_eq(pc, 3);
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_jf)
{
	uint16_t code[] = {
		8, 0, 4,
		21,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_jt)
{
	uint16_t code[] = {
		7, 1, 4,
		21,
		7, 0, 0,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	emulate1();
	ck_assert_uint_eq(pc, 7);
	emulate1();
	ck_assert_uint_eq(pc, 8);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_ld)
{
	uint16_t code[] = {
		1, REG(0), 5,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(regs[0], 5);
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_add)
{
	uint16_t code[] = {
		9, REG(0), 32064, 885,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 181);
	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_and)
{
	uint16_t code[] = {
		12, REG(0), 32064, 885,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 320);
	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_eq)
{
	uint16_t code[] = {
		4, REG(0), 15, 15,
		4, REG(0), 15, 2,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 1);
	emulate1();
	ck_assert_uint_eq(pc, 8);
	ck_assert_uint_eq(regs[0], 0);
	emulate1();
	ck_assert_uint_eq(pc, 9);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_gt)
{
	uint16_t code[] = {
		5, REG(0), 15, 15,
		5, REG(0), 16, 15,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 0);
	emulate1();
	ck_assert_uint_eq(pc, 8);
	ck_assert_uint_eq(regs[0], 1);
	emulate1();
	ck_assert_uint_eq(pc, 9);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_mult)
{
	uint16_t code[] = {
		10, REG(0), 5, 7,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 35);
	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_mod)
{
	uint16_t code[] = {
		11, REG(0), 12, 7,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 5);
	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_not)
{
	uint16_t code[] = {
		14, REG(0), 32064,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(regs[0], 703);
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_or)
{
	uint16_t code[] = {
		13, REG(0), 32064, 885,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[0], 32629);
	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_push)
{
	uint16_t code[] = {
		2, REG(0),
		2, 15,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	regs[0] = 14123;

	emulate1();
	ck_assert_uint_eq(pc, 2);
	ck_assert_uint_eq(stack[0], 14123);
	ck_assert_uint_eq(stack_depth, 1);
	ck_assert_uint_ge(stack_alloc, 1);

	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(stack[0], 14123);
	ck_assert_uint_eq(stack[1], 15);
	ck_assert_uint_eq(stack_depth, 2);
	ck_assert_uint_ge(stack_alloc, 2);

	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_pop)
{
	uint16_t code[] = {
		2, REG(0),
		3, REG(1),
		0,
	};

	install_words(code, PC_START, sizeof(code));
	regs[0] = 14123;

	emulate1();
	ck_assert_uint_eq(pc, 2);
	ck_assert_uint_eq(stack[0], 14123);
	ck_assert_uint_eq(stack_depth, 1);
	ck_assert_uint_ge(stack_alloc, 1);

	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(regs[1], 14123);
	ck_assert_uint_eq(stack_depth, 0);
	ck_assert_uint_ge(stack_alloc, 0);

	emulate1();
	ck_assert_uint_eq(pc, 5);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_call)
{
	uint16_t code[] = {
		17, 3,
		21,
		0,
	};

	install_words(code, PC_START, sizeof(code));

	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(stack[0], 2);
	ck_assert_uint_eq(stack_depth, 1);
	ck_assert_uint_ge(stack_alloc, 1);

	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_ret)
{
	uint16_t code[] = {
		17, 3,
		0,
		18,
	};

	install_words(code, PC_START, sizeof(code));

	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(stack[0], 2);
	ck_assert_uint_eq(stack_depth, 1);
	ck_assert_uint_ge(stack_alloc, 1);

	emulate1();
	ck_assert_uint_eq(pc, 2);
	ck_assert_uint_eq(stack_depth, 0);
	ck_assert_uint_ge(stack_alloc, 0);

	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_rmem)
{
	uint16_t code[] = {
		15, REG(0), 0,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(regs[0], 15);
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

START_TEST(test_wmem)
{
	uint16_t code[] = {
		16, 3, 0,
		21,
		0,
	};

	install_words(code, PC_START, sizeof(code));
	emulate1();
	ck_assert_uint_eq(pc, 3);
	ck_assert_uint_eq(memory[3], 0);
	emulate1();
	ck_assert_uint_eq(pc, 4);
	ck_assert_uint_eq(halted, true);
}
END_TEST

Suite *
suite_instr(void)
{
	Suite *s;
	TCase *t;

	s = suite_create("instr");

	t = tcase_create("print");
	tcase_add_checked_fixture(t, init, destroy);
	tcase_add_test(t, test_halt);
	tcase_add_test(t, test_in);
	tcase_add_test(t, test_nop);
	tcase_add_test(t, test_out);
	suite_add_tcase(s, t);

	t = tcase_create("flowcontrol");
	tcase_add_checked_fixture(t, init, destroy);
	tcase_add_test(t, test_call);
	tcase_add_test(t, test_jmp);
	tcase_add_test(t, test_jmp_reg);
	tcase_add_test(t, test_jf);
	tcase_add_test(t, test_jt);
	tcase_add_test(t, test_ret);
	suite_add_tcase(s, t);

	t = tcase_create("basic");
	tcase_add_checked_fixture(t, init, destroy);
	tcase_add_test(t, test_eq);
	tcase_add_test(t, test_gt);
	tcase_add_test(t, test_ld);
	tcase_add_test(t, test_rmem);
	tcase_add_test(t, test_wmem);
	suite_add_tcase(s, t);

	t = tcase_create("math");
	tcase_add_checked_fixture(t, init, destroy);
	tcase_add_test(t, test_add);
	tcase_add_test(t, test_and);
	tcase_add_test(t, test_mult);
	tcase_add_test(t, test_mod);
	tcase_add_test(t, test_not);
	tcase_add_test(t, test_or);
	suite_add_tcase(s, t);

	t = tcase_create("stack");
	tcase_add_checked_fixture(t, init, destroy);
	tcase_add_test(t, test_pop);
	tcase_add_test(t, test_push);
	suite_add_tcase(s, t);

	return (s);
}
