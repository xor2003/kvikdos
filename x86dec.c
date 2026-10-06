#include "kvikdos.h"
#include "x86dec.h"

/* Register-index helpers. modrm reg order: ax cx dx bx sp bp si di. */

static unsigned long get_gpr(const struct kvm_regs *regs, unsigned idx, unsigned size) {
  unsigned long v;
  switch (idx & 7) {
   case 0: v = regs->rax; break;
   case 1: v = regs->rcx; break;
   case 2: v = regs->rdx; break;
   case 3: v = regs->rbx; break;
   case 4: v = regs->rsp; break;
   case 5: v = regs->rbp; break;
   case 6: v = regs->rsi; break;
   default: v = regs->rdi; break;
  }
  if (size == 1 && (idx & 4)) return (v >> 8) & 0xff;  /* ah ch dh bh */
  return size == 1 ? v & 0xff : size == 2 ? v & 0xffff : v;
}

static void set_gpr(struct kvm_regs *regs, unsigned idx, unsigned size, unsigned long v) {
  __u64 *p;
  switch (idx & 7) {
   case 0: p = &regs->rax; break;
   case 1: p = &regs->rcx; break;
   case 2: p = &regs->rdx; break;
   case 3: p = &regs->rbx; break;
   case 4: p = &regs->rsp; break;
   case 5: p = &regs->rbp; break;
   case 6: p = &regs->rsi; break;
   default: p = &regs->rdi; break;
  }
  if (size == 1) {
    if (idx & 4) *p = (*p & ~0xff00UL) | ((v & 0xff) << 8);  /* ah ch dh bh */
    else *p = (*p & ~0xffUL) | (v & 0xff);
  } else if (size == 2) {
    *p = (*p & ~0xffffUL) | (v & 0xffff);
  } else {
    *p = v & 0xffffffffUL;
  }
}

static unsigned short get_sreg_sel(const struct kvm_sregs *sregs, unsigned idx) {
  switch (idx) {
   case 0: return sregs->es.selector;
   case 1: return sregs->cs.selector;
   case 2: return sregs->ss.selector;
   case 3: return sregs->ds.selector;
   case 4: return sregs->fs.selector;
   default: return sregs->gs.selector;
  }
}

static void set_sreg_sel(struct kvm_sregs *sregs, unsigned idx, unsigned short sel) {
  struct kvm_segment *seg = idx == 0 ? &sregs->es : idx == 1 ? &sregs->cs :
      idx == 2 ? &sregs->ss : idx == 3 ? &sregs->ds : idx == 4 ? &sregs->fs : &sregs->gs;
  seg->selector = sel;
  seg->base = (unsigned)sel << 4;
}

/* Skip instruction prefixes; returns opsize in bytes (2 or 4) via *opsize,
 * 32-bit addressing flag via *addr32, and positions at the first opcode byte.
 * Default addressing is 16-bit: the trap regions (page 0, env, ROM probes)
 * are all written by real-mode code. */
static const unsigned char *skip_prefixes(const unsigned char *inst, const unsigned char *end, unsigned *opsize, int *addr32) {
  *opsize = 2;
  *addr32 = 0;
  while (inst < end) {
    const unsigned char c = *inst;
    if (c == 0x66) { *opsize = 4; ++inst; }
    else if (c == 0x67) { *addr32 = 1; ++inst; }
    else if (c == 0xf0 || c == 0xf2 || c == 0xf3 ||
             c == 0x26 || c == 0x2e || c == 0x36 || c == 0x3e || c == 0x64 || c == 0x65) { ++inst; }
    else break;
  }
  return inst;
}

/* Bytes the modrm + displacement occupy. `m` points at the modrm byte. */
static unsigned modrm_len(const unsigned char *m, int addr32) {
  const unsigned mod = *m >> 6, rm = *m & 7;
  unsigned len = 1;
  if (addr32) {
    if (rm == 4) len += 1;  /* SIB byte. */
    if (mod == 0 && rm == 5) len += 4;
    else if (mod == 1) len += 1;
    else if (mod == 2) len += 4;
  } else {
    if (mod == 0 && rm == 6) len += 2;  /* [disp16]. */
    else if (mod == 1) len += 1;
    else if (mod == 2) len += 2;
  }
  return len;
}

/* Read a little-endian immediate of `size` bytes (1/2/4). */
static unsigned long get_imm(const unsigned char *p, unsigned size) {
  return size == 1 ? p[0] : size == 2 ? (unsigned)p[0] | ((unsigned)p[1] << 8) :
         (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int x86dec_store(const unsigned char *inst, unsigned instlen,
                 const struct kvm_regs *regs, const struct kvm_sregs *sregs,
                 unsigned long *value, unsigned *size) {
  const unsigned char * const end = inst + instlen;
  unsigned opsize;
  int addr32;
  const unsigned char *op;
  if (!instlen) return -1;
  op = skip_prefixes(inst, end, &opsize, &addr32);
  if (op >= end) return -1;
  switch (*op) {
   case 0x88: case 0x89: case 0x8c: {  /* mov r/m, reg (or sreg for 8c) */
     const unsigned char *m = op + 1;
     unsigned reg;
     if (m >= end || (*m >> 6) == 3) return -1;  /* register direct: not a store */
     reg = (*m >> 3) & 7;
     *size = *op == 0x88 ? 1 : *op == 0x8c ? 2 : opsize;
     if (*op == 0x8c) *value = get_sreg_sel(sregs, reg);
     else *value = get_gpr(regs, reg, *size);
     return 0;
   }
   case 0xa2: case 0xa3:  /* mov moffs, al/ax/eax */
     *size = *op == 0xa2 ? 1 : opsize;
     *value = get_gpr(regs, 0, *size);
     return 0;
   case 0xc6: case 0xc7: {  /* mov r/m, imm */
     const unsigned char *m = op + 1;
     unsigned ml;
     if (m >= end || (*m >> 6) == 3) return -1;
     ml = modrm_len(m, addr32);
     *size = *op == 0xc6 ? 1 : opsize;
     if (m + ml + *size > end) return -1;
     *value = get_imm(m + ml, *size);
     return 0;
   }
   case 0xaa: case 0xab:  /* stos */
     *size = *op == 0xaa ? 1 : opsize;
     *value = get_gpr(regs, 0, *size);
     return 0;
   case 0x50: case 0x51: case 0x52: case 0x53:  /* push reg */
   case 0x54: case 0x55: case 0x56: case 0x57:
     *size = opsize;
     *value = get_gpr(regs, *op & 7, opsize);
     return 0;
   case 0x06: case 0x0e: case 0x16: case 0x1e:  /* push sreg */
     *size = 2;
     *value = get_sreg_sel(sregs, *op == 0x06 ? 0 : *op == 0x0e ? 1 : *op == 0x16 ? 2 : 3);
     return 0;
   case 0x68:  /* push imm16/32 */
     *size = opsize;
     if (op + 1 + opsize > end) return -1;
     *value = get_imm(op + 1, opsize);
     return 0;
   case 0x6a:  /* push imm8 */
     *size = opsize;
     if (op + 2 > end) return -1;
     *value = (unsigned long)(long)(signed char)op[1];  /* sign-extended */
     return 0;
   case 0x9c:  /* pushf */
     *size = opsize;
     *value = regs->rflags & (opsize == 2 ? 0xffffUL : 0xffffffffUL);
     return 0;
   case 0xff: {  /* push r/m (only register-direct form decodes the value) */
     const unsigned char *m = op + 1;
     if (m >= end || ((*m >> 3) & 7) != 6 || (*m >> 6) != 3) return -1;
     *size = opsize;
     *value = get_gpr(regs, *m & 7, opsize);
     return 0;
   }
   default: return -1;
  }
}

int x86dec_load(const unsigned char *inst, unsigned instlen,
                struct kvm_regs *regs, struct kvm_sregs *sregs,
                unsigned long fill, unsigned fill_len) {
  const unsigned char * const end = inst + instlen;
  unsigned opsize;
  int addr32;
  const unsigned char *op;
  (void)fill_len;
  if (!instlen) return -1;
  op = skip_prefixes(inst, end, &opsize, &addr32);
  if (op >= end) return -1;
  switch (*op) {
   case 0x8a: case 0x8b: case 0x8e: {  /* mov reg/sreg, r/m */
     const unsigned char *m = op + 1;
     unsigned reg;
     if (m >= end || (*m >> 6) == 3) return -1;
     reg = (*m >> 3) & 7;
     if (*op == 0x8e) { set_sreg_sel(sregs, reg, (unsigned short)fill); }
     else set_gpr(regs, reg, *op == 0x8a ? 1 : opsize, fill);
     return 0;
   }
   case 0xa0: case 0xa1:  /* mov al/ax/eax, moffs */
     set_gpr(regs, 0, *op == 0xa0 ? 1 : opsize, fill);
     return 0;
   case 0x0f: {  /* two-byte: movzx/movsx, push sreg ext */
     const unsigned char *m = op + 1, *mm;
     unsigned reg;
     if (m >= end) return -1;
     if (*m == 0xb6 || *m == 0xb7 || *m == 0xbe || *m == 0xbf) {
       mm = m + 1;
       if (mm >= end || (*mm >> 6) == 3) return -1;
       reg = (*mm >> 3) & 7;
       set_gpr(regs, reg, opsize, fill);
       return 0;
     }
     return -1;
   }
   case 0x58: case 0x59: case 0x5a: case 0x5b:  /* pop reg */
   case 0x5c: case 0x5d: case 0x5e: case 0x5f:
     set_gpr(regs, *op & 7, opsize, fill);
     return 0;
   case 0x07: case 0x17: case 0x1f:  /* pop sreg */
     set_sreg_sel(sregs, *op == 0x07 ? 0 : *op == 0x17 ? 2 : 4, (unsigned short)fill);
     return 0;
   case 0x9d:  /* popf */
     regs->rflags = (regs->rflags & ~0xffffUL) | (fill & 0xffff);
     return 0;
   default: return -1;
  }
}

int x86dec_access_size(const unsigned char *inst, unsigned instlen) {
  const unsigned char * const end = inst + instlen;
  unsigned opsize;
  int addr32;
  const unsigned char *op;
  if (!instlen) return -1;
  op = skip_prefixes(inst, end, &opsize, &addr32);
  if (op >= end) return -1;
  switch (*op) {
   case 0x88: case 0x8a: case 0xa0: case 0xa2: case 0xaa: case 0xc6:
     return 1;
   case 0x8c: case 0x8e: case 0x06: case 0x0e: case 0x16: case 0x1e:
   case 0x07: case 0x17: case 0x1f:
     return 2;
   case 0x89: case 0x8b: case 0xa1: case 0xa3: case 0xab: case 0xc7:
   case 0x50: case 0x51: case 0x52: case 0x53:
   case 0x54: case 0x55: case 0x56: case 0x57:
   case 0x58: case 0x59: case 0x5a: case 0x5b:
   case 0x5c: case 0x5d: case 0x5e: case 0x5f:
   case 0x68: case 0x6a: case 0x9c: case 0x9d: case 0xff:
     return (int)opsize;
   case 0x0f:
     if (op + 1 < end && (op[1] == 0xb6 || op[1] == 0xbe)) return 1;
     if (op + 1 < end && (op[1] == 0xb7 || op[1] == 0xbf)) return 2;
     return -1;
   default: return -1;
  }
}
