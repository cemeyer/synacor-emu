#include <openssl/des.h>

#include "emu.h"
#include "instr.h"

void
instr_halt(struct instr_decode_common *idc __unused)
{

	halted = true;
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
