#include "kvikdos.h"

const char default_program_mcb[16] = {
    'Z',  /* 'Z' indicates last member of MCB chain; 'M' would be non-last. */
    (char)PROCESS_ID,  /* \0\0 indicates free block. */
    (char)(PROCESS_ID >> 8),
    (char)(DOS_ALLOC_PARA_LIMIT - PSP_PARA),  /* Number of paragraphs low byte. */
    (char)((DOS_ALLOC_PARA_LIMIT - PSP_PARA) >> 8),  /* Number of paragraphs high byte. */
    0, 0,  /* Size of previous block. Unused for PROGRAM_MCB_PARA. */
    (char)0xb2,  /* Reserved bytes, random. */
    'K', 'V', '1', 'K', 'P', 'R', '0', 'G',  /* "KV1KPR0G". Program name. */
};

const char freed_mcb[16] = {
    '\x88', '\xc9', '\xc0', '\x96', '\x1e', '\xc4', '\x42', '\xd5',  /* Random bytes, part of signature. */
    'K', 'V', '1', 'K', 'F', 'R', '3', '3',  /* "KV1KFR33". Program name signature. */
};

char is_mcb_bad(void *mem, unsigned short block_para) {
  const char* mcb = (const char*)mem + (block_para << 4) - 16;
  unsigned short size_para;
  if (block_para < PSP_PARA) return 1;  /* MCB too low in memory. qblink.exe calls with block_para==0 many times, but it's still bad. */
  if (block_para >= DOS_ALLOC_PARA_LIMIT) return 2;  /* MCB too high in memory. */
  if (memcmp(mcb + 7, default_program_mcb + 7, 9) != 0) return 3;  /* MCB has bad signature. */
  if (MCB_TYPE(mcb) == 'Z') {
    /* A free last MCB ('Z' with PID 0) is legal: DOS keeps the whole
     * conventional arena chained, ending in a free 'Z' tail. */
  } else if (MCB_TYPE(mcb) != 'M') {
    return 5;  /* Bad MCB type. */
  }
  if (MCB_PID(mcb) != 0 && MCB_PID(mcb) >= DOS_ALLOC_PARA_LIMIT) return 6;  /* Bad MCB process ID: owner must be a plausible PSP paragraph (DOS stamps the owner's PSP, e.g. Phar Lap sub-PSPs), not just kvikdos's fake PROCESS_ID. */
  size_para = MCB_SIZE_PARA(mcb);
  if (MCB_TYPE(mcb) == 'Z') {
    if (block_para + size_para > DOS_ALLOC_PARA_LIMIT) return 7;  /* Final MCB too long. */
  } else {
    const char * const next_mcb = mcb + 16 + (size_para << 4);
    if (block_para + size_para >= DOS_ALLOC_PARA_LIMIT) return 8;  /* Non-final MCB too long. */
    if (MCB_PSIZE_PARA(next_mcb) != size_para) return 9;  /* MCB size and next psize mismatch. */
    if (MCB_PID(mcb) == 0 && MCB_PID(next_mcb) == 0) return 10;  /* found adjacent free MCBs in next. */
  }
  if (block_para == PSP_PARA) {
    if (MCB_PSIZE_PARA(mcb) != 0) return 11;  /* Nonzero PSP MCB psize. */
    if (MCB_PID(mcb) == 0) return 12;  /* PSP MCB is free. */
  } else {
    const char * const prev_mcb = mcb - 16 - (MCB_PSIZE_PARA(mcb) << 4);
    if (block_para < PSP_PARA + 1 + MCB_PSIZE_PARA(prev_mcb)) return 13;  /* MCB psize too large. */
    if (MCB_TYPE(prev_mcb) != 'M') return 14;  /* Bad prev MCB type. */
    if (MCB_SIZE_PARA(prev_mcb) != MCB_PSIZE_PARA(mcb)) return 15;  /* MCB prev size and psize mismatch. */
    if (MCB_PID(mcb) == 0 && MCB_PID(prev_mcb) == 0) return 16;  /* Found adjacent free MCBs in prev. */
  }
  return 0;  /* MCB looks good. */
}

void check_all_mcbs(void *mem) {
  unsigned block_para = PSP_PARA;
  for (;;) {
    const char mcb_error = is_mcb_bad(mem, block_para);
    const char * const mcb = (const char*)mem + (block_para << 4) - 16;
    if (mcb_error) {
      fprintf(stderr, "fatal: bad MCB for block_para=0x%04x: %d\n", block_para, mcb_error);
      exit(252);
    }
    if (MCB_TYPE(mcb) == 'Z') break;
    block_para += 1 + MCB_SIZE_PARA(mcb);
  }
}

/* Initializes the DOS List-of-Lists (int 21h AH=52h): a dos_info_t laid out
 * like msdos_player. ES:BX = 0x66:0x10 points at first_dpb, so first_mcb sits
 * at [ES:BX-2] = linear INVARS_LIN-2. Table spans INVARS_LIN-0x26 ..
 * INVARS_LIN+0x6f and carries a NUL device header + retf routine, like DOS's
 * own sysvars page. */
void init_dos_info(void *mem, unsigned long xmem_size) {
  char * const di = (char*)mem + INVARS_LIN;
  const unsigned ds = (INVARS_LIN >> 4) - 1;  /* Table segment 0x66. */
  const unsigned nul_off = INVARS_LIN + 0x22 - (ds << 4);  /* +0x22, off 0x32. */
  const unsigned rtn_off = INVARS_LIN + 0x69 - (ds << 4);  /* +0x69, off 0x79. */
  const unsigned long xmem_kb = xmem_size >> 10;
  memset(di - 0x26, 0, 0x96);
  *(unsigned short*)(di - 0x22) = 1;  /* magic_word. */
  *(unsigned short*)(di - 0x02) = PROGRAM_MCB_PARA;  /* first_mcb. */
  memset(di + 0x00, 0xff, 8);  /* first_dpb, first_sft: none. */
  *(unsigned short*)(di + 0x08) = nul_off;  /* clock_device: -> NUL. */
  *(unsigned short*)(di + 0x0a) = ds;
  *(unsigned short*)(di + 0x0c) = nul_off;  /* con_device: -> NUL. */
  *(unsigned short*)(di + 0x0e) = ds;
  *(unsigned short*)(di + 0x10) = 0x200;  /* max_sector_len. */
  memset(di + 0x12, 0xff, 12);  /* disk_buf_info, cds, fcb_table: none. */
  di[0x21] = DRIVE_COUNT;  /* last_drive (LASTDRIVE). */
  memset(di + 0x22, 0xff, 4);  /* nul_device.next_driver: end of chain. */
  *(unsigned short*)(di + 0x26) = 0x8004;  /* nul_device.attributes. */
  *(unsigned short*)(di + 0x28) = rtn_off;  /* nul_device.strategy: retf. */
  *(unsigned short*)(di + 0x2a) = rtn_off;  /* nul_device.interrupt: retf. */
  memcpy(di + 0x2c, "NUL     ", 8);  /* nul_device.dev_name. */
  *(unsigned short*)(di + 0x3f) = 20;  /* buffers_x. */
  di[0x43] = 3;  /* boot_drive: C:. */
  di[0x44] = 1;  /* i386_or_later. */
  *(unsigned short*)(di + 0x45) = (unsigned short)(xmem_kb > 0xffffUL ? 0xffffUL : xmem_kb);  /* ext_mem_size. */
  memset(di + 0x47, 0xff, 4);  /* disk_buf_heads: none. */
  *(unsigned short*)(di + 0x66) = 0xffff;  /* first_umb_fcb: no UMBs. */
  *(unsigned short*)(di + 0x68) = PROGRAM_MCB_PARA;  /* first_mcb_2. */
  di[0x69] = (char)0xcb;  /* nul_device_routine: retf. */
}

int detect_dos_executable_program(int img_fd, const char *prog_filename, char *p) {
  int r;
  if (img_fd < 0) {  /* For --kvm-check. */
    r = 1;
    *p = '\xc3';  /* `ret' instruction for DOS .com program. It exits successfully. */
  } else {
    r = read(img_fd, p, PROGRAM_HEADER_SIZE);
  }
  if (r < 0) {
    perror("fatal: error reading DOS executable program header");
    exit(252);
  }
  if (r == 0) {
    fprintf(stderr, "fatal: empty DOS executable program");
    exit(252);
  }
  if (r >= 2 && (('M' | 'Z' << 8) == *(unsigned short*)p || ('M' << 8 | 'Z') == *(unsigned short*)p)) {
    if (r < 24) {
      /* DOSBox 0.74-4, FreeDOS 1.2 just assume that it's a .com program if
       * 2 <= file_size <= 27 and it starts with "MZ" or "ZM".
       *
       * In kvikdos, for files starting with "MZ or "ZM", if 2 <= file_size
       * <= 23, then it fails here, and if file_size >= 24, then is treated
       * as an .exe program.
       */
      fprintf(stderr, "fatal: DOS .exe program too short: %s\n", prog_filename);
      exit(252);
    }
    /* !! TODO(pts): Follow emulated symlink e.g. if tcc.exe contains `@@@ bcc.exe' */
  } else if (r >= 6 && is_same_ascii_nocase(p, "@echo ", 6)) {
    fprintf(stderr, "fatal: DOS .bat batch files not supported as executable: %s\n", prog_filename);
    exit(252);  /* TODO(pts): Delegate a DOS exec() of a .bat to run_dos_batch (top-level .bat targets already run in main.c). */
  } else if (r >= 4 && 0 == memcmp(p, "\x7f""ELF", 4)) {  /* Typically Linux native executable. */
    fprintf(stderr, "fatal: ELF executable programs not supported as executable: %s\n", prog_filename);
    exit(252);  /* TODO(pts): Delegate a DOS exec() of an ELF to run_native_execvp (top-level ELF targets already run natively in main.c). */
  } else if (r >= 3 && ('#' | '!' << 8) == *(unsigned short*)p && (p[2] == ' ' || p[2] == '/')) {
    /* Unix script #! shebang detected. */
    fprintf(stderr, "fatal: Unix scripts not supported: %s\n", prog_filename);
    exit(252);  /* TODO(pts): Delegate a DOS exec() of a script to run_native_execvp (top-level shebang targets already run natively in main.c). */
  } else if (img_fd < 0) {  /* For --kvm-check. */
  } else {  /* Otherwise it's a DOS .com program, but only if it has .com extension. */
    const char *ext = get_linux_ext(prog_filename);
    const size_t ext_size = strlen(ext) + 1;
    if (is_same_ascii_nocase(ext, "com", ext_size)) {  /* OK. */
    } else if (is_same_ascii_nocase(ext, "bat", ext_size)) {
      /* We may add support for a subset of batch file syntax in the future. */
      fprintf(stderr, "fatal: DOS .bat batch files not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "cmd", ext_size)) {
      fprintf(stderr, "fatal: Windows NT and OS/2 .cmd scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "ps1", ext_size)) {
      fprintf(stderr, "fatal: PowerShell .ps1 scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "sh", ext_size)) {
      fprintf(stderr, "fatal: Unix .sh shell scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "pl", ext_size)) {
      fprintf(stderr, "fatal: Perl .pl scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "pm", ext_size)) {
      fprintf(stderr, "fatal: Perl .pm scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "py", ext_size)) {
      fprintf(stderr, "fatal: Python .py scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "rb", ext_size)) {
      fprintf(stderr, "fatal: Ruby .rb scripts not supported: %s\n", prog_filename);
      exit(252);
    } else if (is_same_ascii_nocase(ext, "elf", ext_size)) {
      fprintf(stderr, "fatal: ELF executable programs not supported: %s\n", prog_filename);
      exit(252);
    } else {
      /* Refuse to run as .com program, file may be a data file or text
       * file, containing gargage machine instructions.
       */
      fprintf(stderr, "fatal: neither .exe signature nor filename extension recognized for program: %s\n", prog_filename);
      exit(252);
    }
  }
  return r;
}

static const unsigned char fixed_exepack_stub[283] = {
    0x89, 0xc5, 0x8c, 0xc3, 0x83, 0xc3, 0x10, 0x0e, 0x1f, 0x8b, 0x0e, 0x06,
    0x00, 0x89, 0xc8, 0x83, 0xc0, 0x0f, 0xd1, 0xd8, 0xd0, 0xe8, 0xd0, 0xe8,
    0xd0, 0xe8, 0x8c, 0xda, 0x01, 0xd0, 0x89, 0xda, 0x03, 0x16, 0x0c, 0x00,
    0x39, 0xd0, 0x73, 0x02, 0x89, 0xd0, 0x8e, 0xc0, 0x31, 0xf6, 0x31, 0xff,
    0xf3, 0xa4, 0x8e, 0xc2, 0x50, 0xb8, 0x6e, 0x00, 0x50, 0xcb, 0x83, 0xee,
    0x01, 0x73, 0x08, 0x8c, 0xde, 0x4e, 0x8e, 0xde, 0xbe, 0x0f, 0x00, 0x3e,
    0x8a, 0x04, 0xc3, 0x83, 0xef, 0x01, 0x73, 0x08, 0x8c, 0xc7, 0x4f, 0x8e,
    0xc7, 0xbf, 0x0f, 0x00, 0x26, 0x88, 0x05, 0xc3, 0x31, 0xf6, 0xe8, 0xd9,
    0xff, 0x3c, 0xff, 0x74, 0xf9, 0x46, 0x31, 0xff, 0xe8, 0xcf, 0xff, 0x88,
    0xc2, 0xe8, 0xca, 0xff, 0x88, 0xc5, 0xe8, 0xc5, 0xff, 0x88, 0xc1, 0x88,
    0xd0, 0x24, 0xfe, 0x3c, 0xb0, 0x75, 0x0c, 0xe8, 0xb8, 0xff, 0xe3, 0x15,
    0xe8, 0xc4, 0xff, 0xe2, 0xfb, 0xeb, 0x0e, 0x3c, 0xb2, 0x75, 0x60, 0xe3,
    0x08, 0xe8, 0xa6, 0xff, 0xe8, 0xb4, 0xff, 0xe2, 0xf8, 0xf6, 0xc2, 0x01,
    0x74, 0xca, 0x0e, 0x1f, 0xbe, 0x2d, 0x01, 0x31, 0xd2, 0xad, 0x89, 0xc1,
    0xe3, 0x19, 0xad, 0x89, 0xc7, 0x83, 0xe7, 0x0f, 0xd1, 0xe8, 0xd1, 0xe8,
    0xd1, 0xe8, 0xd1, 0xe8, 0x01, 0xd0, 0x01, 0xd8, 0x8e, 0xc0, 0x26, 0x01,
    0x1d, 0xe2, 0xe7, 0x80, 0xc6, 0x10, 0x75, 0xdd, 0x8b, 0x36, 0x0a, 0x00,
    0x01, 0xde, 0x8b, 0x3e, 0x08, 0x00, 0x01, 0x1e, 0x02, 0x00, 0x83, 0xeb,
    0x10, 0x8e, 0xdb, 0x8e, 0xc3, 0xfa, 0x8e, 0xd6, 0x89, 0xfc, 0xfb, 0x89,
    0xe8, 0xbb, 0x00, 0x00, 0x2e, 0xff, 0x2f, 0x90, 0x90, 0x90, 0x90, 0xb4,
    0x40, 0xbb, 0x02, 0x00, 0xb9, 0x16, 0x00, 0x8c, 0xca, 0x8e, 0xda, 0xba,
    0x17, 0x01, 0xcd, 0x21, 0xb8, 0xff, 0x4c, 0xcd, 0x21, 0x50, 0x61, 0x63,
    0x6b, 0x65, 0x64, 0x20, 0x66, 0x69, 0x6c, 0x65, 0x20, 0x69, 0x73, 0x20,
    0x63, 0x6f, 0x72, 0x72, 0x75, 0x70, 0x74 };

char *load_dos_executable_program(int img_fd, const char *filename, void *mem, const char *header, int header_size, struct kvm_regs *regs, struct kvm_sregs *sregs, unsigned short *block_size_para_out, unsigned psp_para) {
#define MEMSIZE_AVAILABLE_PARA ((DOS_MEM_LIMIT >> 4) - psp_para - 0x10 /* PSP */)
  const unsigned memsize_available_para = MEMSIZE_AVAILABLE_PARA;
  char *psp;
  if (header_size >= 24 && (('M' | 'Z' << 8) == ((unsigned short*)header)[EXE_SIGNATURE] || ('M' << 8 | 'Z') == ((unsigned short*)header)[EXE_SIGNATURE])) {
    const unsigned short * const exehdr = (const unsigned short*)header;
    const unsigned short nblocks = exehdr[EXE_NBLOCKS] & 0x7ff;  /* Turbo C++ 3 BOSS NE stub. Mask to 1 MiB. */
    const unsigned exesize = exehdr[EXE_LASTSIZE] ? ((nblocks - 1) << 9) + exehdr[EXE_LASTSIZE] : nblocks << 9;
    const unsigned headsize = (unsigned)exehdr[EXE_HDRSIZE] << 4;
    const unsigned image_size = exesize - headsize;
    unsigned memsize_min_para = (nblocks << 5) - exehdr[EXE_HDRSIZE] + exehdr[EXE_MINALLOC];  /* This includes .bss after the image. Please note that this doesn't depend on exehdr[EXE_LASTSIZE]. Formula is same as in MS-DOS 6.22, FreeDOS 1.2, DOSBox 0.74-4. */
    unsigned memsize_max_para = (unsigned short)(exehdr[EXE_MAXALLOC] + 1) < 2 ? 0xffff : (nblocks << 5) - exehdr[EXE_HDRSIZE] + exehdr[EXE_MAXALLOC];
    char * const image_addr = (char*)mem + (psp_para << 4) + 0x100;
    const unsigned image_para = psp_para + 0x10;
    unsigned reloc_count = exehdr[EXE_NRELOC];
    const unsigned stack_end_plus_0x100 = ((unsigned)(unsigned short)(exehdr[EXE_SS] + 0x10) << 4) + (exehdr[EXE_SP] ? exehdr[EXE_SP] : 0x10000);
    if (exehdr[EXE_LASTSIZE] > 0x200) {
      fprintf(stderr, "fatal: DOS .exe last block size too large (0x%04x > 0x200): %s\n", exehdr[EXE_LASTSIZE], filename);
      exit(252);
    }
    if (exehdr[EXE_MINALLOC] == 0 && exehdr[EXE_MAXALLOC] == 0) {
      fprintf(stderr, "fatal: loading DOS .exe to upper part of memory not supported: %s\n", filename);
      exit(252);
    }
    if (exesize <= headsize) {
      fprintf(stderr, "fatal: DOS .exe image smaller than header: %s\n", filename);
      exit(252);
    }
    if (stack_end_plus_0x100 > (memsize_min_para << 4) + 0x100) {  /* Some .exe files have it. */
      if (exehdr[EXE_MINALLOC] == 0 && exehdr[EXE_MAXALLOC] == 0xffff) {  /* Some ancient programs such as Microsoft Linker 2.00 link.exe and DeSmet C c88.exe have it. */
        memsize_min_para = (stack_end_plus_0x100 - 0x100 + 0xf) >> 4;  /* Make it large enough. */
      } else {
        fprintf(stderr, "fatal: DOS .exe stack pointer after end of program memory (0x%x - 0x100 > 0x%x): %s\n", stack_end_plus_0x100, memsize_min_para << 4, filename);
        exit(252);
      }
    }
    if (memsize_min_para > memsize_max_para) {
      /* Some historical toolchains emit malformed MINALLOC/MAXALLOC pairs.
       * MS-DOS still loads them by effectively treating MAXALLOC as at least
       * MINALLOC, so clamp instead of failing hard. */
      memsize_max_para = memsize_min_para;
    }
    if (memsize_min_para > memsize_available_para) {
      fprintf(stderr, "fatal: DOS .exe uses too much conventional memory: %s\n", filename);
      exit(252);
    }
    /* 0x10 and 0x100: Allow EXE_CS value of 0xfff0 to wrap around, and then cs would point to the PSP. */
    if (((unsigned)(unsigned short)(exehdr[EXE_CS] + 0x10) << 4) + exehdr[EXE_IP] >= image_size + 0x100) {
      fprintf(stderr, "fatal: DOS .exe entry point after end of image (0x%x >= 0x%x): %s\n", ((unsigned)exehdr[EXE_CS] << 4) + exehdr[EXE_IP], image_size, filename);
      exit(252);
    }
    if ((unsigned)lseek(img_fd, headsize, SEEK_SET) != headsize) {
      fprintf(stderr, "fatal: error seeking to image in DOS .exe: %s\n", filename);
      exit(252);
    }
    if ((unsigned)read(img_fd, image_addr, image_size) != image_size) {
      fprintf(stderr, "fatal: error reading image in DOS .exe: %s\n", filename);
      exit(252);
    }
    if (getenv("KVIKDOS_DUMP_LOAD")) {  /* Dump conventional mem right after image load. */
      FILE *df = fopen("/tmp/load-lowmem.bin", "wb");
      if (df) { fwrite(mem, 1, 0xc0000, df); fclose(df); }
      fprintf(stderr, "load-dump: image_size=%x loaded to %p-ish, byte@file0x23b70=%02x\n",
              image_size, (void*)image_addr,
              (unsigned)*(const unsigned char*)(image_addr + (0x23b70 - headsize)));
    }
    if (reloc_count) {  /* Process relocations. */
      unsigned short reloc[1024]; /* 2048 bytes on the stack. */
      if (header_size < 26) {  /* exehdr[EXE_RELOCPOS] is not available. */
        fprintf(stderr, "fatal: DOS .exe too short for relocpos: %s\n", filename);
        exit(252);
      }
      if ((unsigned)lseek(img_fd, exehdr[EXE_RELOCPOS], SEEK_SET) != exehdr[EXE_RELOCPOS]) {
        fprintf(stderr, "fatal: error seeking to image relocations: %s\n", filename);
        exit(252);
      }
      while (reloc_count != 0) {
        const unsigned to_read = reloc_count > (sizeof(reloc) >> 2) ? sizeof(reloc) : reloc_count << 2;
        const unsigned got = read(img_fd, reloc, to_read);
        unsigned short *r, *rend;
        if (got != to_read) {
          fprintf(stderr, "fatal: error reading relocations in DOS .exe: %s\n", filename);
          exit(252);
        }
        reloc_count -= got >> 2;
        for (r = reloc, rend = r + (got >> 1); r != rend; r += 2) {
          *(unsigned short*)(image_addr + ((unsigned)r[1] << 4) + r[0]) += image_para;
        }
      }
    }
    psp = image_addr - 0x100;
    *(unsigned short*)&regs->rip = exehdr[EXE_IP];  /* DOS .exe entry point. */
    sregs->cs.selector = exehdr[EXE_CS] + image_para;
    *(unsigned short*)&regs->rsp = exehdr[EXE_SP];
    sregs->ss.selector = exehdr[EXE_SS] + image_para;
    *(unsigned*)(psp + 6) = 0xc0;  /* CP/M far call 5 service request address. Obsolete. */
    *block_size_para_out = ((memsize_max_para > memsize_available_para) ? memsize_available_para : memsize_max_para) + 0x10 /* PSP */;
    if (exehdr[EXE_IP] == 16 || exehdr[EXE_IP] == 18 || exehdr[EXE_IP] == 20) {  /* Detect exepack, find decompression stub within it, replace stub with fixed stub to avoid ``Packed file is corrupt'' error. DOS 5.0 does a similar fix. */
      /* More info about the A20 bug in the exepack stubs: https://github.com/joncampbell123/dosbox-x/issues/7#issuecomment-667653041
       * More info about the exepack file format: https://www.bamsoftware.com/software/exepack/
       * Example error with buggy (unfixed) stubs: fatal: KVM memory access denied phys_addr=00101103 value=0000000000000000 size=1 is_write=0
       */
      unsigned short * const packhdr = (unsigned short*)(image_addr + ((unsigned)exehdr[EXE_CS] << 4));
      const unsigned exepack_max_size = image_size - ((unsigned)exehdr[EXE_CS] << 4) - exehdr[EXE_IP];
      const unsigned exepack_stub_plus_reloc_size =  packhdr[3] - exehdr[EXE_IP];
      if (*(unsigned short*)((char*)packhdr + exehdr[EXE_IP] - 2) == ('R' | 'B' << 8) &&  /* exepack signature. */
          exepack_stub_plus_reloc_size >= 258 && exepack_stub_plus_reloc_size <= exepack_max_size) {
        char *after_packhdr = (char*)packhdr + exehdr[EXE_IP];
        const char *c = (const char*)memmem(after_packhdr, exepack_stub_plus_reloc_size, "\xcd\x21\xb8\xff\x4c\xcd\x21", 7);
        if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "info: detected DOS .exe packed with exepack: header_size=%d exepack_max_size=%u exepack_stub_plus_reloc_size=%u\n", exehdr[EXE_IP], exepack_max_size, exepack_stub_plus_reloc_size);
        if (c) {
          const unsigned exepack_stub_size = (unsigned)(c + 7 + 22 - after_packhdr);
          if (exepack_stub_size >= 258 && exepack_stub_size <= 290) {
            if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "info: detected DOS .exe packed with exepack: header_size=%d exepack_max_size=%u exepack_stub_plus_reloc_size=%u exepack_stub_size=%u\n", exehdr[EXE_IP], exepack_max_size, exepack_stub_plus_reloc_size, exepack_stub_size);
            /* Fix A20 bug (failure as ``Packed file is corrupt'' because ES
             * wraps around 0x10000) by replacing the stub.
             */
            memmove((char*)packhdr + 18 + sizeof(fixed_exepack_stub), after_packhdr + exepack_stub_size, exepack_stub_plus_reloc_size - exepack_stub_size);  /* Move packed reloc. */
            memcpy((char*)packhdr + 18, fixed_exepack_stub, sizeof(fixed_exepack_stub));  /* Copy fixed stub. */
            if (exehdr[EXE_IP] != 18) {
              if (exehdr[EXE_IP] == 16) {  /* Make it longer, because fixed_exepack_stub works only with an 18-byte header (it has org 18 in stub.asm). */
                *(unsigned short*)&regs->rip = 18;  /* Update DOS .exe entry point. */
                packhdr[7] = 1;  /* skip_len. */
              } else {  /* (exehdr[EXE_IP] == 20) */  /* Microsoft Macro Assembler 5.10A linker link.exe. */
                if (packhdr[4] != 0) {
                  fprintf(stderr, "fatal: unexpected packhdr[4] in 20-byte exepack header: 0x%04x\n", packhdr[4]);
                  exit(252);
                }
                packhdr[4] = packhdr[5];
                packhdr[5] = packhdr[6];
                packhdr[6] = packhdr[7];
                packhdr[7] = packhdr[8];
              }
              packhdr[8] = ('R' | 'B' << 8);  /* exepack signature. */
            }
          }
        }
      }
    }
  } else {
    /* Load DOS .com program. */
    char * const p = (char *)mem + (psp_para << 4) + 0x100;
    int r;
    memcpy(p, header, header_size);
    r = img_fd < 0 ? 0 :  /* For --kvm-check. */
        read(img_fd, p + header_size, MAX_DOS_COM_SIZE + 1 - header_size);
    if (r < 0) { /*read_error:*/
      perror("fatal: error reading DOS executable program");
      exit(252);
    }
    r += header_size;
    if (r > MAX_DOS_COM_SIZE) {
      fprintf(stderr, "fatal: DOS executable program too long: %s\n", filename);
      exit(252);
    }
    sregs->cs.selector = sregs->ss.selector = psp_para;
    psp = (char*)mem + (psp_para << 4);  /* Program Segment Prefix. */
    *(unsigned short*)&regs->rsp = 0xfffe;
    *(unsigned short*)(psp + *(unsigned short*)&regs->rsp) = 0;  /* Push a 0 byte. */
    *(unsigned short*)(psp + 6) = MAX_DOS_COM_SIZE + 0x100;  /* .COM bytes available in segment (CP/M). DOSBox doesn't initialize it. */
    /*memset(psp, 0, 0x100);*/  /* Not needed, mmap MAP_ANONYMOUS has done it. */
    *(unsigned short*)&regs->rip = 0x100;  /* DOS .com entry point. */
    /* A .com gets all the rest of conventional memory. Guard against a
     * load base so high it leaves under 64 KiB (0x1000 paras). */
    if (memsize_available_para + 0x10 < 0x1000) {
      fprintf(stderr, "fatal: load base leaves too little conventional memory for .com: %s\n", filename);
      exit(252);
    }
    *block_size_para_out = memsize_available_para + 0x10 /* PSP */;  /* Minimum would be 0x1000 paras (65536 bytes), including PSP. */
  }

  /* https://github.com/svn2github/dosbox/blob/acd380bcde72db74f3b476253899016f686bc0ef/src/dos/dos_execute.cpp#L501-L506 */
  *(unsigned short*)&regs->rax = 0;  /* FreeDOS 1.2 sets AH and AL to 0xff or 0x00 according to some FCB value (fcbcode) (https://github.com/FDOS/kernel/blob/8c8d21311974e3274b3c03306f3113ee77ff2f45/kernel/task.c#L339-L341), but most of the time they end up as 0. */
  *(unsigned short*)&regs->rbx = 0;  /* FreeDOS 1.2 and DOSBox 0.74-4 sets BX the same way as AX, i.e. based on some FCB values. */
  *(unsigned short*)&regs->rcx = 0xff;
  *(unsigned short*)&regs->rdx = psp_para;
  *(unsigned short*)&regs->rsi = *(unsigned short*)&regs->rip;
  *(unsigned short*)&regs->rdi = *(unsigned short*)&regs->rsp;
  /**(unsigned short*)&regs->rsp = ...;*/  /* Set above. */
  *(unsigned short*)&regs->rbp = 0x91c;
  /* EFLAGS https://en.wikipedia.org/wiki/FLAGS_register */
  *(unsigned short*)&regs->rflags = 0x0202;  /* DOSBox 0.74-4 sets it to 0x7202 == (reserved|IF|IOPL3|NT), MS-DOS 6.22 sets it to 0x7246 == (reserved|AF|ZF|IF|IOPL3|NT), FreeDOS 1.2 sets it to 0x0200 == (reserved), but a real 386+ in real mode has IOPL==0 and NT==0 (bits 12..14 clear). Phar Lap DOS extenders detect an 8086 by those bits being set, and report `no 80386' — so we use 0x0202 (reserved|IF). */
  /**(unsigned short*)&regs->rip = ...;*/  /* Set above. */
  /*sregs->cs.selector = ...;*/  /* Set above. */
  sregs->ds.selector = psp_para;  /* Set above. */
  sregs->es.selector = psp_para;  /* Set above. */
  /*sregs->ss.selector = ...;*/  /* Set above. */

  /* https://stanislavs.org/helppc/program_segment_prefix.html */
  *(unsigned short*)(psp + 2) = DOS_MEM_LIMIT >> 4;  /* Top of memory. */
  psp[5] = (char)0xf4;  /* hlt instruction; this is machine code to jump to the CP/M dispatcher. */
  *(unsigned short*)(psp + 0x2c) = ENV_PARA;
  *(unsigned short*)(psp) = 0x20cd;  /* `int 0x20' opcode. */
  *(unsigned short*)(psp + 0x40) = 5;  /* DOS version number (DOSBox also reports 5). */
  *(unsigned short*)(psp + 0x50) = 0x21cd;  /* `int 0x21' opcode. */
  *(unsigned short*)(psp + 0x32) = 20;  /* `Number of bytes in JFT. */
  *(unsigned*)(psp + 0x34) = 0x18 | psp_para << 16;  /* `Far pointer to JFT. */
  *(unsigned*)(psp + 0x38) = 0xffffffffU;  /* `Pointer to (lack of) previous PSP. */
  *(unsigned*)(psp + 0x0a) = *((unsigned*)mem + 0x22);  /* Copy of `int 0x22' vector. Program terminate address. Not an interrupt. */
  *(unsigned*)(psp + 0x0e) = *((unsigned*)mem + 0x23);  /* Copy of `int 0x23' vector. Ctrl-<Break> handler address. Not an interrupt.  */
  *(unsigned*)(psp + 0x12) = *((unsigned*)mem + 0x24);  /* Copy of `int 0x24' vector. Critical error handler. Do not execute directly (why?). */
  psp[0x52] = (char)0xcb;  /* `retf' opcode. */
  psp[5] = (char)0x9a;  /* Opcode for `call far segment:offset'. */
  /* These are the PSP fields we don't fill (but keep as 0 as returned by mmap MAP_ANONYMOUS):
   * 0x18 20 bytes  file handle array (Undocumented DOS 2.x+); if handle array element is FF then handle is available. Network redirectors often indicate remotes files by setting these to values between 80-FE. DOS 2+ Job File Table JFT), one byte per file handle, FFh = closed. https://en.wikipedia.org/wiki/Job_File_Table
   * 0x2e dword     SS:SP on entry to last INT 21 call (Undoc. 2.x+)
   * 0x38 dword     pointer to previous PSP (default FFFF:FFFF, Undoc. 3.x+), used by SHARE in DOS 3.3
   * 0x3c byte      DOS 4+ (DBCS) interim console flag (see AX=6301h) Novell DOS 7 DBCS interim flag as set with AX=6301h (possibly also used by Far East MS-DOS 3.2-3.3)
   * 0x3d byte      (APPEND) TrueName flag (see INT 2F/AX=B711h)
   * 0x3e byte      (Novell NetWare) flag: next byte initialized if CEh (OS/2) capabilities flag
   * 0x3f byte      (Novell NetWare) Novell task number if previous byte is CEh
   * 0x42 word      (MSWindows3) selector of next PSP (PDB) in linked list Windows keeps a linked list of Windows programs only
   * 0x44 word      (MSWindows3) "PDB_Partition"
   * 0x46 word      (MSWindows3) "PDB_NextPDB"
   * 0x5c 36 bytes  default unopened FCB #1 (parts overlayed by FCB #2)
   * 0x6c 20 bytes  default unopened FCB #2 (overlays part of FCB #1) (overwritten if FCB 1 is opened)
   */
  return psp;
}

int load_dos_overlay_program(int img_fd, const char *filename, void *mem, const char *header, int header_size, unsigned short load_para, unsigned short reloc_para) {
  if (header_size >= 24 && (('M' | 'Z' << 8) == *(const unsigned short*)header || ('M' << 8 | 'Z') == *(const unsigned short*)header)) {
    const unsigned short * const exehdr = (const unsigned short*)header;
    const unsigned short nblocks = exehdr[EXE_NBLOCKS] & 0x7ff;
    const unsigned exesize = exehdr[EXE_LASTSIZE] ? ((nblocks - 1) << 9) + exehdr[EXE_LASTSIZE] : nblocks << 9;
    const unsigned headsize = (unsigned)exehdr[EXE_HDRSIZE] << 4;
    const unsigned image_size = exesize - headsize;
    const unsigned image_linear = (unsigned)load_para << 4;
    unsigned reloc_count = exehdr[EXE_NRELOC];
    if (exehdr[EXE_LASTSIZE] > 0x200 || exesize <= headsize || image_linear + image_size > DOS_MEM_LIMIT) {
      errno = ENOMEM;
      return -1;
    }
    if ((unsigned)lseek(img_fd, headsize, SEEK_SET) != headsize) return -1;
    if ((unsigned)read(img_fd, (char*)mem + image_linear, image_size) != image_size) return -1;
    if (reloc_count) {
      unsigned short reloc[1024];
      if (header_size < 26) { errno = EINVAL; return -1; }
      if ((unsigned)lseek(img_fd, exehdr[EXE_RELOCPOS], SEEK_SET) != exehdr[EXE_RELOCPOS]) return -1;
      while (reloc_count != 0) {
        const unsigned to_read = reloc_count > (sizeof(reloc) >> 2) ? sizeof(reloc) : reloc_count << 2;
        const unsigned got = read(img_fd, reloc, to_read);
        unsigned short *r, *rend;
        if (got != to_read) return -1;
        reloc_count -= got >> 2;
        for (r = reloc, rend = r + (got >> 1); r != rend; r += 2) {
          const unsigned linear = image_linear + ((unsigned)r[1] << 4) + r[0];
          if (linear + 2 > DOS_MEM_LIMIT) { errno = EINVAL; return -1; }
          *(unsigned short*)((char*)mem + linear) += reloc_para;
        }
      }
    }
  } else {
    const unsigned image_linear = (unsigned)load_para << 4;
    struct stat st;
    int got;
    if (fstat(img_fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (unsigned long)st.st_size > (unsigned long)(DOS_MEM_LIMIT - image_linear)) {
      errno = ENOMEM;
      return -1;
    }
    if ((unsigned)lseek(img_fd, 0, SEEK_SET) != 0) return -1;
    got = read(img_fd, (char*)mem + image_linear, st.st_size);
    if (got != st.st_size) return -1;
  }
  (void)filename;
  return 0;
}
