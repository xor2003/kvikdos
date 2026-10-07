#include "kvikdos.h"
#include "intrun.h"

/* KVM_EXIT_IO: guest port I/O. Services the timer-tick port, the A20/fast-reset
 * system control port and the CMOS/RTC index+data ports; other ports read as 0
 * (permissive) or fatal. Returns IA_NEXT to keep running, IA_FATAL on error. */
int io_dispatch(void) {
  char *p = (char*)hx.io.data;
  if (hx.io.port == 0x40 && hx.io.size == 1 && hx.io.direction == 0) {
    *p = (char)bda_ticks_now();  /* Low byte of the live PIT channel-0 count. */
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if ((hx.io.port == 0x60 || hx.io.port == 0x64) && hx.io.size == 1 && hx.io.direction == 0) {
    /* 8042 keyboard-controller reads for direct pollers (QuickBASIC's IDE
     * idle loop does `in al,0x64` / `in al,0x60` instead of int 16h).
     * 0x64 status: bit0 = output-buffer full (a scancode waits in 0x60).
     * 0x60 data: the make-scancode byte, 0 when the raw queue is empty. */
    const int isr = guest_int9_hooked(mem);
    (void)tty_drain(tty_state, mem, !isr);
    /* New key edge + guest ISR: deliver IRQ1 (int 9) so the ISR reads 0x60
     * and fills the BDA buffer itself — real 8042 semantics. */
    kbd_maybe_inject_irq();
    if (hx.io.port == 0x64) *p = tty_raw_pending() ? (char)1 : (char)0;
    else { const int s = tty_raw_pop(); *p = s < 0 ? (char)0 : (char)s; }
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    bda_update_ticks(mem);
    return IA_NEXT;
  } else if (hx.io.port == 0x61 && hx.io.size == 1) {
    /* PPI port B (XT keyboard acknowledge + speaker gate): the int 9 ISR
     * pulses bit7 high then low after reading 0x60. Latch it; reads return
     * the latch like a real 8255 (with speaker/timer bits as written). */
    if (hx.io.direction) port_0x61 = *p;
    else *p = port_0x61;
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if ((hx.io.port == 0x20 || hx.io.port == 0x21) && hx.io.size == 1) {
    /* Master PIC (8259A): 0x20 = command port — OCW2 EOI forms clear the
     * in-service bits our injected IRQ1 sets (0x20 non-specific EOI clears
     * the highest in-service bit; 0x60|n specific EOI clears bit n).
     * 0x21 = interrupt mask register (guest keyboard drivers unmask IRQ1
     * here when installing their int 9 ISR). */
    if (hx.io.port == 0x21) {
      if (hx.io.direction) pic_imr = (unsigned char)*p;
      else *p = (char)pic_imr;
    } else if (hx.io.direction) {
      const unsigned char v = (unsigned char)*p;
      if ((v & 0xf8) == 0x60) pic_isr &= (unsigned char)~(1u << (v & 7));
      else if (v & 0x20) {  /* Non-specific EOI: clear lowest set bit. */
        unsigned n;
        for (n = 0; n < 8; ++n)
          if (pic_isr & (1u << n)) { pic_isr &= (unsigned char)~(1u << n); break; }
      }
    }
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if ((hx.io.port == 0x3b4 || hx.io.port == 0x3d4) && hx.io.size == 1) {  /* MDA/CGA CRTC index (borrowed CRTC surface, per msdos_player). */
    if (hx.io.direction) port_crtc_index = *p & 0x1f;
    else *p = (char)port_crtc_index;
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if ((hx.io.port == 0x3b5 || hx.io.port == 0x3d5) && hx.io.size == 1) {  /* MDA/CGA CRTC data. */
    unsigned char *const m = (unsigned char*)mem;
    if (hx.io.direction) {
      crtc_regs[(unsigned char)port_crtc_index] = *p;
      if (port_crtc_index == 14 || port_crtc_index == 15) {
        /* Cursor regs written directly — mirror into the BDA so the console
         * renderer and int 10h/ah==3 readers follow (QuickBASIC moves the
         * cursor this way). */
        const unsigned off = ((unsigned)crtc_regs[14] << 8) | crtc_regs[15];
        unsigned char *const cur = m + 0x450 + (m[0x462] << 1);
        cur[0] = (unsigned char)(off % VID_COLS);
        cur[1] = (unsigned char)(off / VID_COLS);
      } else if (port_crtc_index == 10 || port_crtc_index == 11) {
        m[0x460] = crtc_regs[11];  /* Cursor end scanline. */
        m[0x461] = crtc_regs[10];  /* Cursor start (bit5 hides). */
      }
    } else {
      if (port_crtc_index == 14 || port_crtc_index == 15) {  /* Sync from BDA. */
        const unsigned char *const cur = m + 0x450 + (m[0x462] << 1);
        const unsigned off = (unsigned)cur[1] * VID_COLS + cur[0];
        crtc_regs[14] = (unsigned char)(off >> 8);
        crtc_regs[15] = (unsigned char)off;
      }
      *p = (char)crtc_regs[(unsigned char)port_crtc_index];
    }
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if ((hx.io.port == 0x3b8 || hx.io.port == 0x3d8 || hx.io.port == 0x3d9) && hx.io.size == 1) {
    /* MDA mode control / CGA mode control / CGA color select — store & read
     * back (real 0x3d8/0x3d9 are write-only; returning the latch is harmless). */
    if (hx.io.direction) {
      if (hx.io.port == 0x3b8) port_0x3b8 = *p;
      else if (hx.io.port == 0x3d8) port_0x3d8 = *p;
      else port_0x3d9 = *p;
    } else {
      *p = (char)(hx.io.port == 0x3b8 ? port_0x3b8 : hx.io.port == 0x3d8 ? port_0x3d8 : port_0x3d9);
    }
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if ((hx.io.port == 0x3ba || hx.io.port == 0x3da) && hx.io.size == 1 && hx.io.direction == 0) {
    /* MDA/CGA status register — the call QuickBASIC spins on before drawing.
     * bit0: horizontal retrace in progress; bit3: vertical retrace in
     * progress.  Derive both from host time: ~60 Hz frame, ~1.3 ms vblank,
     * ~31.5 kHz line rate for hsync. */
    struct timeval tv;
    unsigned long f;
    gettimeofday(&tv, NULL);
    f = (unsigned long)tv.tv_usec % 16667;
    *p = (char)((f >= 15333 ? 8 : 0) | ((tv.tv_usec & 31) >= 29 ? 1 : 0));
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if (hx.io.port == 0x92 && hx.io.size == 1) {  /* PS/2 system control port: A20 gate + fast reset. */
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    if (hx.io.direction == 0) {  /* IN: bit 1 = A20 enabled, bit 0 = fast reset. */
      *p = (port_0x92_a20 << 1);
    } else {  /* OUT: track A20 state; bit 0 (fast reset) ignored. */
      port_0x92_a20 = (*p >> 1) & 1;
    }
    return IA_NEXT;
  } else if (hx.io.port == 0x70 && hx.io.size == 1 && hx.io.direction == 1) {  /* CMOS index select. */
    port_0x70_index = *p;
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if (hx.io.port == 0x71 && hx.io.size == 1 && hx.io.direction == 0) {  /* CMOS data read. */
    const unsigned long ext_kb = emu->xmem_size >> 10;
    *p = 0;
    switch (port_0x70_index & 0x7f) {
     case 0x17: *p = ext_kb > 0xffffUL ? 0xff : (char)ext_kb; break;  /* Extended memory KB low (legacy). */
     case 0x18: *p = ext_kb > 0xffffUL ? 0xff : (char)(ext_kb >> 8); break;  /* Extended memory KB high. */
     case 0x30: *p = (char)(ext_kb & 0xff); break;  /* Extended memory KB low (AT). */
     case 0x31: *p = (char)((ext_kb >> 8) & 0xff); break;  /* Extended memory KB high (AT). */
     case 0x00: case 0x02: case 0x04: case 0x06: case 0x07: case 0x08: case 0x09: {  /* RTC registers, BCD. */
      time_t t = time(NULL);
      struct tm *tm = localtime(&t);
      if (tm) {
        int v = port_0x70_index == 0x00 ? tm->tm_sec : port_0x70_index == 0x02 ? tm->tm_min : port_0x70_index == 0x04 ? tm->tm_hour :
                port_0x70_index == 0x07 ? tm->tm_mday : port_0x70_index == 0x08 ? tm->tm_mon + 1 : tm->tm_year % 100;
        *p = (char)(((v / 10) << 4) | (v % 10));
      }
      break;
     }
     default: *p = 0; break;
    }
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if (hx.io.port == 0x71 && hx.io.size == 1 && hx.io.direction == 1) {  /* CMOS data write: ignore. */
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    return IA_NEXT;
  } else if (!emu_params->strict_mode) {
    if (hx.io.direction == 0) memset(p, 0, hx.io.size * hx.io.count);  /* IN: return 0. */
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    /* OUT: ignore in permissive mode. */
    return IA_NEXT;
  } else {
    trace_guest_io(hx.io.port, hx.io.direction, hx.io.size, hx.io.count, p);
    fprintf(stderr, "fatal: IO port: port=0x%02x data=%08x size=%d direction=%s\n", hx.io.port, *(const unsigned*)p, hx.io.size, hx.io.direction ? "out" : "in");
    return IA_FATAL;
  }
}

/* KVM_EXIT_MMIO: guest access to the read-only first page or unmapped memory.
 * Serves the BIOS ROM probes, BIOS data area, interrupt-vector writes and the
 * buffered set_int pairing; unknown accesses read 0xff (permissive) or fatal.
 * Returns IA_NEXT to keep running, IA_FATAL on error. */
int mmio_dispatch(void) {
  const unsigned mmio_len = hx.mmio.len;
  const unsigned addr = (unsigned)hx.mmio.phys_addr;
  char highmsg[2];
  /* CS:IP points to the instruction doing the memory operation (not after). */
  if (!(mmio_len == 1 || mmio_len == 2 || mmio_len == 4 || mmio_len == 8)) {
    highmsg[0] = '\0';
    goto bad_memory_access;
  }
  if (sizeof(hx.mmio.phys_addr) > 4 && hx.mmio.phys_addr >> (32 * (sizeof(hx.mmio.phys_addr) > 4))) {  /* Physical address is larger than 32 bits. */
    highmsg[0] = '+'; highmsg[1] = '\0';
    goto bad_memory_access;
  } else if (addr == 0xfffea && mmio_len == 1 && !hx.mmio.is_write && (sphinx_cmm_flags & 3) == 3) {
    /* SPHiNX C-- 1.04 compiler does this, just ignore. */
  } else if (addr - (ENV_PARA << 4) < (PROGRAM_MCB_PARA - 1 - ENV_PARA) << 4 && hx.mmio.is_write && mmio_len <= 16) {  /* Overwrites environment area. */
    /* Microsoft BASIC Professional Development System 7.10 linker pblink.exe. It overwrites length and program name with program name and args. */
    /* This emulation is a little bit slow (because of the ioctl(... KVM_RUN ...) overhead), but it's called only less than 75 times at startup. */
    memcpy((char*)mem + addr, hx.mmio.data, mmio_len);
  } else if (addr - 0xf0000U < 0x10000U) {  /* BIOS ROM area probes (F000:0000..FFFF:FFFF). */
    if (hx.mmio.is_write) {
      /* Ignore writes to ROM area. */
    } else {
      memset(hx.mmio.data, 0xff, mmio_len);  /* Typical ROM default value for unmodeled bytes. */
    }
  } else if (addr == 0xffffe && !hx.mmio.is_write && mmio_len == 1) {  /* BASIC programs compiled by Microsoft BASIC Professional Development System 7.10 compiler pbc.exe */
    hx.mmio.data[0] = 0xfc;  /* Machine ID is regular OC (0xfc). Same as default in src/ints/bios.cpp in DOSBox 0.74. */
  } else if (addr - 0xffff5U < 8U && !hx.mmio.is_write && mmio_len == 1) {  /* JWasm 2.11a jwasmr.exe */
    hx.mmio.data[0] = "01/01/92"[addr - 0xffff5U];  /* System BIOS date, same as default in src/ints/bios.cpp in DOSBox 0.74. */
  } else if (addr == 0xfff7e && !hx.mmio.is_write && mmio_len == 2) {  /* Reading the first MCB pointer in INVARS (see int 0x21 call with ah == 0x52). Used by Microsoft Macro Assembler 6.00B driver masm.exe. */
    *(unsigned short*)hx.mmio.data = PROGRAM_MCB_PARA;
  } else if (addr < 0x400 && hx.mmio.is_write && addr + mmio_len <= 0x400 && ((mmio_len == 2 && (addr & 1) == 0) || (mmio_len == 4 && (addr & 3) == 0))) {  /* Set interrupt vector directly (not via int 0x21 call with ah == 0x25). */
    /* Microsoft BASIC Professional Development System 7.10 compiler pbc.exe */
    const unsigned char set_int_num = addr >> 2;
    if (mmio_len == 2) {  /* There are subsequent sets (segment and offset parts), we buffer them, and call set_int only once. */
      if (addr & 2) {  /* Set segment part. */
        if ((ongoing_set_int & 0xff) == set_int_num && (ongoing_set_int & 0x100)) {
          *(unsigned*)hx.mmio.data = *(unsigned short*)hx.mmio.data << 16 | ongoing_set_int >> 16;
          goto do_set_int;
        } else {
          ongoing_set_int = 0x200 | set_int_num | *(unsigned short*)hx.mmio.data << 16;
          return IA_NEXT;  /* Prevent `ongoing_set_int = 0' below. */
        }
      } else {  /* Set offset part. */
        if ((ongoing_set_int & 0xff) == set_int_num && (ongoing_set_int & 0x200)) {
          *(unsigned*)hx.mmio.data = *(unsigned short*)hx.mmio.data | ongoing_set_int >> 16 << 16;
          goto do_set_int;
        } else {
          ongoing_set_int = 0x100 | set_int_num | *(unsigned short*)hx.mmio.data << 16;
          return IA_NEXT;  /* Prevent `ongoing_set_int = 0' below. */
        }
      }
    } else { do_set_int:
      if (set_int(set_int_num, *(unsigned*)hx.mmio.data, mem, had_get_ints, &tasm30_bitset)) return IA_FATAL;
    }
  } else if (addr == 0xa003e && mmio_len == 2 && !hx.mmio.is_write) {
    /* Microsoft Macro Assembler 6.00B driver masm.exe. */
    *(unsigned short*)hx.mmio.data = 0;
  } else if (addr == 0x501 && addr + mmio_len <= 0x504 && hx.mmio.is_write) {
    /* Microsoft Macro Assembler 1.00 m.exe only writes byte at 0x501. */
    memcpy((char*)mem + addr, hx.mmio.data, mmio_len);
  } else if (addr >= 0x400 && addr + mmio_len <= 0x500 && hx.mmio.is_write) {
    /* BIOS data area writes (keyboard flags, tick count, video state). */
    memcpy((char*)mem + addr, hx.mmio.data, mmio_len);
  } else if (addr >= 0x400 && addr + mmio_len <= 0x500 && !hx.mmio.is_write) {
    /* BIOS data area reads: served from the (read-only) page, but an
     * access may have been reported as MMIO anyway; return the data. */
    memcpy(hx.mmio.data, (char*)mem + addr, mmio_len);
  } else {
    highmsg[0] = '\0';
   bad_memory_access:
    if (!emu_params->strict_mode) {
      /* Reads of unmapped physical memory return 0xff (floating ISA bus),
       * so memory-sizing probes that write a signature and read it back
       * reliably detect the end of RAM.
       */
      if (!hx.mmio.is_write) memset(hx.mmio.data, 0xff, mmio_len);
      return IA_NEXT;
    }
    fprintf(stderr, "fatal: KVM memory access denied phys_addr=%08x%s value=%08x%08x size=%u is_write=%u\n", addr, highmsg, ((unsigned*)hx.mmio.data)[1], ((unsigned*)hx.mmio.data)[0], mmio_len, hx.mmio.is_write);
    return IA_FATAL;
  }
  ongoing_set_int = 0;  /* No set_int operation ongoing. */
  return IA_NEXT;  /* Just continue at following cs:ip. */
}
