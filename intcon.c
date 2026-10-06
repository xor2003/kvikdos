#include "kvikdos.h"
#include "intrun.h"

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
              process_key(tty_state, 1, &result_ax, (unsigned short*)&regs.rflags);  /* Check availability without reading. */
              if (*(unsigned short*)&regs.rflags & (1 << 6)) {  /* ZF==1. */
                process_key(tty_state, 0, &result_ax, (unsigned short*)&regs.rflags);  /* Read. */
                *(unsigned char*)&regs.rax = (unsigned char)result_ax;  /* Return only the keycode. */
              }
            }
  }   else if (ah == 0x07 || ah == 0x08) {
  /* Wait for console input without echo. */
            unsigned short result_ax;
           func_0x07_or_0x08:
            /* We should check Ctrl-<Break> with ah == 0x08, but the
             * difference doesn't matter, bcause in kvikdos Ctrl-<Break> is
             * never delivered.
             */
            process_key(tty_state, 0, &result_ax, (unsigned short*)&regs.rflags);  /* Read. */
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
            *(unsigned char*)&regs.rax = 0;  /* No input ready. 0xff would be input. */
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
  /* Keyboard input with echo. */
            char c;
            int got;
           func_0x01:
            if ((got = read(0, &c, 1)) <= 0) {  /* STDIN_FILENO. */
              c = 0x1a;  /* Ctrl-<Z>, EOF. */
            } else if (c == '\0') {
              c = '\1';  /* Never report control keys: '\0' + another character. */
            }
            *(unsigned char*)&regs.rax = c;
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
