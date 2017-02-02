#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#include <zlib.h>

#include "emu.h"
#include "instr.h"

struct inprec {
	uint64_t	 ir_insn;
	size_t		 ir_len;
	char		 ir_inp[0];
};

/* Machine state */
uint32_t	 pc,
		 pc_start,
		 instr_size;
bool		 halted;
/* 64kB, word addressed  */
uint16_t	 regs[8];
uint16_t	 memory[0x10000 / sizeof(uint16_t)];
uint16_t	*stack;
size_t		 stack_depth;
size_t		 stack_alloc;

/* Emulater auxiliary info */
uint64_t	 start;		/* Start time in us */
uint64_t	 insns;
uint64_t	 insnlimit;
uint64_t	 insnreplaylim;
bool		 replay_mode;
volatile bool	 ctrlc;

bool		 tracehex;
bool		 tracedisas;
bool		 onlydisas;
bool		 onlytranspile;
FILE		*tracefile;
FILE		*outfile;
FILE		*coutfile;
char		*cout_stream;
size_t		 cout_size;
FILE		*orig_coutfile;
FILE		*infile;

GHashTable	*input_record;			// insns -> inprec

static bool jmplabels[32*1024];

// Could easily sort by popularity over time.
static struct instr_decode synacor_instr[] = {
	{  0, 0, instr_halt, trans_halt, "halt", },
	{  1, 2, instr_ld,   trans_ld,   "mov", },
	{  2, 1, instr_push, trans_push, "push", },
	{  3, 1, instr_pop,  trans_pop,  "pop", },
	{  4, 3, instr_eq,   trans_eq,   "eq", },
	{  5, 3, instr_gt,   trans_gt,   "gt", },
	{  6, 1, instr_jmp,  trans_jmp,  "jmp", },
	{  7, 2, instr_jt,   trans_jt,   "jt", },
	{  8, 2, instr_jf,   trans_jf,   "jf", },
	{  9, 3, instr_add,  trans_add,  "add", },
	{ 10, 3, instr_mult, trans_mult, "mult", },
	{ 11, 3, instr_mod,  trans_mod,  "mod", },
	{ 12, 3, instr_and,  trans_and,  "and", },
	{ 13, 3, instr_or,   trans_or,   "or", },
	{ 14, 2, instr_not,  trans_not,  "not", },
	{ 15, 2, instr_rmem, trans_rmem, "rmem", },
	{ 16, 2, instr_wmem, trans_wmem, "wmem", },
	{ 17, 1, instr_call, trans_call, "call", },
	{ 18, 0, instr_ret,  trans_ret,  "ret", },
	{ 19, 1, instr_out,  trans_out,  "out", },
	{ 20, 1, instr_in,   trans_in,   "in", },
	{ 21, 0, instr_nop,  trans_nop,  "nop", },
};

void
print_ips(void)
{
	uint64_t end = now();

	if (end == start)
		end++;

	printf("Approx. %ju instructions per second (Total: %ju).\n",
	    (uintmax_t)insns * 1000000 / (end - start), (uintmax_t)insns);
}

static void
printarg(FILE *f, uint16_t val, bool last)
{

	if (val <= INT16_MAX)
		fprintf(f, " %u", (uns)val);
	else if (val < 32776)
		fprintf(f, " r%u", (uns)val - 32768);
	else
		fprintf(f, " invalid#%u", (uns)val);
	if (!last)
		fprintf(f, ",");
}

void
init(void)
{

	pc = 0;
	insns = 0;
	halted = false;
	infile = stdin;
	outfile = stdout;
	memset(regs, 0, sizeof(regs));
	stack_depth = stack_alloc = 0;
	start = now();
	//memset(memory, 0, sizeof(memory));
}

void
destroy(void)
{

	free(stack);
	stack = NULL;
}

#ifndef EMU_CHECK
static void
ctrlc_handler(int s)
{

	(void)s;
	ctrlc = true;
}

static void
writes(int fd, const char *str)
{

	(void)write(fd, str, strlen(str));
}

static void
write_errno(int fd)
{
	char buf[11];
	size_t len;
	int error;

	error = errno;
	for (len = 0; error != 0; len++) {
		buf[len] = "0123456789"[error % 10];
		error /= 10;
	}

	for (; len > 0; len--)
		(void)write(fd, &buf[len - 1], 1);
}

static int
writeall(int fd, const void *buf, size_t len)
{
	size_t written;
	ssize_t rc;

	for (written = 0; written < len; written += (size_t)rc) {
		rc = write(fd, (const char *)buf + written,
		    len - written);
		if (rc < 0)
			return (rc);
	}
	return (0);
}

static void
save_handler(int s)
{
	size_t written;
	ssize_t rc;
	uint64_t sd_tmp;
	uint32_t crc, pc_tmp;
	int fd;

	(void)s;

	fd = open("synacor.save", O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0) {
		writes(STDERR_FILENO, "Failed to open synacor.save: ");
		if (errno == EEXIST)
			writes(STDERR_FILENO,
			    "file already exists; refusing to overwrite.");
		else
			write_errno(STDERR_FILENO);
		writes(STDERR_FILENO, "\n");
		return;
	}

	/*
	 * File format is:
	 *
	 * stack_depth:u64 || pc:u32 || crc32:u32 || memory[] || regs[] || stack[]
	 */
	sd_tmp = stack_depth;
	rc = writeall(fd, &sd_tmp, sizeof(sd_tmp));
	if (rc < 0)
		goto out;
	pc_tmp = pc;
	rc = writeall(fd, &pc_tmp, sizeof(pc_tmp));
	if (rc < 0)
		goto out;

	/* Checksum over: stack_depth || pc || memory || regs || stack */
	crc = crc32(0, (void *)&sd_tmp, sizeof(sd_tmp));
	crc = crc32(crc, (void *)&pc_tmp, sizeof(pc_tmp));
	crc = crc32(crc, (void *)memory, sizeof(memory));
	crc = crc32(crc, (void *)regs, sizeof(regs));
	crc = crc32(crc, (void *)stack, stack_depth * sizeof(*stack));
	rc = writeall(fd, &crc, sizeof(crc));
	if (rc < 0)
		goto out;

	rc = writeall(fd, memory, sizeof(memory));
	if (rc < 0)
		goto out;
	rc = writeall(fd, regs, sizeof(regs));
	if (rc < 0)
		goto out;
	rc = writeall(fd, stack, stack_depth * sizeof(*stack));
	if (rc < 0)
		goto out;

	writes(STDERR_FILENO, "Saved synacor.save.\n");

out:
	if (rc < 0) {
		writes(STDERR_FILENO, "Failed to write synacor.save: ");
		write_errno(STDERR_FILENO);
		writes(STDERR_FILENO, "\n");
	}
	close(fd);
	return;
}

void
usage(void)
{
	printf("usage: synacor-emu FLAGS [binaryimage]\n"
		"\n"
		"  FLAGS:\n"
		"    -c=OUTPUT.c   Recompile memory to C\n"
		"    -d            Trace output, disassembled\n"
		"    -D            Disassemble memory\n"
		"    -l=<N>        Limit execution to N instructions\n"
		"    -r            Restore save file binaryimage\n"
		"    -s=<N>        Set initial value of r7\n"
		"    -t=TRACEFILE  Emit instruction trace\n"
		"    -x            Trace output in hex\n");
	exit(1);
}

static size_t
freadall(void *buf, size_t sz, size_t nelm, FILE *f)
{
	size_t rd, idx;

	for (idx = 0; idx < nelm; idx += rd) {
		rd = fread((char *)buf + (idx * sz), sz, nelm - idx, f);
		if (rd == 0)
			break;
	}
	return (idx);
}

static void
loadrestore(FILE *romfile)
{
	const char *error;
	size_t rd;
	uint64_t sd;
	uint32_t crc, computed, pc_tmp;

	/*
	 * File format is:
	 *
	 * stack_depth:u64 || crc32:u32 || memory[] || regs[] || stack[]
	 */

	error = "short save file";
	rd = freadall(&sd, sizeof(sd), 1, romfile);
	if (rd != 1)
		goto out;
	rd = freadall(&pc_tmp, sizeof(pc_tmp), 1, romfile);
	if (rd != 1)
		goto out;
	pc = pc_tmp;
	rd = freadall(&crc, sizeof(crc), 1, romfile);
	if (rd != 1)
		goto out;
	rd = freadall(memory, sizeof(memory[0]), ARRAYLEN(memory), romfile);
	if (rd != ARRAYLEN(memory))
		goto out;
	rd = freadall(regs, sizeof(regs[0]), ARRAYLEN(regs), romfile);
	if (rd != ARRAYLEN(regs))
		goto out;

	stack_depth = stack_alloc = sd;
	stack = realloc(stack, stack_alloc * sizeof(*stack));
	ASSERT(stack != NULL, "realloc");

	rd = freadall(stack, sizeof(*stack), stack_depth, romfile);
	if (rd != stack_depth)
		goto out;

	error = "checksum error";
	computed = crc32(0, (void *)&sd, sizeof(sd));
	computed = crc32(computed, (void *)&pc_tmp, sizeof(pc_tmp));
	computed = crc32(computed, (void *)memory, sizeof(memory));
	computed = crc32(computed, (void *)regs, sizeof(regs));
	computed = crc32(computed, (void *)stack, stack_depth * sizeof(*stack));
	if (computed != crc)
		goto out;

	error = NULL;

out:
	if (error != NULL) {
		fprintf(stderr, "Couldn't read restore file: %s\n", error);
		exit(1);
	} else
		printf("Loaded save file successfully.\n");
}

static void
loadrom(FILE *romfile)
{
	size_t rd, idx;

	idx = 0;
	while (true) {
		rd = fread(&memory[idx], sizeof(memory[0]),
		    ARRAYLEN(memory) - idx, romfile);
		if (rd == 0)
			break;
		idx += rd;
	}
	printf("Loaded %zu words from image.\n", idx);
}

static void
write_c_header(void)
{
	size_t i;

	fprintf(coutfile,
		"#include <stdbool.h>\n"
		"#include <stdio.h>\n"
		"#include <stdlib.h>\n"
		"#include <stdint.h>\n");

	/* Machine state */
	fprintf(coutfile, "static uint16_t memory[%zu] = {\n\t",
	    ARRAYLEN(memory));
	for (i = 0; i < ARRAYLEN(memory); i++)
		fprintf(coutfile, "%u, ", (uns)memory[i]);
	fprintf(coutfile, "\n};\n");
	fprintf(coutfile, "static uintptr_t stack[1024 * 1024] = {\n\t");
	for (i = 0; i < stack_depth; i++)
		fprintf(coutfile, "%u, ", (uns)stack[i]);
	fprintf(coutfile, "\n};\n");

	fprintf(coutfile, "static size_t stack_depth = %zu;\n", stack_depth);
	fprintf(coutfile, "static bool halted;\n");

	fprintf(coutfile, "static uint16_t regs[%zu] = {\n\t", ARRAYLEN(regs));
	for (i = 0; i < ARRAYLEN(regs); i++)
		fprintf(coutfile, "%u, ", (uns)regs[i]);
	fprintf(coutfile, "\n};\n\n");

	/* Helpers */
	fprintf(coutfile,
		"static void\n"
		"push(uintptr_t val)\n"
		"{\n"
		"\tstack[stack_depth++] = val;\n"
		"}\n\n");
	fprintf(coutfile,
		"static uintptr_t\n"
		"pop(void)\n"
		"{\n"
		"\tif (stack_depth == 0)\n"
		"\t\tabort();\n"
		"\treturn (stack[--stack_depth]);\n"
		"}\n\n");
	fprintf(coutfile,
		"#define MOD(val) ((val) & 0x7fff)\n\n");

	/* Main body of code */
	fprintf(coutfile, "void\n");
	fprintf(coutfile, "main(void)\n");
	fprintf(coutfile, "{\n");
	fprintf(coutfile, "\n");
	fprintf(coutfile, "\tint tmp;\n");
	fprintf(coutfile, "\tuint16_t bogus = 0xffff;\n\n");

	orig_coutfile = coutfile;
	coutfile = open_memstream(&cout_stream, &cout_size);

	/* Resume PC: */
	fprintf(coutfile, "\tgoto l%u;\n\n", (uns)pc);
}

static void
write_c_footer(void)
{
	size_t i;

	fclose(coutfile);
	coutfile = orig_coutfile;

	fprintf(coutfile, "\tstatic void *jmptable[] = {\n");
	for (i = 0; i < ARRAYLEN(jmplabels); i++) {
		if (!jmplabels[i])
			continue;
		fprintf(coutfile, "\t\t[%zu] = &&l%zu,\n", i, i);
	}
	fprintf(coutfile, "\t};\n\n");

	fwrite(cout_stream, 1, cout_size, coutfile);

	fprintf(coutfile, "\tdo {} while (0);\n");

	fprintf(coutfile, "}\n");
}

int
main(int argc, char **argv)
{
	const char *romfname;
	FILE *romfile;
	uint16_t r7;
	bool restore;
	int opt;

	if (argc < 2)
		usage();

	restore = false;
	r7 = 0;
	while ((opt = getopt(argc, argv, "c:Ddl:rs:t:x")) != -1) {
		switch (opt) {
		case 'c':
			onlytranspile = true;
			coutfile = fopen(optarg, "wb");
			if (!coutfile) {
				printf("Failed to open output `%s'\n",
				    optarg);
				exit(1);
			}
			break;
		case 'd':
			if (tracehex) {
				printf("-d and -x are mutually exclusive.\n");
				exit(1);
			}
			tracedisas = true;
			break;
		case 'D':
			onlydisas = true;
			tracedisas = true;
			break;
		case 'l':
			insnlimit = atoll(optarg);
			break;
		case 'r':
			restore = true;
			break;
		case 's':
			r7 = atoll(optarg);
			break;
		case 't':
			tracefile = fopen(optarg, "wb");
			if (!tracefile) {
				printf("Failed to open tracefile `%s'\n",
				    optarg);
				exit(1);
			}
			break;
		case 'x':
			if (tracedisas) {
				printf("-d and -x are mutually exclusive.\n");
				exit(1);
			}
			tracehex = true;
			break;
		default:
			usage();
			break;
		}
	}

	if (optind >= argc)
		usage();

	romfname = argv[optind];

	romfile = fopen(romfname, "rb");
	ASSERT(romfile, "fopen");

	input_record = g_hash_table_new_full(NULL, NULL, NULL, free);
	ASSERT(input_record, "x");

	init();
	if (restore)
		loadrestore(romfile);
	else
		loadrom(romfile);
	fclose(romfile);

	if (onlytranspile) {
		write_c_header();
		pc = 0;
	} else if (onlydisas) {
		pc = 0;
		tracefile = stdout;
	} else
		regs[7] = r7;

	signal(SIGINT, ctrlc_handler);
	signal(SIGUSR1, save_handler);

	emulate();

	if (onlytranspile)
		write_c_footer();

	printf("Got HALT, stopped.\n");

	print_regs();
	print_ips();

	if (tracefile)
		fclose(tracefile);
	if (coutfile)
		fclose(coutfile);

	return 0;
}
#endif

void
emulate1(void)
{
	struct instr_decode_common idc;
	uint16_t instr;
	size_t i, j;

	pc_start = pc;
	instr_size = 1;

	instr = memory[pc];

	if (onlytranspile) {
		fprintf(coutfile, "l%u:\n", (uns)pc);
		jmplabels[pc] = true;
	}

	for (i = 0; i < ARRAYLEN(synacor_instr); i++)
		if (synacor_instr[i].icode == instr)
			break;

	if (i == ARRAYLEN(synacor_instr)) {
		if (!onlydisas && !onlytranspile)
			illins(instr);

		printf("%05u: illegal instruction %u", (uns)pc_start,
		    (uns)instr);
		if (instr >= 32 && instr < 128)
			printf(" '%c'", (char)instr);
		printf("\n");
		pc++;
		goto out;
	}

	memset(&idc, 0, sizeof(idc));
	idc.instr = instr;
	for (j = 0; j < synacor_instr[i].arguments; j++) {
		idc.args[j] = memory[pc + 1 + j];
		instr_size++;
	}

	if (onlytranspile) {
		if (i == ARRAYLEN(synacor_instr))
			fprintf(coutfile, "\tabort();\n");
		else
			synacor_instr[i].transpile(&idc);
	} else if (!onlydisas)
		synacor_instr[i].code(&idc);
	pc += instr_size;

	if (!replay_mode && tracefile) {
		ASSERT(instr_size > 0 && instr_size < 5, "instr_size: %u",
		    (uns)instr_size);

		if (onlydisas)
			fprintf(tracefile, "%05u: ", (uns)pc_start);
		for (j = 0; j < instr_size; j++) {
			uint16_t word;

			word = memory[pc_start + j];
			if (tracedisas) {
				if (j == 0)
					fprintf(tracefile, "%s",
					    synacor_instr[i].name);
				else
					printarg(tracefile, word,
					    j == (instr_size - 1));
			} else if (tracehex) {
				fprintf(tracefile, "%04x ", (uns)word);
			} else {
				size_t wr;
				wr = fwrite(&word, 2, 1, tracefile);
				ASSERT(wr == 1, "fwrite: %s", strerror(errno));
			}
		}
		if (tracehex || tracedisas)
			fprintf(tracefile, "\n");
	}

out:
	if (onlydisas || onlytranspile) {
		if (onlytranspile && pc > 6073)
			halted = true;
		else if (pc >= ARRAYLEN(memory))
			halted = true;
	} else
		ASSERT(pc < ARRAYLEN(memory), "overflow pc");

	insns++;
}

static void
dumpmem(uint16_t addr, unsigned len)
{

	for (unsigned i = 0; i < len / 2; i++) {
		printf("%04x", memory[addr + i]);
		if (i % 0x10 == 0xf)
			printf("\n");
	}
}

void
emulate(void)
{

#ifndef QUIET
	printf("Initial register state:\n");
	print_regs();
	printf("============================================\n\n");
#endif

	while (true) {
		if (ctrlc) {
			printf("Got ^C, stopping...\n");
			abort_nodump();
		}

#ifndef EMU_CHECK
		if (replay_mode && insns >= insnreplaylim) {
			replay_mode = false;
			insnreplaylim = 0;
		}

		if (replay_mode && insnreplaylim < insns) {
			init();
			pc = 0;
			continue;
		}
#endif

		if (halted)
			break;

		emulate1();

		if (halted)
			break;

		if (insnlimit && insns >= insnlimit) {
			printf("\nXXX Hit insn limit, halting XXX\n");
			break;
		}
	}
}

void __dead2
_unhandled(const char *f, unsigned l, uint16_t instr)
{

	printf("%s:%u: Instruction: %#04x @PC=%#06x is not implemented\n",
	    f, l, (unsigned)instr, (unsigned)pc_start);
	printf("Raw at PC: ");
	for (unsigned i = 0; i < 3; i++)
		printf("%04x", memory[pc_start + i]);
	printf("\n");
	abort_nodump();
}

void __dead2
_illins(const char *f, unsigned l, uint16_t instr)
{

	printf("%s:%u: ILLEGAL Instruction: %u @PC=%u\n",
	    f, l, (unsigned)instr, (unsigned)pc_start);
	printf("Raw at PC: ");
	for (unsigned i = 0; i < 3; i++)
		printf("%04x", memory[pc_start + i]);
	printf("\n");
	abort_nodump();
}

void __dead2
abort_nodump(void)
{

	print_regs();
	print_ips();

	exit(1);
}

#if 0
static void
printmemword(const char *pre, uint16_t addr)
{

	printf("%s", pre);
	printf("%02x", membyte(addr));
	printf("%02x", membyte(addr + 1));
}

static void
printreg(unsigned reg)
{

	// XXX
	printf("%04x  ", registers[reg]);
}
#endif

void
print_regs(void)
{

#if 0
	// XXX
	printf("pc  ");
	printreg(PC);
	printf("sp  ");
	printreg(SP);
	printf("sr  ");
	printreg(SR);
	printf("cg  ");
	printreg(CG);
	printf("\n");

	for (unsigned i = 4; i < 16; i += 4) {
		for (unsigned j = i; j < i + 4; j++) {
			printf("r%02u ", j);
			printreg(j);
		}
		printf("\n");
	}

	printf("instr:");
	for (unsigned i = 0; i < 4; i++)
		printmemword("  ", (pc_start & 0xfffe) + 2*i);
	printf("\nstack:");
	for (unsigned i = 0; i < 4; i++)
		printmemword("  ", (registers[SP] & 0xfffe) + 2*i);
	printf("\n      ");
	for (unsigned i = 4; i < 8; i++)
		printmemword("  ", (registers[SP] & 0xfffe) + 2*i);
	printf("\n");
#endif
}

uint64_t
now(void)
{
	struct timespec ts;
	int rc;

	rc = clock_gettime(CLOCK_REALTIME, &ts);
	ASSERT(rc == 0, "clock_gettime: %d:%s", errno, strerror(errno));

	return ((uint64_t)sec * ts.tv_sec + (ts.tv_nsec / 1000));
}

#ifndef EMU_CHECK
static void
ins_inprec(char *dat, size_t sz)
{
	struct inprec *new_inp = malloc(sizeof *new_inp + sz + 1);

	ASSERT(new_inp, "oom");

	new_inp->ir_insn = insns;
	new_inp->ir_len = sz + 1;
	memcpy(new_inp->ir_inp, dat, sz);
	new_inp->ir_inp[sz] = 0;

	g_hash_table_insert(input_record, ptr(insns), new_inp);
}

void
getsn(uint16_t addr, uint16_t bufsz)
{
	struct inprec *prev_inp;
	char *buf;

	ASSERT((size_t)addr + bufsz < 0xffff, "overflow");
	//memset(&memory[addr], 0, bufsz);

	if (bufsz <= 1)
		return;

	prev_inp = g_hash_table_lookup(input_record, ptr(insns));
	if (replay_mode)
		ASSERT(prev_inp, "input at insn:%ju not found!\n",
		    (uintmax_t)insns);

	if (prev_inp) {
		memcpy(&memory[addr], prev_inp->ir_inp, prev_inp->ir_len);
		return;
	}

	printf("Gets (':'-prefix for hex)> ");
	fflush(stdout);

	buf = malloc(2 * bufsz + 2);
	ASSERT(buf, "oom");
	buf[0] = 0;

	if (fgets(buf, 2 * bufsz + 2, stdin) == NULL)
		goto out;

	if (buf[0] != ':') {
		size_t len;

		len = strlen(buf);
		while (len > 0 &&
		    (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
			buf[len - 1] = '\0';
			len--;
		}

		strncpy((char*)&memory[addr], buf, bufsz);
		memory[addr + strlen(buf)] = 0;
		ins_inprec(buf, bufsz);
	} else {
		unsigned i;
		for (i = 0; i < bufsz - 1u; i++) {
			unsigned byte;

			if (buf[2*i+1] == 0 || buf[2*i+2] == 0) {
				memory[addr+i] = 0;
				break;
			}

			sscanf(&buf[2*i+1], "%02x", &byte);
			//printf("%02x", byte);
			memory[addr + i] = byte;
		}
		ins_inprec((void*)&memory[addr], i);
	}
out:
	free(buf);
}
#endif
