#include "kvikdos.h"
#include "intrun.h"

/* KVM_EXIT_IO: guest port I/O. Services the timer-tick port, the A20/fast-reset
 * system control port and the CMOS/RTC index+data ports; other ports read as 0
 * (permissive) or fatal. Returns IA_NEXT to keep running, IA_FATAL on error. */
int io_dispatch(void) {
  char *p = (char*)run + run->io.data_offset;
  if (run->io.port == 0x40 && run->io.size == 1 && run->io.direction == 0) {
    *p = port_0x40_tick++;  /* Simulate some timer ticks. */
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    return IA_NEXT;
  } else if (run->io.port == 0x92 && run->io.size == 1) {  /* PS/2 system control port: A20 gate + fast reset. */
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    if (run->io.direction == 0) {  /* IN: bit 1 = A20 enabled, bit 0 = fast reset. */
      *p = (port_0x92_a20 << 1);
    } else {  /* OUT: track A20 state; bit 0 (fast reset) ignored. */
      port_0x92_a20 = (*p >> 1) & 1;
    }
    return IA_NEXT;
  } else if (run->io.port == 0x70 && run->io.size == 1 && run->io.direction == 1) {  /* CMOS index select. */
    port_0x70_index = *p;
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    return IA_NEXT;
  } else if (run->io.port == 0x71 && run->io.size == 1 && run->io.direction == 0) {  /* CMOS data read. */
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
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    return IA_NEXT;
  } else if (run->io.port == 0x71 && run->io.size == 1 && run->io.direction == 1) {  /* CMOS data write: ignore. */
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    return IA_NEXT;
  } else if (!emu_params->strict_mode) {
    if (run->io.direction == 0) memset(p, 0, run->io.size * run->io.count);  /* IN: return 0. */
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    /* OUT: ignore in permissive mode. */
    return IA_NEXT;
  } else {
    trace_guest_io(run->io.port, run->io.direction, run->io.size, run->io.count, p);
    fprintf(stderr, "fatal: IO port: port=0x%02x data=%08x size=%d direction=%s\n", run->io.port, *(const unsigned*)p, run->io.size, run->io.direction ? "out" : "in");
    return IA_FATAL;
  }
}

/* KVM_EXIT_MMIO: guest access to the read-only first page or unmapped memory.
 * Serves the BIOS ROM probes, BIOS data area, interrupt-vector writes and the
 * buffered set_int pairing; unknown accesses read 0xff (permissive) or fatal.
 * Returns IA_NEXT to keep running, IA_FATAL on error. */
int mmio_dispatch(void) {
  const unsigned mmio_len = run->mmio.len;
  const unsigned addr = (unsigned)run->mmio.phys_addr;
  char highmsg[2];
  /* CS:IP points to the instruction doing the memory operation (not after). */
  if (!(mmio_len == 1 || mmio_len == 2 || mmio_len == 4 || mmio_len == 8)) {
    highmsg[0] = '\0';
    goto bad_memory_access;
  }
  if (sizeof(run->mmio.phys_addr) > 4 && run->mmio.phys_addr >> (32 * (sizeof(run->mmio.phys_addr) > 4))) {  /* Physical address is larger than 32 bits. */
    highmsg[0] = '+'; highmsg[1] = '\0';
    goto bad_memory_access;
  } else if (addr == 0xfffea && mmio_len == 1 && !run->mmio.is_write && (sphinx_cmm_flags & 3) == 3) {
    /* SPHiNX C-- 1.04 compiler does this, just ignore. */
  } else if (addr - (ENV_PARA << 4) < (PROGRAM_MCB_PARA - 1 - ENV_PARA) << 4 && run->mmio.is_write && mmio_len <= 16) {  /* Overwrites environment area. */
    /* Microsoft BASIC Professional Development System 7.10 linker pblink.exe. It overwrites length and program name with program name and args. */
    /* This emulation is a little bit slow (because of the ioctl(... KVM_RUN ...) overhead), but it's called only less than 75 times at startup. */
    memcpy((char*)mem + addr, run->mmio.data, mmio_len);
  } else if (addr - 0xf0000U < 0x10000U) {  /* BIOS ROM area probes (F000:0000..FFFF:FFFF). */
    if (run->mmio.is_write) {
      /* Ignore writes to ROM area. */
    } else {
      memset(run->mmio.data, 0xff, mmio_len);  /* Typical ROM default value for unmodeled bytes. */
    }
  } else if (addr == 0xffffe && !run->mmio.is_write && mmio_len == 1) {  /* BASIC programs compiled by Microsoft BASIC Professional Development System 7.10 compiler pbc.exe */
    run->mmio.data[0] = 0xfc;  /* Machine ID is regular OC (0xfc). Same as default in src/ints/bios.cpp in DOSBox 0.74. */
  } else if (addr - 0xffff5U < 8U && !run->mmio.is_write && mmio_len == 1) {  /* JWasm 2.11a jwasmr.exe */
    run->mmio.data[0] = "01/01/92"[addr - 0xffff5U];  /* System BIOS date, same as default in src/ints/bios.cpp in DOSBox 0.74. */
  } else if (addr == 0xfff7e && !run->mmio.is_write && mmio_len == 2) {  /* Reading the first MCB pointer in INVARS (see int 0x21 call with ah == 0x52). Used by Microsoft Macro Assembler 6.00B driver masm.exe. */
    *(unsigned short*)run->mmio.data = PROGRAM_MCB_PARA;
  } else if (addr < 0x400 && run->mmio.is_write && addr + mmio_len <= 0x400 && ((mmio_len == 2 && (addr & 1) == 0) || (mmio_len == 4 && (addr & 3) == 0))) {  /* Set interrupt vector directly (not via int 0x21 call with ah == 0x25). */
    /* Microsoft BASIC Professional Development System 7.10 compiler pbc.exe */
    const unsigned char set_int_num = addr >> 2;
    if (mmio_len == 2) {  /* There are subsequent sets (segment and offset parts), we buffer them, and call set_int only once. */
      if (addr & 2) {  /* Set segment part. */
        if ((ongoing_set_int & 0xff) == set_int_num && (ongoing_set_int & 0x100)) {
          *(unsigned*)run->mmio.data = *(unsigned short*)run->mmio.data << 16 | ongoing_set_int >> 16;
          goto do_set_int;
        } else {
          ongoing_set_int = 0x200 | set_int_num | *(unsigned short*)run->mmio.data << 16;
          return IA_NEXT;  /* Prevent `ongoing_set_int = 0' below. */
        }
      } else {  /* Set offset part. */
        if ((ongoing_set_int & 0xff) == set_int_num && (ongoing_set_int & 0x200)) {
          *(unsigned*)run->mmio.data = *(unsigned short*)run->mmio.data | ongoing_set_int >> 16 << 16;
          goto do_set_int;
        } else {
          ongoing_set_int = 0x100 | set_int_num | *(unsigned short*)run->mmio.data << 16;
          return IA_NEXT;  /* Prevent `ongoing_set_int = 0' below. */
        }
      }
    } else { do_set_int:
      if (set_int(set_int_num, *(unsigned*)run->mmio.data, mem, had_get_ints, &tasm30_bitset)) return IA_FATAL;
    }
  } else if (addr == 0xa003e && mmio_len == 2 && !run->mmio.is_write) {
    /* Microsoft Macro Assembler 6.00B driver masm.exe. */
    *(unsigned short*)run->mmio.data = 0;
  } else if (addr == 0x501 && addr + mmio_len <= 0x504 && run->mmio.is_write) {
    /* Microsoft Macro Assembler 1.00 m.exe only writes byte at 0x501. */
    memcpy((char*)mem + addr, run->mmio.data, mmio_len);
  } else if (addr >= 0x400 && addr + mmio_len <= 0x500 && run->mmio.is_write) {
    /* BIOS data area writes (keyboard flags, tick count, video state). */
    memcpy((char*)mem + addr, run->mmio.data, mmio_len);
  } else if (addr >= 0x400 && addr + mmio_len <= 0x500 && !run->mmio.is_write) {
    /* BIOS data area reads: served from the (read-only) page, but an
     * access may have been reported as MMIO anyway; return the data. */
    memcpy(run->mmio.data, (char*)mem + addr, mmio_len);
  } else {
    highmsg[0] = '\0';
   bad_memory_access:
    if (!emu_params->strict_mode) {
      /* Reads of unmapped physical memory return 0xff (floating ISA bus),
       * so memory-sizing probes that write a signature and read it back
       * reliably detect the end of RAM.
       */
      if (!run->mmio.is_write) memset(run->mmio.data, 0xff, mmio_len);
      return IA_NEXT;
    }
    fprintf(stderr, "fatal: KVM memory access denied phys_addr=%08x%s value=%08x%08x size=%u is_write=%u\n", addr, highmsg, ((unsigned*)run->mmio.data)[1], ((unsigned*)run->mmio.data)[0], mmio_len, run->mmio.is_write);
    return IA_FATAL;
  }
  ongoing_set_int = 0;  /* No set_int operation ongoing. */
  return IA_NEXT;  /* Just continue at following cs:ip. */
}
