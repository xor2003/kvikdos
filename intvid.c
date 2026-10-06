#include "kvikdos.h"
#include "intrun.h"

int int10_dispatch(void) {
/* Video output. */
        if (ah == 0x00 && !vid_active) {  /* Set video mode; a text mode starts the renderer. */
          const unsigned char mode = (unsigned char)regs.rax;
          if (mode <= 3 || mode == 7) {  /* 40x25 or 80x25 text modes. */
            ((unsigned char*)mem)[0x449] = mode;  /* BDA current video mode. */
            vid_enter();
            vid_fill(mem, 0, 0, VID_ROWS - 1, VID_COLS - 1, ' ', 7);  /* Clear screen. */
            *(unsigned short*)((char*)mem + 0x450) = 0;  /* Cursor to 0,0. */
            *(unsigned short*)((char*)mem + 0x460) = 0x0607;  /* Default DOS underline cursor shape. */
            vid_wrap_pend = 0;
            vid_render(mem);
            return IA_NEXT;
          }
        }
        if (vid_active) {  /* Text mode: operate directly on the framebuffer. */
          unsigned short * const cur = (unsigned short*)((char*)mem + 0x450);  /* BDA cursor, page 0. */
          unsigned char * const vram = (unsigned char*)mem + VID_BASE;
          if (ah == 0x02) {  /* Set cursor position. */
            if (((unsigned char*)&regs.rbx)[1] == 0) *cur = *(unsigned short*)&regs.rdx;
          } else if (ah == 0x03) {  /* Read cursor position and size. */
            *(unsigned short*)&regs.rcx = *(unsigned short*)((char*)mem + 0x460);
            *(unsigned short*)&regs.rdx = *cur;
          } else if (ah == 0x01) {  /* Set cursor shape. */
            *(unsigned short*)((char*)mem + 0x460) = *(unsigned short*)&regs.rcx;
          } else if (ah == 0x08) {  /* Read character and attribute at cursor. */
            const unsigned char curcol = (*cur & 0xff) < VID_COLS ? (*cur & 0xff) : (VID_COLS - 1);
            const unsigned cell = (((*cur >> 8) & 0xff) * VID_COLS + curcol) << 1;
            *(unsigned short*)&regs.rax = (vram[cell + 1] << 8) | vram[cell];
          } else if (ah == 0x09 || ah == 0x0a) {  /* Write character(+attribute) at cursor, CX times. */
            unsigned n = *(unsigned short*)&regs.rcx;
            const unsigned char curcol = (*cur & 0xff) < VID_COLS ? (*cur & 0xff) : (VID_COLS - 1);
            unsigned cell = (((*cur >> 8) & 0xff) * VID_COLS + curcol) << 1;
            const unsigned char at8 = (unsigned char)regs.rbx;
            while (n-- && cell < VID_BUFSZ) {
              vram[cell] = (unsigned char)regs.rax;
              if (ah == 0x09) vram[cell + 1] = at8;
              cell += 2;
            }
          } else if (ah == 0x06) {  /* Scroll window up / clear. */
            vid_scroll(mem, (unsigned char)regs.rax, (unsigned char)(regs.rbx >> 8), *(unsigned short*)&regs.rcx, *(unsigned short*)&regs.rdx, 0);
          } else if (ah == 0x07) {  /* Scroll window down. */
            vid_scroll(mem, (unsigned char)regs.rax, (unsigned char)(regs.rbx >> 8), *(unsigned short*)&regs.rcx, *(unsigned short*)&regs.rdx, 1);
          } else if (ah == 0x0e) {  /* Teletype output. */
            vid_putc(mem, (unsigned char)regs.rax);
          } else if (ah == 0x13) {  /* Write string ES:BP at row DH, column DL. */
            const unsigned char *str = (const unsigned char*)mem + (((unsigned)sregs.es.selector) << 4) + (*(unsigned short*)&regs.rbp);
            unsigned n = *(unsigned short*)&regs.rcx, i;
            const unsigned char strcol = ((unsigned char)regs.rdx) < VID_COLS ? (unsigned char)regs.rdx : (VID_COLS - 1);
            unsigned cell = ((((unsigned char*)&regs.rdx)[1] & 0xff) * VID_COLS + strcol) << 1;
            const unsigned char al = (unsigned char)regs.rax, at8 = (unsigned char)regs.rbx;
            for (i = 0; i < n && cell < VID_BUFSZ; ++i) {
              if (al & 2) { vram[cell] = str[i << 1]; vram[cell + 1] = str[(i << 1) + 1]; }
              else { vram[cell] = str[i]; if (!(al & 1)) vram[cell + 1] = at8; }
              cell += 2;
            }
            if (al & 1) *cur = (unsigned short)((((unsigned char*)&regs.rdx)[1] << 8) | (((unsigned char)regs.rdx + n) & 0xff));  /* Move cursor past the string. */
          } else if (ah == 0x0f) {  /* Get video state. */
            *(unsigned short*)&regs.rax = 80 << 8 | ((unsigned char*)mem)[0x449];
            ((unsigned char*)&regs.rbx)[1] = 0;
          } else if (ah == 0x05 || ah == 0x10 || ah == 0x11 || ah == 0x1a) {
            /* Display page, palette, font, combination code: ignored in minimal text mode. */
          } else if (ah == 0x12) {  /* Video subsystem configuration. */
            *(unsigned short*)&regs.rbx = 1 << 8;
            *(unsigned short*)&regs.rcx = 0;
            *(unsigned short*)&regs.rax = 80 << 8 | 3;
            ((unsigned char*)&regs.rbx)[1] = 0;
          }
          /* Any other int 0x10 call is a harmless no-op in text mode. */
          vid_render(mem);
          return IA_NEXT;
        }
        if (ah != 0x03 && ah != 0x02) video_write_step = 0;
        if (ah == 0x0e) {  /* Teletype output. */
          stdout_write_p = (const char*)&regs.rax; stdout_write_end = stdout_write_p + 1; emit_stdout(); return IA_NEXT;
        } else if (ah == 0x0f) {  /* Get video state. https://stanislavs.org/helppc/int_10-f.html */
          sphinx_cmm_flags |= 1;
          *(unsigned short*)&regs.rax = 80 << 8 | 3;  /* 80x25. */
          ((unsigned char*)&regs.rbx)[1] = 0;  /* BH := page (0). */
        } else if (ah == 0x08) {  /* Read character and attribute at cursor. */
          *(unsigned short*)&regs.rax = 0;  /* AH == attribute, AL == character. */
        } else if (ah == 0x09 ||  /* Write Character and Attribute at Cursor Position (it does not move the cursor). */
                   ah == 0x0a) {  /* Write Character Only at Current Cursor Position. */
          const unsigned char page = *(unsigned short*)&regs.rbx >> 8;  /* Page in BH. */
          if (page == 0) {
            ++video_write_step;
            video_byte_written = *(char*)&regs.rax;
          }
          /* TODO(pts): Record multiple characters (CX > 1). */
        } else if (ah == 0x03) {  /* Read Cursor Position and Size. */
          const unsigned char page = *(unsigned short*)&regs.rbx >> 8;  /* Page in BH. */
          *(unsigned short*)&regs.rcx = *(unsigned short*)((char*)mem + 0x460);
          *(unsigned short*)&regs.rdx = *((unsigned short*)((char*)mem + 0x450) + page);  /* DH := row (0..24); DL := column (0..79). Both 0 by default. */
          if (page == 0) {
            if (!is_stdout_write_cursor) {
              *(unsigned short*)&regs.rdx = *(unsigned short*)((char*)mem + 0x450) = 1;  /* Report nonzero column, so subsequent "\x08" in ah == 0x2 (Set cursor position) would work. */
              is_stdout_write_cursor = 1;
            }
            if (video_write_step == 1) {
              ++video_write_step;  /* = 2. */
            } else {
               video_write_step = 0;
            }
          }
        } else if (ah == 0x02) {  /* Set cursor position. */
          const unsigned char page = *(unsigned short*)&regs.rbx >> 8;  /* Page in BH. */
          unsigned short * const cursor_at_ptr = (unsigned short*)((char*)mem + 0x450) + page;
          if (page == 0) {
            is_stdout_write_cursor = 1;
            if (video_write_step == 2 && *cursor_at_ptr + 1 == *(unsigned short*)&regs.rdx) {  /* Move the cursor by 1 to the right. */
              /* Write byte to stdout if it was written by int 0x10 (ah == 0x09 or ah == 0x0a), then ah == 0x03, then ah == 0x02.
               * This is done by ASM32 1.1 assembler asm32.exe
               */
              stdout_write_p = &video_byte_written;
              stdout_write_end = stdout_write_p + 1; emit_stdout(); return IA_NEXT;
            }
            if (*(unsigned short*)&regs.rdx <= 0xff && *cursor_at_ptr <= 0xff) {
              if (*(unsigned short*)&regs.rdx == 0) {
                (void)!write(1, "\r", 1);  /* On Linux, move to the beginning of the line. */
              } else if (*(unsigned short*)&regs.rdx < *cursor_at_ptr) {
                unsigned count = *cursor_at_ptr - *(unsigned short*)&regs.rdx;
                const char * const backs = "\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08";  /* Works on TERM=xterm and TERM=linux. */
                while (count > 0x10) {
                  (void)!write(1, backs, 0x10);
                  count -= 0x10;
                }
                (void)!write(1, backs, count);
              }
            }
          }
          video_write_step = 0;
          if (page < 8) {
             *cursor_at_ptr = *(unsigned short*)&regs.rdx;  /* DH := row; DL := column. */
          }
        } else if (ah == 0x01) {  /* Set cursor type. */
          *(unsigned short*)((char*)mem + 0x460) = *(unsigned short*)&regs.rcx;
        } else if (ah == 0x12) {  /* Video subsystem configuration. https://stanislavs.org/helppc/int_10-12.html */
          const unsigned char bl = (unsigned char)regs.rbx;
          if (bl == 0x10) {  /* Get video configuration information. */
            sphinx_cmm_flags |= 2;
            *(unsigned short*)&regs.rbx = 1 << 8 | 0;  /* Mono, 64 KiB EGA memory. */
            *(unsigned short*)&regs.rcx = 0;  /* Feature bits and switch settings. */
          } else {
            fprintf(stderr, "fatal: unsupported subcall for video subsystem configuration: 0x%02x\n", bl);
            return IA_FATAL;
          }
          *(unsigned short*)&regs.rax = 80 << 8 | 3;  /* 80x25. */
          ((unsigned char*)&regs.rbx)[1] = 0;  /* BH := page (0). */
        } else {
          return IA_FATAL_INT;
        }
  return IA_NEXT;
}

