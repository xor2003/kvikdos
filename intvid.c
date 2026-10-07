#include "kvikdos.h"
#include "intrun.h"

/* int 10h video BIOS, CGA-scoped (per msdos_player's pcbios_int_10h_* and
 * ntvdm's handle_int_10): all 8 text pages, per-page cursors, blink toggle,
 * and the common probe calls IDEs make.  Cells live at 0xb8000 +
 * page*VID_PAGE_STRIDE; the renderer shows the active page (BDA 0x462). */
int int10_dispatch(void) {
  if (ah == 0x00) {  /* Set video mode; a text mode (re)starts the renderer. */
    const unsigned char mode = (unsigned char)regs.rax & 0x7f;
    if (mode <= 3 || mode == 7) {  /* 40x25/80x25 color, mode 7 = 80x25 mono. */
      vid_bda_init(mem, mode);
      vid_enter();
      if (!(regs.rax & 0x80)) {  /* bit7 of AL: keep screen contents. */
        unsigned p;
        for (p = 0; p < VID_PAGES; ++p)
          vid_fill(mem, p, 0, 0, VID_ROWS - 1, VID_COLS - 1, ' ', 7);
      }
      vid_wrap_pend = 0;
      vid_render(mem);
      return IA_NEXT;
    }
    /* Graphics modes are out of scope: ignore (the caller sees a text mode
     * still in effect, which is closer to CGA behavior than a crash). */
    if (vid_active) { vid_render(mem); return IA_NEXT; }
  }
  if (vid_active) {  /* Text mode: operate directly on the framebuffer. */
    const unsigned page = ((unsigned char*)mem)[0x462] < VID_PAGES
                          ? ((unsigned char*)mem)[0x462] : 0;
    if (ah == 0x02) {  /* Set cursor position of page BH. */
      const unsigned bp = ((unsigned char*)&regs.rbx)[1];
      if (bp < VID_PAGES)
        *(unsigned short*)((char*)mem + 0x450 + 2 * bp) = *(unsigned short*)&regs.rdx;
    } else if (ah == 0x03) {  /* Read cursor position and size of page BH. */
      const unsigned bp = ((unsigned char*)&regs.rbx)[1];
      *(unsigned short*)&regs.rcx = *(unsigned short*)((char*)mem + 0x460);
      *(unsigned short*)&regs.rdx = *(unsigned short*)((char*)mem + 0x450 + 2 * (bp < VID_PAGES ? bp : 0));
    } else if (ah == 0x01) {  /* Set cursor shape. */
      *(unsigned short*)((char*)mem + 0x460) = *(unsigned short*)&regs.rcx;
    } else if (ah == 0x05) {  /* Select active display page. */
      const unsigned char np = (unsigned char)regs.rax;
      if (np < VID_PAGES && np != page) {
        ((unsigned char*)mem)[0x462] = np;
        *(unsigned short*)((char*)mem + 0x44e) = np * VID_PAGE_STRIDE;
        vid_enter();  /* Repaint: the shadow tracks the newly shown page. */
      }
    } else if (ah == 0x08) {  /* Read character and attribute at cursor of page BH. */
      const unsigned bp = ((unsigned char*)&regs.rbx)[1];
      const unsigned short c0 = *(unsigned short*)((char*)mem + 0x450 + 2 * (bp < VID_PAGES ? bp : 0));
      const unsigned char curcol = (c0 & 0xff) < VID_COLS ? (c0 & 0xff) : (VID_COLS - 1);
      const unsigned cell = (((c0 >> 8) & 0xff) * VID_COLS + curcol) << 1;
      const unsigned char * const pv = vid_page(mem, bp);
      *(unsigned short*)&regs.rax = (pv[cell + 1] << 8) | pv[cell];
    } else if (ah == 0x09 || ah == 0x0a) {  /* Write character(+attribute) at cursor of page BH, CX times. */
      const unsigned bp = ((unsigned char*)&regs.rbx)[1];
      const unsigned short c0 = *(unsigned short*)((char*)mem + 0x450 + 2 * (bp < VID_PAGES ? bp : 0));
      const unsigned char curcol = (c0 & 0xff) < VID_COLS ? (c0 & 0xff) : (VID_COLS - 1);
      unsigned char * const pv = vid_page(mem, bp);
      unsigned cell = (((c0 >> 8) & 0xff) * VID_COLS + curcol) << 1;
      unsigned n = *(unsigned short*)&regs.rcx;
      const unsigned char at8 = (unsigned char)regs.rbx;
      while (n-- && cell < VID_BUFSZ) {
        pv[cell] = (unsigned char)regs.rax;
        if (ah == 0x09) pv[cell + 1] = at8;
        cell += 2;
      }
    } else if (ah == 0x06) {  /* Scroll window up / clear (active page). */
      vid_scroll(mem, page, (unsigned char)regs.rax, (unsigned char)(regs.rbx >> 8), *(unsigned short*)&regs.rcx, *(unsigned short*)&regs.rdx, 0);
    } else if (ah == 0x07) {  /* Scroll window down. */
      vid_scroll(mem, page, (unsigned char)regs.rax, (unsigned char)(regs.rbx >> 8), *(unsigned short*)&regs.rcx, *(unsigned short*)&regs.rdx, 1);
    } else if (ah == 0x0e) {  /* Teletype output on page BH. */
      vid_putc(mem, ((unsigned char*)&regs.rbx)[1], (unsigned char)regs.rax);
    } else if (ah == 0x13) {  /* Write string ES:BP at row DH, column DL, page BH. */
      const unsigned bp = ((unsigned char*)&regs.rbx)[1];
      unsigned char * const pv = vid_page(mem, bp);
      unsigned short * const pcur = (unsigned short*)((char*)mem + 0x450 + 2 * (bp < VID_PAGES ? bp : 0));
      const unsigned char *str = (const unsigned char*)mem + (((unsigned)sregs.es.selector) << 4) + (*(unsigned short*)&regs.rbp);
      unsigned n = *(unsigned short*)&regs.rcx, i;
      const unsigned char strcol = ((unsigned char)regs.rdx) < VID_COLS ? (unsigned char)regs.rdx : (VID_COLS - 1);
      unsigned cell = ((((unsigned char*)&regs.rdx)[1] & 0xff) * VID_COLS + strcol) << 1;
      const unsigned char al = (unsigned char)regs.rax, at8 = (unsigned char)regs.rbx;
      for (i = 0; i < n && cell < VID_BUFSZ; ++i) {
        if (al & 2) { pv[cell] = str[i << 1]; pv[cell + 1] = str[(i << 1) + 1]; }
        else { pv[cell] = str[i]; if (!(al & 1)) pv[cell + 1] = at8; }
        cell += 2;
      }
      if (al & 1) *pcur = (unsigned short)((((unsigned char*)&regs.rdx)[1] << 8) | (((unsigned char)regs.rdx + n) & 0xff));  /* Move cursor past the string. */
    } else if (ah == 0x0f) {  /* Get video state from the BDA. */
      *(unsigned short*)&regs.rax = (((unsigned char*)mem)[0x44a] << 8) | ((unsigned char*)mem)[0x449];
      ((unsigned char*)&regs.rbx)[1] = ((unsigned char*)mem)[0x462];
    } else if (ah == 0x10) {  /* Palette functions (CGA: only the blink toggle is real). */
      if ((unsigned char)regs.rax == 0x03) {  /* Toggle blink/intensity attribute bit7. */
        vid_blink = ((unsigned char)regs.rbx & 1) ? 1 : 0;
        vid_enter();  /* Repaint under the new interpretation. */
      }
      /* Single-palette-register calls (AL=0,1,2,7-9) are nops on CGA text. */
    } else if (ah == 0x11) {  /* Character generator (CGA: fixed ROM font). */
      if ((unsigned char)regs.rax == 0x30) {  /* Get font information. */
        /* BH selects the font; point ES:BP at the int 44h vector contents
         * like a real BIOS, report a 16-scanline cell and 25 rows. */
        const unsigned short *ivt = (const unsigned short*)mem;
        *(unsigned short*)&regs.rbp = ivt[2 * 0x44];
        SET_SREG(es, ivt[2 * 0x44 + 1]);
        *(unsigned short*)&regs.rcx = *(unsigned short*)((char*)mem + 0x485);
        *(unsigned char*)&regs.rdx = ((unsigned char*)mem)[0x484];
      }
      /* Font loads/reloads (AL=0..4, 0x10..0x14, 0x20..0x24) are nops: the
       * terminal owns the actual glyphs. */
    } else if (ah == 0x12) {  /* Video subsystem configuration. */
      const unsigned char bl = (unsigned char)regs.rbx;
      if (bl == 0x10) {  /* Get video configuration information. */
        sphinx_cmm_flags |= 2;
        *(unsigned short*)&regs.rbx = 0x0003;  /* BH=0 color, BL=3: 256 KiB video memory. */
        *(unsigned short*)&regs.rcx = 0;       /* Feature bits and switch settings. */
        ((unsigned char*)&regs.rax)[1] = 0x12;  /* Function supported. */
      } else if (bl == 0x30) {  /* Select scan lines for alphanumeric modes. */
        ((unsigned char*)&regs.rax)[1] = 0x12;  /* Supported; rows stay 25. */
      }
      /* Other BL subfunctions stay unimplemented (CGA has no such calls). */
    } else if (ah == 0x1a) {  /* Display combination code. */
      if ((unsigned char)regs.rax == 0) {
        ((unsigned char*)&regs.rax)[0] = 0x1a;  /* Function supported. */
        *(unsigned char*)&regs.rbx = 0x02;      /* BL=2: CGA color display (per ntvdm; a 0 answer makes QuickC misdetect EGA). */
        ((unsigned char*)&regs.rbx)[1] = 0;     /* No alternate display. */
      }
    } else if (ah == 0x1b) {  /* Functionality/state information: unsupported. */
      *(unsigned char*)&regs.rax = 0;
    } else if (ah == 0x04) {  /* Read light pen position: none. */
      *(unsigned char*)&regs.rax = 0;  /* AH=0: not triggered. */
    } else if (ah == 0x0b) {  /* Set color palette/border: nop in text mode. */
    } else if (ah == 0xef) {  /* Hercules/etc. probe: DL=0xff = not present. */
      *(unsigned char*)&regs.rdx = 0xff;
    } else if (ah == 0xfa) {  /* Mouse driver RIL probe: BX=0 = not present. */
      *(unsigned short*)&regs.rbx = 0;
    } else if (ah == 0xff) {  /* TopView/refresh: flush the renderer now. */
      vid_render(mem);
    }
    /* Any other int 0x10 call (scroll-clear aliases, palette dumps, save/
     * restore state) is a harmless no-op in text mode. */
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
