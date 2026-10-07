#include "kvikdos.h"

unsigned g_case_fallback_mode = 2;  /* Best default: all. */

FILE *g_diag_file = NULL;

unsigned g_diag_mask = 0;

FILE *g_io_trace_file = NULL;

int g_io_trace_all = 0;

char *g_mem_dump_filename = NULL;

unsigned long g_mem_dump_start = 0;

unsigned long g_mem_dump_size = 0;

int g_exit_regs = 0;

void close_io_trace_file(void) {
  if (g_io_trace_file != NULL) {
    fclose(g_io_trace_file);
    g_io_trace_file = NULL;
  }
}

void maybe_open_io_trace_file(void) {
  const char *filename = getenv("KVIKDOS_IO_TRACE");
  const char *mode = getenv("KVIKDOS_IO_TRACE_PORTS");
  if (filename == NULL || filename[0] == '\0') return;
  g_io_trace_all = (mode != NULL && 0 == strcmp(mode, "all"));
  g_io_trace_file = fopen(filename, "wb");
  if (g_io_trace_file == NULL) {
    perror("fatal: cannot open KVIKDOS_IO_TRACE");
    exit(1);
  }
  if (atexit(close_io_trace_file) != 0) {
    fprintf(stderr, "fatal: atexit failed for KVIKDOS_IO_TRACE\n");
    exit(1);
  }
}

void maybe_parse_mem_dump(void) {
  const char *filename = getenv("KVIKDOS_MEM_DUMP");
  const char *start = getenv("KVIKDOS_MEM_DUMP_START");
  const char *size = getenv("KVIKDOS_MEM_DUMP_SIZE");
  char *endp;
  if (filename == NULL || filename[0] == '\0') return;
  if (start == NULL || size == NULL || start[0] == '\0' || size[0] == '\0') {
    fprintf(stderr, "fatal: KVIKDOS_MEM_DUMP requires KVIKDOS_MEM_DUMP_START and KVIKDOS_MEM_DUMP_SIZE\n");
    exit(1);
  }
  g_mem_dump_start = strtoul(start, &endp, 0);
  if (*endp != '\0') {
    fprintf(stderr, "fatal: bad KVIKDOS_MEM_DUMP_START: %s\n", start);
    exit(1);
  }
  g_mem_dump_size = strtoul(size, &endp, 0);
  if (*endp != '\0') {
    fprintf(stderr, "fatal: bad KVIKDOS_MEM_DUMP_SIZE: %s\n", size);
    exit(1);
  }
  g_mem_dump_filename = xstrdup(filename);
  if (g_mem_dump_filename == NULL) {
    perror("fatal: xstrdup");
    exit(1);
  }
}

void maybe_parse_exit_regs(void) {
  const char *value = getenv("KVIKDOS_EXIT_REGS");
  g_exit_regs = (value != NULL && value[0] != '\0' && strcmp(value, "0") != 0);
}

void maybe_dump_guest_mem(const void *mem, unsigned mem_size) {
  int fd;
  if (g_mem_dump_filename == NULL) return;
  if (g_mem_dump_start > mem_size || g_mem_dump_size > mem_size - g_mem_dump_start) {
    fprintf(stderr, "fatal: KVIKDOS_MEM_DUMP range out of bounds: start=0x%lx size=0x%lx mem=0x%x\n",
            g_mem_dump_start, g_mem_dump_size, mem_size);
    exit(252);
  }
  fd = open(g_mem_dump_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    perror("fatal: cannot open KVIKDOS_MEM_DUMP");
    exit(252);
  }
  if ((unsigned long)write(fd, (const char*)mem + g_mem_dump_start, g_mem_dump_size) != g_mem_dump_size) {
    perror("fatal: cannot write KVIKDOS_MEM_DUMP");
    close(fd);
    exit(252);
  }
  close(fd);
}

void trace_guest_io(unsigned short port, unsigned char direction, unsigned char size, unsigned char count, const char *data) {
  unsigned i;
  unsigned n;
  unsigned char rec[4];
  if (g_io_trace_file == NULL) return;
  setvbuf(g_io_trace_file, NULL, _IONBF, 0);  /* Keep records visible across kill/crash. */
  if (!g_io_trace_all && !(port == 0x388 || port == 0x389)) return;
  if (size == 0 || count == 0) return;
  n = (unsigned)size * (unsigned)count;
  rec[0] = direction ? 'O' : 'I';
  rec[1] = (unsigned char)port;
  rec[2] = (unsigned char)(port >> 8);
  for (i = 0; i < n; ++i) {
    rec[3] = (unsigned char)data[i];
    if (fwrite(rec, 1, sizeof(rec), g_io_trace_file) != sizeof(rec)) {
      perror("fatal: error writing KVIKDOS_IO_TRACE");
      exit(252);
    }
  }
}

void dump_regs(const char *prefix, const struct kvm_regs *regs, const struct kvm_sregs *sregs) {
#define R16(name) (*(unsigned short*)&regs->r##name)
#define S16(name) (sregs->name.selector)  /* 16 bits. */
  fprintf(g_diag_file, "%s: regs: cs:%04x ip:%04x ax:%04x bx:%04x cx:%04x dx:%04x si:%04x di:%04x sp:%04x bp:%04x flags:%08x ds:%04x es:%04x fs:%04x gs:%04x ss:%04x\n",
          prefix, S16(cs), R16(ip),
          R16(ax), R16(bx), R16(cx), R16(dx), R16(si), R16(di), R16(sp), R16(bp), *(unsigned*)&regs->rflags,
          S16(ds), S16(es), S16(fs), S16(gs), S16(ss));
  if (sregs->cr0 & 1) {  /* Protected mode: also dump 32-bit state, control regs and descriptors. */
    fprintf(g_diag_file, "%s: pm: eip:%08x eax:%08x ebx:%08x ecx:%08x edx:%08x esi:%08x edi:%08x esp:%08x ebp:%08x cr0:%08x cr2:%08x cr3:%08x cr4:%08x\n",
            prefix, (unsigned)regs->rip, (unsigned)regs->rax, (unsigned)regs->rbx, (unsigned)regs->rcx, (unsigned)regs->rdx,
            (unsigned)regs->rsi, (unsigned)regs->rdi, (unsigned)regs->rsp, (unsigned)regs->rbp,
            (unsigned)sregs->cr0, (unsigned)sregs->cr2, (unsigned)sregs->cr3, (unsigned)sregs->cr4);
    fprintf(g_diag_file, "%s: pm: cs{base:%08x lim:%08x db:%d g:%d} ss{base:%08x lim:%08x db:%d g:%d} ds{base:%08x lim:%08x} es{base:%08x lim:%08x}\n",
            prefix,
            (unsigned)sregs->cs.base, (unsigned)sregs->cs.limit, sregs->cs.db, sregs->cs.g,
            (unsigned)sregs->ss.base, (unsigned)sregs->ss.limit, sregs->ss.db, sregs->ss.g,
            (unsigned)sregs->ds.base, (unsigned)sregs->ds.limit,
            (unsigned)sregs->es.base, (unsigned)sregs->es.limit);
  }
  fflush(stdout);
}

void copy_args_to_dos_args(char *p, const char* const *args) {
  unsigned size = 1;
  while (*args) {
    const char *arg = *args++;
    const unsigned arg_size = strlen(arg);
    if (size + arg_size < size || size + arg_size > 127) {
      fprintf(stderr, "fatal: DOS command line args too long\n");
      exit(252);
    }
    p[size++] = ' ';  /* Initial space compatible with MS-DOS. */
    memcpy(p + size, arg, arg_size);
    size += arg_size;
  }
  p[size] = '\r';
  *p = --size;
}

unsigned short get_dos_error_code(int le, unsigned short default_code) {
  /* https://stanislavs.org/helppc/dos_error_codes.html */
  return le == ENOENT ? 2  /* File not found. */
       : le == EACCES ? 5  /* Access denied. */
       : le == EBADF ? 6  /* Invalid handle. */
       : default_code;  /* Example: 0x1f: General failure. */
}
