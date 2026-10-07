#include "kvikdos.h"

void init_emu(struct EmuState *emu) {
  emu->hv = NULL;
  emu->mem = NULL;
  emu->xmem = NULL;
  emu->xmem_size = 0;
  emu->bios_rom = NULL;
  emu->ems_pool = NULL;
  emu->ems_pool_pages = 0;
}

char *guest_ptr(const EmuState *emu, unsigned long gpa) {
  if (gpa < GUEST_MEM_LIMIT) return (char*)emu->mem + gpa;
  if (emu->xmem_size && gpa - 0x100000UL < emu->xmem_size) return (char*)emu->xmem + (gpa - 0x100000UL);
  return NULL;
}

void reset_emu(struct EmuState *emu, const EmuParams *emu_params) {
  void *mem;
  if (!emu->hv) {
    /* First program load: create the VM and map the guest memory regions.
     * The backend is picked by hv_create() (KVM on Linux, WHPX on Windows).
     * Any read/write outside the mapped regions triggers an MMIO exit. */
    if ((emu->hv = hv_create()) == NULL) {
      fprintf(stderr, "fatal: no hypervisor backend available\n");
      exit(252);
    }
    if ((emu->mem = mem = mmap(NULL, GUEST_MEM_LIMIT, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)) ==
        MAP_FAILED) {
      perror("fatal: mmap");
      exit(252);
    }
    /* gpa must be a multiple of the host page size (0x1000). */
    if (hv_set_memory(emu->hv, 0, GUEST_MEM_MODULE_START,
                      GUEST_MEM_LIMIT - GUEST_MEM_MODULE_START,
                      (char*)mem + GUEST_MEM_MODULE_START, 0) < 0) {
      perror("fatal: hv_set_memory");
      exit(252);
    }
    if (GUEST_MEM_MODULE_START != 0) {
      /* Magic interrupt table + BIOS data area: read-only page (0x0-0xfff),
       * so guest writes arrive as MMIO exits for the run loop to apply. */
      if (hv_set_memory(emu->hv, 1, 0, 0x1000, mem, 1) < 0) {
        perror("fatal: hv_set_memory page0");
        exit(252);
      }
    }
    /* Extended memory above 1 MiB, used by protected-mode programs and
     * reported via int 15h/XMS/CMOS. Lazily faulted by the kernel.
     */
    if (emu_params->mem_mb > 1) {
      emu->xmem_size = ((unsigned long)emu_params->mem_mb - 1) << 20;
      if ((emu->xmem = mmap(NULL, emu->xmem_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)) == MAP_FAILED) {
        perror("fatal: mmap xmem");
        exit(252);
      }
      if (hv_set_memory(emu->hv, 2, 0x100000, emu->xmem_size, emu->xmem, 0) < 0) {
        perror("fatal: hv_set_memory xmem");
        exit(252);
      }
    }
    /* BIOS ROM page (0xf0000-0xfffff), read-only like a real ROM. Without a
     * mapping, every F-segment read costs an MMIO exit — programs that scan
     * the ROM area (e.g. Borland RTM does a word-by-word sweep at startup)
     * would generate hundreds of thousands of exits. Mostly 0xff bytes, plus
     * the few locations programs probe (kept in sync with mmio_dispatch). */
    if ((emu->bios_rom = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
      perror("fatal: mmap bios_rom");
      exit(252);
    }
    memset(emu->bios_rom, 0xff, 0x10000);
    memcpy((char*)emu->bios_rom + 0xfff5, "01/01/92", 8);  /* System BIOS date (same as DOSBox default). */
    ((char*)emu->bios_rom)[0xfffe] = (char)0xfc;  /* Machine ID: PC AT (same as DOSBox default). */
    *(unsigned short*)((char*)emu->bios_rom + 0xff7e) = PROGRAM_MCB_PARA;  /* INVARS first-MCB mirror used by masm.exe. */
    ((char*)emu->bios_rom)[0xfff0] = (char)0xcb;  /* retf at the reset vector: a stray far call returns harmlessly. */
    if (hv_set_memory(emu->hv, 3, 0xf0000, 0x10000, emu->bios_rom, 1) < 0) {
      perror("fatal: hv_set_memory bios_rom");
      exit(252);
    }
    if (hv_create_vcpu(emu->hv) < 0) exit(252);
    if (hv_get_sregs(emu->hv, &emu->initial_sregs) < 0) {  /* Will be reused by DOS exec(). */
      perror("fatal: hv_get_sregs");
      exit(252);
    }
  } else {
    mem = emu->mem;
    memset((char*)mem + (PSP_PARA << 4), '\0', DOS_MEM_LIMIT - (PSP_PARA << 4));
    memset(mem, '\0', ENV_PARA << 4);
    memset((char*)mem + ENV_LIMIT, '\0', (PSP_PARA << 4) - ENV_LIMIT);
  }
  mouse_reset();  /* int 33h driver state is per-program, like a real MOUSE.COM reload. */
}
