#include <check.h>

#include "emu.h"
#include "test.h"

#define	PC_START		0

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

	emulate1();
	ck_assert_uint_eq(pc, PC_START + 7);
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
	tcase_add_test(t, test_nop);
	tcase_add_test(t, test_out);
	suite_add_tcase(s, t);

	return (s);
}
