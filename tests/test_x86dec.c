/* Unit tests for x86dec.c -- the mini decoder the WHPX backend uses to
 * complete memory-access exits. Build natively on Linux (no guest needed):
 *   gcc -O2 -Wall -o /tmp/test_x86dec tests/test_x86dec.c x86dec.c
 * Exit status 0 = all pass. Prints "ALL PASS" or per-case FAIL lines. */
#include <stdio.h>
#include <string.h>
#include "../mini_kvm.h"  /* struct kvm_regs/kvm_sregs POD decls. */
#include "../x86dec.h"

static int failures = 0;

static void expect(int ok, const char *name) {
  if (!ok) { printf("FAIL %s\n", name); ++failures; }
}

int main(void) {
  struct kvm_regs r;
  struct kvm_sregs s;
  unsigned long v;
  unsigned sz;
  memset(&r, 0, sizeof r);
  memset(&s, 0, sizeof s);

  /* c7 04 2a 00  mov word [si], 0x2a  (16-bit addressing: rm=4 -> [si]) */
  {
    unsigned char inst[] = {0xc7, 0x04, 0x2a, 0x00};
    expect(x86dec_store(inst, sizeof inst, &r, &s, &v, &sz) == 0
           && v == 0x2a && sz == 2, "mov[si]imm16");
  }

  /* 89 44 02  mov [si+2], ax   -- ax=0x1234 */
  {
    unsigned char inst[] = {0x89, 0x44, 0x02};
    r.rax = 0x1234;
    expect(x86dec_store(inst, sizeof inst, &r, &s, &v, &sz) == 0
           && v == 0x1234 && sz == 2, "mov[si+2]ax");
  }

  /* 66 89 00  mov [bx+si], eax  -- eax=0xdeadbeef, opsize override */
  {
    unsigned char inst[] = {0x66, 0x89, 0x00};
    r.rax = 0xdeadbeef;
    expect(x86dec_store(inst, sizeof inst, &r, &s, &v, &sz) == 0
           && v == 0xdeadbeef && sz == 4, "mov[b xsi]eax");
  }

  /* 8c 4c 04  mov [si+4], cx  -- wait: 8c is mov r/m, SREG; modrm 4c = cx? */
  /* 8c /r stores segment reg; modrm reg field picks sreg. 8c 44 02: reg=0 -> es */
  {
    unsigned char inst[] = {0x8c, 0x44, 0x02};
    s.es.selector = 0xb800;
    expect(x86dec_store(inst, sizeof inst, &r, &s, &v, &sz) == 0
           && v == 0xb800 && sz == 2, "mov[si+2]es");
  }

  /* 8e 44 06  mov es, [si+6]  -- load into segment reg */
  {
    unsigned char inst[] = {0x8e, 0x44, 0x06};
    s.es.selector = 0;
    expect(x86dec_load(inst, sizeof inst, &r, &s, 0xc001, 2) == 0
           && s.es.selector == 0xc001, "moves[si+6]");
  }

  /* 8b 44 08  mov ax, [si+8]  -- load into gpr */
  {
    unsigned char inst[] = {0x8b, 0x44, 0x08};
    r.rax = 0xffff0000;
    expect(x86dec_load(inst, sizeof inst, &r, &s, 0x77aa, 2) == 0
           && (r.rax & 0xffff) == 0x77aa, "movax[si+8]");
  }

  /* 9c pushf as a store of rflags -- if covered; else size check only. */
  expect(x86dec_access_size((const unsigned char*)"\x8b\x44\x08", 3) == 2,
         "access_size_mov16");
  expect(x86dec_access_size((const unsigned char*)"\x66\x8b\x00", 3) == 4,
         "access_size_mov32");

  /* ff 36 xx xx  push [imm16] is a 2-byte store */
  {
    unsigned char inst[] = {0xff, 0x36, 0x10, 0x00};
    expect(x86dec_access_size(inst, sizeof inst) == 2, "access_size_pushm16");
  }

  if (failures) { printf("FAILURES: %d\n", failures); return 1; }
  printf("ALL PASS\n");
  return 0;
}
