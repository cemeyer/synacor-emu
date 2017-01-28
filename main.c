#include <unistd.h>

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
FILE		*tracefile;
FILE		*outfile;

GHashTable	*input_record;			// insns -> inprec

// Could easily sort by popularity over time.
static struct instr_decode synacor_instr[] = {
	{ 0, 0, instr_halt },
	{ 1, 2, instr_ld },
	{ 2, 1, instr_push },
	{ 3, 1, instr_pop },
	{ 4, 3, instr_eq },
	{ 5, 3, instr_gt },
	{ 6, 1, instr_jmp },
	{ 7, 2, instr_jt },
	{ 8, 2, instr_jf },
	{ 9, 3, instr_add },
	{ 10, 3, instr_mult },
	{ 11, 3, instr_mod },
	{ 12, 3, instr_and },
	{ 13, 3, instr_or },
	{ 14, 2, instr_not },
	{ 15, 2, instr_rmem },
	{ 16, 2, instr_wmem },
	{ 17, 1, instr_call },
	{ 18, 0, instr_ret },
	{ 19, 1, instr_out },
	{ 21, 0, instr_nop },
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

void
init(void)
{

	pc = 0;
	insns = 0;
	halted = false;
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

void
usage(void)
{
	printf("usage: synacor-emu FLAGS [binaryimage]\n"
		"\n"
		"  FLAGS:\n"
		"    -l=<N>        Limit execution to N instructions\n"
		"    -t=TRACEFILE  Emit instruction trace\n"
		"    -x            Trace output in hex\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	size_t rd, idx;
	const char *romfname;
	FILE *romfile;
	int opt;

	if (argc < 2)
		usage();

	while ((opt = getopt(argc, argv, "gl:t:Tx")) != -1) {
		switch (opt) {
		case 'l':
			insnlimit = atoll(optarg);
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

	idx = 0;
	while (true) {
		rd = fread(&memory[idx], sizeof(memory[0]),
		    ARRAYLEN(memory) - idx, romfile);
		if (rd == 0)
			break;
		idx += rd;
	}
	printf("Loaded %zu words from image.\n", idx);

	fclose(romfile);
	signal(SIGINT, ctrlc_handler);

	pc = 0;
	emulate();

	printf("Got HALT, stopped.\n");

	print_regs();
	print_ips();

	if (tracefile)
		fclose(tracefile);

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

	for (i = 0; i < ARRAYLEN(synacor_instr); i++)
		if (synacor_instr[i].icode == instr)
			break;

	if (i == ARRAYLEN(synacor_instr))
		illins(instr);

	memset(&idc, 0, sizeof(idc));
	idc.instr = instr;
	for (j = 0; j < synacor_instr[i].arguments; j++) {
		idc.args[j] = memory[pc + 1 + j];
		instr_size++;
	}

	synacor_instr[i].code(&idc);
	pc += instr_size;

	if (!replay_mode && tracefile) {
		ASSERT(instr_size > 0 && instr_size < 3, "instr_size: %u",
		    (uns)instr_size);

		for (i = 0; i < instr_size; i++) {
			uint16_t word;

			word = memory[pc_start + i];
			if (tracehex)
				fprintf(tracefile, "%04x ", (uns)word);
			else {
				size_t wr;
				wr = fwrite(&word, 2, 1, tracefile);
				ASSERT(wr == 1, "fwrite: %s", strerror(errno));
			}
		}
		if (tracehex)
			fprintf(tracefile, "\n");
	}

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

	printf("%s:%u: ILLEGAL Instruction: %u @PC=%#06x\n",
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
