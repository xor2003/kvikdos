#include "kvikdos.h"
#include "intrun.h"

/* DOS console input uses the same BIOS queue as int 16h and IRQ1. Polling
 * the host fd separately loses keys already drained into the BDA. */
static int console_key(int wait, unsigned short *key) {
  const unsigned short saved_ax = (unsigned short)regs.rax;
  const unsigned char saved_ah = ah;
  int action;
  ah = wait ? 0 : 1;
  action = int16_dispatch();
  *key = (unsigned short)regs.rax;
  *(unsigned short*)&regs.rax = saved_ax;
  ah = saved_ah;
  return action;
}

/* DOS int 21h services: con group. Returns an IA_* action. */
int i21_con(void) {
  if (ah == 0x06) {
  /* Direct console I/O. */
           func_0x06:
            if ((unsigned char)regs.rdx != 0xff) {  /* Output. */
              stdout_write_p = (const char*)&regs.rdx;
              stdout_write_end = stdout_write_p + 1; emit_stdout(); return IA_NEXT;
            } else {  /* Input. */
              unsigned short result_ax;
              int action;
              (void)console_key(0, &result_ax);
              if (!(regs.rflags & (1u << 6))) {  /* ZF=0: a key is available. */
                action = console_key(1, &result_ax);
                if (action != IA_NEXT) return action;
                *(unsigned char*)&regs.rax = (unsigned char)result_ax;
              } else *(unsigned char*)&regs.rax = 0;  /* Nonblocking, no key. */
            }
  }   else if (ah == 0x07 || ah == 0x08) {
  /* Wait for console input without echo. */
            unsigned short result_ax;
            int action;
           func_0x07_or_0x08:
            /* We should check Ctrl-<Break> with ah == 0x08, but the
             * difference doesn't matter, bcause in kvikdos Ctrl-<Break> is
             * never delivered.
             */
            action = console_key(1, &result_ax);
            if (action != IA_NEXT) return action;
            *(unsigned char*)&regs.rax = (unsigned char)result_ax;  /* Return only the keycode. */
  }   else if (ah == 0x02) {
  /* Display output. */
            stdout_write_p = (const char*)&regs.rdx;
            stdout_write_end = stdout_write_p + 1; emit_stdout(); return IA_NEXT;
  }   else if (ah == 0x04) {
  /* Output to STDAUX. */
            const char c = (unsigned char)regs.rdx;
            (void)!write(2, &c, 1);  /* Emulate STDAUX with stderr. */
  }   else if (ah == 0x05) {
  /* Output to STDPRN. */
            const char c = (unsigned char)regs.rdx;
            (void)!write(1, &c, 1);  /* Emulate STDPRN with stdout. */
  }   else if (ah == 0x09) {
  /* Print string. */
            unsigned short dx = *(unsigned short*)&regs.rdx, dx0 = dx;
            const char *p = (char*)mem + ((unsigned)sregs.ds.selector << 4), *p0 = p;
            for (;;) {
              if (p[dx] == '$') break;
              ++dx;
              if (dx == 0) {  /* End of segment: the `$' may live in the next one, like on real DOS. */
                p += 0x10000;
                if (p - (const char*)mem >= GUEST_MEM_LIMIT) {
                  fprintf(stderr, "fatal: unterminated $-terminated string in print\n");
                  exit(252);
                }
              }
            }
            stdout_write_p = p0 + dx0;
            stdout_write_end = p + dx;
            emit_stdout(); return IA_NEXT;
  }   else if (ah == 0x0b) {
  /* Check input status. */
            unsigned short result_ax;
            (void)console_key(0, &result_ax);
            *(unsigned char*)&regs.rax = (*(unsigned short*)&regs.rflags & (1 << 6)) ? 0 : 0xff;
            /* If we detect Ctrl-<Break>, we should run `int 0x23'. */
  }   else if (ah == 0x0a) {
  /* Buffered keyboard input. */
           func_0x0a: {
              char *p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
              unsigned size = *(unsigned char*)p++;
              char *q = ++p, *q_end = q + size;
              for (; q != q_end; ++q) {
                const int got = read(0, q, 1);  /* STDIN_FILENO. */
                char c;
                if (got <= 0) break;
                if ((c = *q) == '\n' || c == '\r') {  /* '\n' in cooked mode, '\r' in raw text mode. */
                  *q++ = '\r'; break;
                } else if (c == '\0') {
                  *q = '\1';  /* Never report control keys: '\0' + another character. */
                }
              }
              p[-1] = q - p;  /* Return number of bytes read. */
            }
  }   else if (ah == 0x01) {
  /* Keyboard input with echo. process_key puts the tty in raw mode for the
   * duration of the read, so each keypress arrives at once — a canonical
   * read(0) would hold input until <Enter>, which is not DOS behavior. */
            unsigned short result_ax;
            char c;
            int action;
           func_0x01:
            action = console_key(1, &result_ax);
            if (action != IA_NEXT) return action;
            c = (char)result_ax;
            if (c == '\0') c = '\1';  /* Never report control keys: '\0' + another character. */
            *(unsigned char*)&regs.rax = (unsigned char)c;
            stdout_write_p = (const char*)&c;  /* DOS AH=01 echoes the character. */
            stdout_write_end = stdout_write_p + 1;
            emit_stdout();
            return IA_NEXT;
  }   else if (ah == 0x0c) {
  /* Clear keyboard buffer and invoke keyboard function. */
            const unsigned char al = (unsigned char)regs.rax;
            if (al == 0x01) {
              goto func_0x01;
            } else if (al == 0x06) {
              goto func_0x06;
            } else if (al == 0x07 || al == 0x08) {
              goto func_0x07_or_0x08;
            } else if (al == 0x0a) {
              goto func_0x0a;
            } else {
              *(unsigned char*)&regs.rax = 0;  /* DOSBox 0.74-4 does this. What should we do? */
            }
  } 
  return IA_NEXT;
}
