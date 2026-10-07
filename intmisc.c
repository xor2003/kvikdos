#include "kvikdos.h"
#include "intrun.h"

/* Minimal stdout emitter shared by int 29h and DOS console writes.
 * stdout_write_p/stdout_write_end bound the bytes to emit. */
void emit_stdout(void) {
  if (vid_active) {
    vid_write_str(mem, stdout_write_p, stdout_write_end);
    vid_render(mem);
  } else {
    if (is_stdout_write_cursor) {
      const char *p = stdout_write_p;
      unsigned short *cursor = (unsigned short*)((char*)mem + 0x450);
      if (*cursor <= 0xff) {
        while (p != stdout_write_end) {
          const char c = *p++;
          if (c - 32U <= 126U - 32U && *cursor < 0xff) ++*cursor; else *cursor = 0;
        }
      }
    }
    (void)!write(1, stdout_write_p, stdout_write_end - stdout_write_p);
  }
}

int int29_dispatch(void) {
  stdout_write_p = (const char*)&regs.rax;
  stdout_write_end = stdout_write_p + 1;
  emit_stdout();
  return IA_NEXT;
}

/* Program termination: at the top level it ends the VM run; under an
 * exec'd child it records the child's exit code and pops back to the
 * suspended parent (DOS process semantics). */
int exit_or_pop2(unsigned char rc, unsigned char type) {
  last_exec_return_code = rc;
  last_exec_exit_type = type;
  return exec_depth != 0 ? IA_EXEC_POP : IA_EXIT;
}

int exit_or_pop(unsigned char rc) {
  return exit_or_pop2(rc, 0);
}

int int20_dispatch(void) {
*(unsigned char*)&regs.rax = 0;  /* EXIT_SUCCESS. */
return exit_or_pop(0);
  return IA_NEXT;
}

int int22_dispatch(void) {
/* Termination handler vector. */
        /* Keep execution flow by returning from the interrupt. */
  return IA_NEXT;
}

/* Real 18.2 Hz BIOS tick count (per msdos_player): ticks since local
 * midnight, same value the BDA keeps at 0x46c. */
unsigned long bda_ticks_now(void) {
  struct timeval tv;
  const struct tm *lt;
  const time_t t = time(NULL);
  gettimeofday(&tv, NULL);
  lt = localtime(&t);
  return (unsigned long)((lt->tm_hour * 3600.0 + lt->tm_min * 60.0 + lt->tm_sec)
                         * 1193180.0 / 65536.0 + tv.tv_usec * 1193180.0 / (65536.0 * 1e6));
}

void bda_update_ticks(void *mem) {
  *(unsigned*)((char*)mem + 0x46c) = (unsigned)bda_ticks_now();
}

void guest_inject_irq(struct hv *hv, void *mem, unsigned int_no) {
  /* Manual IRQ injection: push FLAGS,CS,IP like the CPU does for a hardware
   * interrupt, clear IF+TF, and jump to the guest's IVT handler.  Used for
   * IRQ1 (int 9) so guest-installed keyboard ISRs run — required by IDEs
   * (QuickBASIC) that wait for the ISR flag rather than polling int 16h.
   * Operates on the shared regs/sregs globals: the run loop pushes them to
   * the vCPU before the next hv_run. */
  unsigned char *const m = (unsigned char*)mem;
  const unsigned vec = ((const unsigned*)mem)[int_no];
  unsigned short sp;
  (void)hv;
  if (!(regs.rflags & (1u << 9))) return;  /* IF=0: guest isn't taking IRQs now. */
  sp = (unsigned short)(regs.rsp - 6);
  *(unsigned short*)(m + sregs.ss.base + sp + 4) = (unsigned short)regs.rflags;
  *(unsigned short*)(m + sregs.ss.base + sp + 2) = sregs.cs.selector;
  *(unsigned short*)(m + sregs.ss.base + sp + 0) = (unsigned short)regs.rip;
  *(unsigned short*)&regs.rsp = sp;
  *(unsigned short*)&regs.rflags &= ~((1u << 9) | (1u << 8));  /* IF+TF clear, per real int entry. */
  *(unsigned short*)&regs.rip = (unsigned short)vec;
  sregs.cs.selector = (unsigned short)(vec >> 16);
  sregs.cs.base = (unsigned long)sregs.cs.selector << 4;
  pic_isr |= (unsigned char)(1u << (int_no - 8));  /* IRQ now in service until EOI. */
}

void kbd_maybe_inject_irq(void) {
  /* IRQ1 delivery per real 8042+PIC semantics: the line stays asserted while
   * a scancode byte pends, but the PIC won't re-deliver the same IRQ while
   * it is in service (pic_isr, cleared by the ISR's EOI to port 0x20) or
   * masked (pic_imr, port 0x21).  The ISR drains 0x60 one byte per IRQ, so
   * the make+break pair arrives over two injections — exactly like hardware. */
  if (guest_int9_hooked(mem) && (tty_raw_pending() || (tty_bk_pending() && kbd_can_push(mem))) && !(pic_isr & 2) && !(pic_imr & 2)) {
    guest_inject_irq(emu->hv, mem, 9);
  }
}

int int09_dispatch(void) {
  /* BIOS keyboard hardware ISR (the ROM BIOS int 9): reached when the guest
   * int 9 owner chains to the original vector — standard for IDE keyboard
   * drivers like QuickBASIC's, which read port 0x60 themselves then let the
   * BIOS fill the keyboard buffer.  Pops one decoded keycode (queued at
   * drain time — host escape decoding already produced scan<<8|ascii plus
   * modifier bits), pushes it into the BDA buffer, then issues the PIC EOI.
   * A raw-queue pop here would race the break byte: the ISR's own 0x60 read
   * already consumed the make code, so the next byte is the release. */
  unsigned key, mods;
  /* Wait for BDA room: the staged queue holds the key until the app drains
   * the 16-entry buffer (burst-safe). */
  if (kbd_can_push(mem) && tty_bk_pop(&key, &mods) == 0) {
    kbd_push(mem, key, (int)mods);
  }
  pic_isr &= (unsigned char)~2u;  /* IRQ1 EOI — what `out 0x20,0x20` does. */
  return IA_NEXT;
}

int int1a_dispatch(void) {
/* Timer. */
        if (ah == 0x00) {  /* Read system clock counter (real time, per msdos_player). */
          const unsigned long t = bda_ticks_now();
          tick_count = (unsigned)t;  /* Keep the legacy counter in sync for any other readers. */
          *(unsigned char*)&regs.rax = ((unsigned char*)mem)[0x470];  /* Midnight-rollover byte from the BDA. */
          *(unsigned short*)&regs.rcx = (unsigned short)(t >> 16);
          *(unsigned short*)&regs.rdx = (unsigned short)t;
          bda_update_ticks(mem);
        } else {
          return IA_FATAL_INT;
        }
  return IA_NEXT;
}

int int16_dispatch(void) {
/* Keyboard: the BDA buffer at 0x41e is canonical (per msdos_player), so
 * direct-buffer readers and BIOS callers share state. */
        if (ah == 0x12) {  /* Get extended keyboard status. */
          *(unsigned short*)&regs.rax = *(const unsigned short*)((const char*)mem + 0x417);  /* In BDA, 0 by default, no modifier keys pressed. */
        } else if (ah == 0x02) {  /* Get keyboard status. */
          *(unsigned char*)&regs.rax = *(const unsigned char*)((const char*)mem + 0x417);  /* In BDA, 0 by default, no modifier keys pressed. */
        } else if (ah == 0x05) {  /* Store keystroke CX in the buffer. */
          kbd_push(mem, *(unsigned short*)&regs.rcx, -1);
          *(unsigned char*)&regs.rax = 0;  /* AL=0: stored. */
        } else if (ah == 0x01 || ah == 0x11 ||  /* Check buffer, do not clear. */
                   ah == 0x00 || ah == 0x10) {  /* Wait for keystroke and read. */
          vid_render(mem);  /* The program is idle waiting for input: repaint now. */
          if (tty_state->tty_in_fd == -3) {  /* Fake keys (test mode). */
            process_key(tty_state, ah, (unsigned short*)&regs.rax, (unsigned short*)&regs.rflags);
          } else {
            /* Fold pending host keys in; a guest int 9 ISR fills the BDA
             * buffer itself (via injected IRQ1 + port 0x60). */
            (void)tty_drain(tty_state, mem, !guest_int9_hooked(mem));
            if (ah & 1) {
              const int k = kbd_peek(mem);
              if (k < 0) {
                *(unsigned short*)&regs.rflags |= (1 << 6);  /* ZF=1, no key. */
              } else {
                *(unsigned short*)&regs.rax = (unsigned short)k;
                *(unsigned short*)&regs.rflags &= ~(1 << 6);  /* ZF=0, key available. */
              }
            } else {
              int k = kbd_pop(mem);
              if (k < 0) {
                int mods = 0;
                k = tty_wait_key(tty_state, &mods);  /* Block for one host keypress. */
                kbd_push(mem, (unsigned)k, mods);
                k = kbd_pop(mem);
              }
              *(unsigned short*)&regs.rax = (unsigned short)(k < 0 ? 0 : k);
            }
          }
        } else {
          return IA_FATAL_INT;
        }
  return IA_NEXT;
}

int int2a_dispatch(void) {
/* Network. */
        if (ah == 0x00) {  /* Network installation query. */
          /* By returning ah == 0x00 we indicate that the network is not installed. */
        } else {
          return IA_FATAL_INT;
        }
  return IA_NEXT;
}

int int11_dispatch(void) {
/* Get BIOS equipment flags. */
        *(unsigned short*)&regs.rax = *(const unsigned short*)((const char*)mem + 0x410);
  return IA_NEXT;
}

int int17_dispatch(void) {
/* Printer service (per ntvdm/msdos_player). IDEs poll AH=02 for printer
 * status while idling; report a ready, selected, not-busy printer so the
 * poll terminates (0x90 = bit7 not-busy + bit4 selected, no error/timeout).
 * It's also a device-idle poll point — feed the keyboard buffer here like
 * real IRQ1-driven BIOSes do asynchronously. */
        (void)tty_drain(tty_state, mem, !guest_int9_hooked(mem));
        *(unsigned char*)((char*)&regs.rax + 1) = (char)0x90;  /* AH = status. */
  return IA_NEXT;
}

int int2f_dispatch(void) {
/* Installation checks. */
        const unsigned char al = (unsigned char)regs.rax;
        const unsigned short ax = (unsigned short)regs.rax;
        if (al == 0x00) {  /* Installation check. */
          if (ah < 2 || ah == 0x15) return IA_FATAL_UIC;  /* Doesn't follow the standard format. */
          if (ah == 0x43) {
            *(unsigned char*)&regs.rax = 0x80;  /* XMS installed. */
          } else {
            *(unsigned char*)&regs.rax = 0;  /* Not installed, OK to install. */
          }
        } else if (ax == 0x4310) {  /* Get XMS entry point ES:BX. */
          SET_SREG(es, 0x50);
          *(unsigned short*)&regs.rbx = 0x0002;  /* Far-callable XMS stub at 0x502: int 43h; retf. */
        } else if (*(unsigned short*)&regs.rax == 0x1100 || *(unsigned short*)&regs.rax == 0x111e) {  /* Redirector/network install checks. */
          *(unsigned short*)&regs.rax = 0;  /* Not installed. */
        } else if (*(unsigned short*)&regs.rax == 0x1600 || *(unsigned short*)&regs.rax == 0x160a) {  /* Windows enhanced mode / Windows version probes. */
          *(unsigned short*)&regs.rax = 0;  /* Not running under Windows/386 enhanced services. */
        } else if (*(unsigned short*)&regs.rax == 0xed10) {  /* LINK.EXE probe in some MASM/MSC toolchains. */
          *(unsigned short*)&regs.rax = 0;  /* Not installed / no service. */
        } else if (*(unsigned short*)&regs.rax == 0x1687) {  /* DPMI host probe. */
          /* Keep AX unchanged: no resident DPMI host yet (an external host
           * such as CWSDPMI/HDPMI32/DPMIRES may install itself later). */
        } else if (*(unsigned short*)&regs.rax == 0xfb42 ||
                   *(unsigned short*)&regs.rax == 0xfb43) {
          /* Borland RTM/DPMI multiplex: leave AX unchanged so the stub sees
           * "no resident host" and loads DPMI16BI.OVL itself (upstream pts
           * behavior; returning AX=0 makes BC.EXE/TLINK.EXE fail with
           * "Failed to locate DPMI server"). */
        } else { 
          if (!emu_params->strict_mode) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
            return IA_NEXT;
          }
          fprintf(stderr, "fatal: unsupported int 0x%02x ax:%04x\n", int_num, *(const unsigned short*)&regs.rax);
          return IA_FATAL_INT;
        }
  return IA_NEXT;
}

int int15_dispatch(void) {
/* System BIOS. */
        const unsigned short ax = (unsigned short)regs.rax;
        const unsigned long ext_kb = emu->xmem_size >> 10;
        if (ax == 0xe801) {  /* Check for large free extended memory (XMS). */
          if (ext_kb) {
            *(unsigned short*)&regs.rax = *(unsigned short*)&regs.rbx = ext_kb > 0x3c00UL ? 0x3c00 : (unsigned short)ext_kb;  /* KiB between 1 MiB and 16 MiB. */
            *(unsigned short*)&regs.rcx = *(unsigned short*)&regs.rdx = ext_kb > 0x3c00UL ? (unsigned short)((ext_kb - 0x3c00UL) >> 6) : 0;  /* 64 KiB units above 16 MiB. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
          } else {
            *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1: no extended memory. */
          }
        } else if (ah == 0x88) {  /* Get extended memory (XMS) size. */
          *(unsigned short*)&regs.rax = ext_kb > 0xffffUL ? 0xffff : (unsigned short)ext_kb;
          *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
        } else if (ah == 0x87) {  /* Move block (to/from extended memory), 286+. */
          /* ES:SI points to a pseudo-GDT; descriptor 2 (offset 0x10) is the
           * source and descriptor 3 (offset 0x18) is the destination; CX is a
           * word count. Descriptor base = bytes 2,3,4 and byte 7 (bits 24-31). */
          const unsigned gdt_lin = ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rsi);
          const unsigned char * const gdt = (const unsigned char*)guest_ptr(emu, gdt_lin);
          unsigned long src_lin = 0, dst_lin = 0;
          unsigned words;
          char *src, *dst;
          if (gdt) {
            src_lin = (unsigned)gdt[0x12] | ((unsigned)gdt[0x13] << 8) | ((unsigned)gdt[0x14] << 16) | ((unsigned)gdt[0x17] << 24);
            dst_lin = (unsigned)gdt[0x1a] | ((unsigned)gdt[0x1b] << 8) | ((unsigned)gdt[0x1c] << 16) | ((unsigned)gdt[0x1f] << 24);
          }
          words = *(unsigned short*)&regs.rcx;
          src = gdt ? guest_ptr(emu, src_lin) : NULL;
          dst = gdt ? guest_ptr(emu, dst_lin) : NULL;
          if (!src || !dst) {
            *(unsigned short*)&regs.rax = 0x02 | (unsigned)ah << 8;  /* AH=2: invalid GDT/segment. */
            *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
          } else {
            memmove(dst, src, (size_t)words << 1);
            *(unsigned short*)&regs.rax = 0;  /* AH=0: success. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
          }
        } else if (ah == 0x89) {  /* Switch to protected mode (obsolete 286 BIOS call). */
          *(unsigned short*)&regs.rax = 0x86 | (unsigned)ah << 8;  /* AH=0x86: function not supported. */
          *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
        } else if (ah == 0xbf || ax == 0x1022) {  /* DESQview multitasker API probes: not running under DESQview. */
          *(unsigned short*)&regs.rax = 0x86 | (unsigned)ah << 8;
          *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
        } else if (ax == 0xe820) {  /* E820: query system address map. */
          /* ES:DI = result buffer, EBX = continuation index, ECX = buffer size,
           * EDX = 'SMAP'. Returns a 20-byte {base, length, type} record. */
          const unsigned nentries = emu->xmem_size ? 3U : 2U;
          const unsigned idx = (unsigned)regs.rbx;
          unsigned char * const buf =
              (unsigned char*)guest_ptr(emu, ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdi));
          unsigned long base = 0, len = 0, type = 1;
          unsigned i;
          switch (idx) {
            case 0: base = 0;        len = 0x9fc00; type = 1; break;  /* Conventional RAM. */
            case 1: base = 0x9fc00;  len = 0x60400; type = 2; break;  /* EBDA/video/ROM hole. */
            case 2: base = 0x100000; len = emu->xmem_size; type = 1; break;  /* Extended RAM. */
            default: break;
          }
          if (!buf || (unsigned)regs.rdx != 0x534d4150UL || idx >= nentries) {
            *(unsigned short*)&regs.rax = 0x86 | (unsigned)ah << 8;
            *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
          } else {
            for (i = 0; i < 8; ++i) buf[i] = (unsigned char)(base >> (i * 8));
            for (i = 0; i < 8; ++i) buf[8 + i] = (unsigned char)(len >> (i * 8));
            for (i = 0; i < 4; ++i) buf[16 + i] = (unsigned char)(type >> (i * 8));
            *(unsigned short*)&regs.rcx = 20;
            *(unsigned*)&regs.rbx = idx + 1 < nentries ? idx + 1 : 0;  /* 0 = last entry. */
            regs.rax = 0x534d4150UL;  /* 'SMAP' signature. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
          }
        } else if (ah == 0xc0) {  /* Get configuration. */
          *(unsigned short*)&regs.rax = 0x86 | (unsigned)ah << 8;  /* Not supported. */
          *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
        } else {
          return IA_FATAL_UIC;
        }
  return IA_NEXT;
}

int int0d_dispatch(void) {
/* General protection fault. */
        /* Allow DOS extender probes to fail softly. */
  return IA_NEXT;
}

int int00_dispatch(void) {
/* Division by zero. */
        /* Reached only when the program hasn't overridden IVT[0] — either a
         * real CPU #DE or a software `int 0'.  Real DOS prints "Divide
         * error" and aborts the program; returning to the faulting div
         * would just retrap forever, so terminate the program instead. */
        fprintf(stderr, "Divide error at cs:%04x ip:%04x, aborting program.\n", int_cs, int_ip);
  *(unsigned short*)&regs.rax = 0xf0;  /* Unsuccessful termination, like DOS aborting the program. */
  return exit_or_pop2(0xf0, 2);  /* Termination type 2 = critical error/abort. */
}

int int03_dispatch(void) {
/* Turbo Assembler (TASM) 3.0. */
  return IA_NEXT;
}

/* Dispatch a software interrupt (int_num) to its handler.
 * Returns an IA_* action for the run loop. */
int int_dispatch(void) {
  switch (int_num) {
  case 0x29: return int29_dispatch();
  case 0x20: return int20_dispatch();
  case 0x22: return int22_dispatch();
  case 0x21: return int21_dispatch();
  case 0x10: return int10_dispatch();
  case 0x1a: return int1a_dispatch();
  case 0x16: return int16_dispatch();
  case 0x2a: return int2a_dispatch();
  case 0x11: return int11_dispatch();
  case 0x17: return int17_dispatch();
  case 0x2f: return int2f_dispatch();
  case 0x15: return int15_dispatch();
  case 0x67: return int67_dispatch();
  case 0x43: return int43_dispatch();
  case 0x09: return int09_dispatch();
  case 0x0d: return int0d_dispatch();
  case 0x00: return int00_dispatch();
  case 0x08: return IA_NEXT;  /* IRQ0 timer landing on our stub (no guest handler): the BDA tick is updated by the run loop, so a plain iret is the right no-op — like a BIOS default handler. */
  case 0x03: return int03_dispatch();
  }
  return IA_FATAL_INT;
}
