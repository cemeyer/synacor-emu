#ifndef	__INSTR_H__
#define	__INSTR_H__

struct instr_decode_common {
	uint16_t	instr;
	uint16_t	args[3];
};

struct instr_decode {
	uint16_t	  icode;
	uint16_t	  arguments;

	void		(*code)(struct instr_decode_common *);
};


void instr_add(struct instr_decode_common *);
void instr_eq(struct instr_decode_common *);
void instr_gt(struct instr_decode_common *);
void instr_halt(struct instr_decode_common *);
void instr_jmp(struct instr_decode_common *);
void instr_jf(struct instr_decode_common *);
void instr_jt(struct instr_decode_common *);
void instr_ld(struct instr_decode_common *);
void instr_nop(struct instr_decode_common *);
void instr_out(struct instr_decode_common *);
void instr_pop(struct instr_decode_common *);
void instr_push(struct instr_decode_common *);

void instr_unimp(struct instr_decode_common *);

#endif
