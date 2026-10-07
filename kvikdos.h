#ifndef KVIKDOS_H
#define KVIKDOS_H
#define _GNU_SOURCE 1  /* For MAP_ANONYMOUS and memmem(). Must precede all includes. */
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>  /* For gettimeofday(2). */
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define DIAG_BIT_COMPAT   0x01u  /* compat: unsupported-feature/fallback notes */
#define DIAG_BIT_EXEC     0x02u  /* exec: program exec/exit */
#define DIAG_BIT_INT      0x04u  /* int: interrupt calls and vector get/set */
#define DIAG_BIT_FS       0x08u  /* fs: filesystem and path operations */
#define DIAG_BIT_VERBOSE  0x10u  /* verbose: malloc/MCB/register tracing (all only) */
#define DIAG_ON(bit) (g_diag_mask & (unsigned)(bit))
#if defined(USE_MINI_KVM) || defined(__COSMOPOLITAN__)
/* USE_MINI_KVM for systems with a broken linux/kvm.h; cosmopolitan has none
 * at all (its headers are host-neutral), and the vendored ABI decls in
 * mini_kvm.h double as the universal regs/sregs POD format for all backends. */
#  include "mini_kvm.h"
#else
#  include <linux/kvm.h>
#endif
#include "hv.h"  /* Hypervisor backend abstraction (KVM on Linux, WHPX on Windows). */
#ifndef DEBUG
#define DEBUG 0
#endif
#ifndef DEBUG_ALLOC
#define DEBUG_ALLOC 0
#endif
#ifndef DEBUG_INT
#define DEBUG_INT 0
#endif
#ifndef DEBUG_INTVEC
#define DEBUG_INTVEC 0
#endif
#ifndef DEBUG_EXEC
#define DEBUG_EXEC 0
#endif
#define CMD_PARSE_DEBUG DEBUG
#define CMD_PARSE_MEM_MB_1 1
#ifndef CMD_PARSE_DEBUG
#define CMD_PARSE_DEBUG 0
#endif
#ifndef CMD_PARSE_MEM_MB_1
#define CMD_PARSE_MEM_MB_1 0
#endif
#define CASE_MODE_UPPERCASE 0
#define CASE_MODE_LOWERCASE 1
#define CASE_MODE_UNSPECIFIED 2
#define DRIVE_COUNT 8
#define PFT_LINUX 0
#define PFT_DOS 1
#define PFT_PATH 2
#define DOS_PATH_SIZE 64  /* See int 0x21 ah == 0x47 (get current directory) */
#define LINUX_PATH_SIZE 1024
#if 0  /* We don't use ROM and BIOS area and then XMS above DOS_MEM_LIMIT, we just map DOS_MEM_LIMIT. */
#define MEM_SIZE (2 << 20)  /* In bytes. 2 MiB. */
#endif
#define INT_HLT_PARA 0x54
#define INVARS_LIN 0x670  /* Linear address of dos_info_t.first_dpb. int 21h AH=52h returns ES:BX = 0x66:0x10, so dos_info_t spans 0x64a..0x6df, between the int stubs (0x540-0x63f) and the environment (0x700). */
#define PSP_PARA 0x100
#define PROGRAM_MCB_PARA (PSP_PARA - 1)
#define ENV_PARA 0x70
#define ENV_LIMIT (PROGRAM_MCB_PARA << 4)
#define GUEST_MEM_MODULE_START 0x1000
#define MAGIC_INT_VALUE(int_num) ((unsigned)INT_HLT_PARA << 16 | (unsigned)int_num)
#define DOS_MEM_LIMIT 0xa0000
#define GUEST_MEM_LIMIT 0xc0000
#define DOS_ALLOC_PARA_LIMIT 0xa000
#define MAX_DOS_COM_SIZE 0xfef0
#define PROGRAM_HEADER_SIZE 26  /* Large enough for .exe header (prefix of 26 bytes) and other header detection. */
#define is_linear_byte_user_writable(linear) ((linear) - (ENV_PARA << 4) < (DOS_MEM_LIMIT - (ENV_PARA << 4)))
#define FINDFIRST_MAGIC 0xd5ba1ad0U
#define PROCESS_ID  0x192  /* Same as in DOSBox. */
#define MCB_TYPE(mcb) (*(char*)(mcb))  /* 'Z' indicates last member of MCB chain; 'M' would be non-last. */
#define MCB_PID(mcb) (*(unsigned short*)((char*)(mcb) + 1)) /* 0 indicates free block, PROCESS_ID indicates used block. */
#define MCB_SIZE_PARA(mcb) (*(unsigned short*)((char*)(mcb) + 3))  /* Block size in paragraphs (excluding MCB), must be at least 1 in kvikdos. */
#define MCB_PSIZE_PARA(mcb) (*(unsigned short*)((char*)(mcb) + 5))  /* Size of previous block (excluding MCB), or 0 if this is the first block. This is a kvikdos-specific field, DOS doesn't specify it. */
#if DEBUG || DEBUG_ALLOC
#define DEBUG_CHECK_ALL_MCBS(mem) check_all_mcbs(mem)
#else
#define DEBUG_CHECK_ALL_MCBS(mem) do {} while (0)
#endif
#if !defined(__GLIBC__) && !defined(__UCLIBC__)
#if 0  /* Test. */
#endif
#define memmem my_memmem
#endif
#define EXE_SIGNATURE 0
#define EXE_LASTSIZE 1
#define EXE_NBLOCKS 2
#define EXE_NRELOC 3
#define EXE_HDRSIZE 4
#define EXE_MINALLOC 5
#define EXE_MAXALLOC 6
#define EXE_SS 7
#define EXE_SP 8
#define EXE_CHECKSUM 9  /* Ignored by kvikdos. */
#define EXE_IP 10
#define EXE_CS 11
#define EXE_RELOCPOS 12
#define EXE_NOVERLAY 13  /* Ignored and not even loaded by kvikdos. */
#define get_linux_filename(p) get_linux_filename_r((p), dir_state, fnbuf, NULL)
#if 0  /* Currently unused. */
#endif
#define VID_BASE   0xb8000
#define VID_COLS   80
#define VID_ROWS   25
#define VID_BUFSZ  (VID_COLS * VID_ROWS * 2)
#define VID_PAGE_STRIDE 0x1000  /* BIOS page size for 80x25 (regen word 0x44c). */
#define VID_PAGES  8            /* 8 pages fit in 0xb8000..0xc0000. */

typedef struct DirState {
  char drive;  /* 'A', 'B', 'C', 'D', ... ('A' + DRIVE_COUNT - 1). */
  char current_dir[DRIVE_COUNT][64];  /* In DOS syntax. Ends with \, unless empty. If current_dir[2] is FOO\BAR\, then it corresponds to C:\FOO\BAR. */
  const char *linux_mount_dir[DRIVE_COUNT];  /* Linux directory to which the specific drive has been mounted, with '/' suffix (or empty), or NULL. Owned externally. linux_mount_dir[2] == "/tmp/foo/" maps DOS path C:\MY\FILE.TXT to Linux path /tmp/foo/MY/FILE.TXT .  */
  char case_mode[DRIVE_COUNT];  /* CASE_MODE_... indicating how letters in DOS filename characters should be converted to Linux (uppercase or lowercase). CASE_MODE_UPPERCASE (0) is the default. We could also call it case_fold. */
  const char *dos_prog_abs;  /* DOS absolute pathname of the program being run. Externally owned, can be NULL. */
  const char *linux_prog;  /* Linux pathname of the program being run. Externally owned, can be NULL. */
} DirState;

typedef struct EmuParams {
  char is_hlt_ok;
  unsigned mem_mb;
  const char *hlt_dump_filename;
  const char *diag_filename;
  unsigned diag_mask;
  unsigned case_fallback_mode;  /* 0=off 1=prog 2=all */
  char strict_mode;  /* 0=permissive 1=strict */
  char batch_cd_root_mode;  /* 0=legacy, 1=interpret `cd \foo' as drive-root absolute. */
  char call_near_enabled;
  unsigned short call_near_ip;
  char call_far_enabled;
  unsigned short call_far_seg;
  unsigned short call_far_ip;
  char call_ss_enabled;
  unsigned short call_ss;
  unsigned short call_sp;
  char call_cs_enabled;
  unsigned short call_cs;
  char call_ds_enabled;
  unsigned short call_ds;
  unsigned short call_args[16];
  unsigned call_arg_count;
  unsigned short call_set_regs[7];  /* ax,cx,dx,bx,si,di,bp */
  unsigned call_set_mask;
  unsigned short poke_word_segs[1024];
  unsigned short poke_word_ofs[1024];
  unsigned short poke_word_values[1024];
  unsigned poke_word_count;
} EmuParams;

typedef struct ParsedCmdArgs {
  const char *prog_filename;
  EmuParams emu_params;
  DirState dir_state;
  int tty_in_fd;
  const char* const *args;  /* NULL-terminated list of NUL-terminated strings. Overlaps the program main(...) argv. */
  const char* const *envp0;  /* NULL-terminated list of NUL-terminated strings. Overlaps the program main(...) argv. */
  const char *extra_env[128];
  unsigned extra_env_count;
  const char *dpmi_prog;
  char force_dos;
} ParsedCmdArgs;

typedef struct TtyState {
  int tty_in_fd;
  char is_tty_in_error;
  const unsigned short *next_fake_key;
  int pending_key;  /* Decoded BIOS keycode buffered by a key-check call; -1 = none. */
  char raw_on;      /* Nonzero: persistent raw tty mode while text mode is active. */
} TtyState;

typedef struct EmuState {
  struct hv *hv;  /* Hypervisor backend (KVM on Linux, WHPX on Windows). */
  struct kvm_sregs initial_sregs;
  void *mem;
  void *xmem;  /* Extended memory: guest physical [0x100000, 0x100000 + xmem_size). */
  unsigned long xmem_size;  /* Bytes. 0 if --mem-mb=1. */
  void *bios_rom;  /* Read-only BIOS ROM page mapped at 0xf0000-0xfffff. */
  char *ems_pool;  /* Host backing store for the EMS page frame, lazily allocated. */
  unsigned ems_pool_pages;
} EmuState;

enum { K_UP, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END, K_PGUP, K_PGDN, K_INS, K_DEL,
       K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12 };

enum mz_subformat_t { MZ_SUBFMT_NONE = 0, MZ_SUBFMT_PE, MZ_SUBFMT_NE, MZ_SUBFMT_LE, MZ_SUBFMT_LX };



extern unsigned g_case_fallback_mode;
extern FILE *g_diag_file;
extern unsigned g_diag_mask;
extern FILE *g_io_trace_file;
extern int g_io_trace_all;
extern char *g_mem_dump_filename;
extern unsigned long g_mem_dump_start;
extern unsigned long g_mem_dump_size;
extern int g_exit_regs;
extern char fnbuf[LINUX_PATH_SIZE], fnbuf2[LINUX_PATH_SIZE], argv0_fnbuf[LINUX_PATH_SIZE];
extern char dosfnbuf[DOS_PATH_SIZE];
extern const char default_program_mcb[16];
extern const char freed_mcb[16];
extern int mapped_handles[20 - 5];
extern char exec_fnbuf[LINUX_PATH_SIZE];  /* Used temporarily by run_dos_prog. */
extern char exec_tail_buf[0x80];  /* Command tail for an in-VM exec child. */
extern char linux_prog_buf[LINUX_PATH_SIZE];
extern unsigned short load_env_para;          /* PSP env_seg override; 0 = default ENV_PARA. */
extern unsigned short load_block_limit_para;  /* Cap on the child's block; 0 = rest of conventional memory. */
/* Text-mode renderer state shared between video.c (rendering) and tty.c (raw
 * tty takeover when text mode is active). */
extern char vid_active;
extern char vid_blink;
extern char vid_wrap_pend;
extern int vid_cur_shape;
extern int vid_tty_fd;
extern char vid_raw_taken;
extern struct termios vid_saved_tio;

char *xstrdup(const char *s);
void copy_cstr0(char *dst, size_t dst_size, const char *src);
const char *skip_dot_slash(const char *p);
void remove_duplicate_slashes(char *p);
char is_same_ascii_nocase(const char *a, const char *b, unsigned size);
void *my_memmem(const void *haystack, size_t haystacklen,
                       const void *needle, size_t needlelen);
const char *get_linux_ext(const char *p);
int ensure_fd_is_at_least(int fd, int min_fd);
char get_dos_filename_drive(const char *p, const DirState *dir_state);
void fcb_filename(const char *fcb, char dos_default_drive, char *out, unsigned out_size);
unsigned short dos_fat_date(const struct tm *tm);
unsigned short dos_fat_time(const struct tm *tm);
const char *get_dos_basename(const char *fn);
const char *get_linux_basename(const char *fn);
char upper_ascii(char c);
char has_dos_ext_nocase(const char *fn, const char *ext);
time_t dos_datetime_to_time(unsigned short dos_date, unsigned short dos_time);
char dos_wildcard_match(const char *pattern, const char *name);
char is_dos_filename_83(const char *fn);
void close_io_trace_file(void);
void maybe_open_io_trace_file(void);
void maybe_parse_mem_dump(void);
void maybe_parse_exit_regs(void);
void maybe_dump_guest_mem(const void *mem, unsigned mem_size);
void trace_guest_io(unsigned short port, unsigned char direction, unsigned char size, unsigned char count, const char *data);
void dump_regs(const char *prefix, const struct kvm_regs *regs, const struct kvm_sregs *sregs);
void copy_args_to_dos_args(char *p, const char* const *args);
unsigned short get_dos_error_code(int le, unsigned short default_code);
int resolve_case_fallback_path(const char *in_path, char *out_path, size_t out_size, int keep_last_case);
int open_with_case_fallback(const char *linux_filename, int flags, mode_t mode);
int stat_with_case_fallback(const char *linux_filename, struct stat *st, int keep_last_case);
DIR *opendir_with_case_fallback(const char *linux_dir, char *resolved_out, size_t resolved_out_size);
const char *getenv_prefix(const char *name_prefix, const char **env, const char **env_end);
const char *getenv_prefix_nocase0(const char *name_prefix, const char *const *env);
const char *getenv_prefix_block_nocase(const char *name_prefix, const char *env, const char *env_end);
char get_case_mode_from_last_component(const char *p);
char detect_prog_filename_type(const char *prog_filename);
void case_fold_on_drive(char *p, char drive, const DirState *dir_state);
const char *get_dos_abs_filename_r(const char *p, char drive, const DirState *dir_state, char *out_buf);
char *get_linux_filename_r(const char *p, const DirState *dir_state, char *out_buf, char **out_lastc_out);
char *find_prog_on_path(const char *prog_filename, const DirState *dir_state, const char *dos_path, char *drive_out);
void dos_normalize_abspath(const char *p, const DirState *dir_state, char *out, unsigned out_size);
char *add_env(char *env, char *env_end, const char *var, char do_check);
const char *getenv_dos_prefix(const char *name_prefix, const char *env);
char set_int(unsigned char int_num, unsigned value_seg_ofs, void *mem, char had_get_ints, unsigned char *tasm30_bitset);
/* Working state shared by the parse_args stages (parse_option_loop/finish_args). */
struct ArgsWork {
  struct ParsedCmdArgs cmd;
  const char *argv0, *path_dos_flag, *cwd_dos_flag, *dos_path;
  char *placeholder, *prog_name_arg;
  char **argv, **envp0, **envp;
  char prog_filename_type, dos_prog_drive, is_kvm_check, is_drive_specified;
};
/* Working state shared across the run_dos_batch stages. run_dos_batch is
 * reentrant (nested CALL and .bat execution), so this state travels in a
 * caller-allocated struct rather than in globals. */
struct BatchCtx {
  struct EmuState *emu;
  const char* const *args;
  DirState *dir_state;
  TtyState *tty_state;
  const EmuParams *emu_params;
  const char* const *envp0;
  const char* const *extra_env;
  unsigned extra_env_count;
  unsigned char exit_code;
  int batch_fd, got;
  char buf[4096], *p, *p_line, *q;
  char do_echo;
  char path_override[1024];
  char has_path_override;
  char goto_label[128];
  char have_goto;
  const char *batch_args[10];
  unsigned batch_argc;
  char *batch_env[256];
  unsigned batch_env_count;
  char *setlocal_env[8][256];
  unsigned setlocal_env_count[8];
  unsigned setlocal_depth;
  char batch_eof;
  const char *dos_prog_abs;
  const char *prog_filename;   /* The .bat file (for diagnostics). */
  /* Per-line state (reset each iteration). */
  char c_endarg, do_echo_line;
  char *r, *arg, *endarg;
  unsigned cmd_size;
  char *q_src;
  char rewound;
  char cmdline[4096];
  char pipe_right[4096];
  int pipe_fd, pipe_save_out, pipe_save_in;
  char pipe_stage;
  int saved_stdin, saved_stdout, saved_stderr;
  char has_redir_in, has_redir_out, has_redir_err, append_out, append_err;
  char redir_in[DOS_PATH_SIZE + 4], redir_out[DOS_PATH_SIZE + 4], redir_err[DOS_PATH_SIZE + 4];
  char cleaned[4096];
};
/* batch_process_line result codes. */
#define BL_NEXT 0   /* done_command/next_line: continue with the next line. */
#define BL_EXIT 1   /* `exit' or fatal: leave the batch loop. */
/* batch_dispatch / batch_cmd_* result codes. */
#define DC_DONE    0  /* Command handled; run done_command cleanup. */
#define DC_REPARSE 1  /* `goto reparse_command': re-dispatch (call/if/pipe). */
#define DC_EXIT    2  /* `exit' or fatal: leave the batch loop. */
#define DC_PASS    3  /* Line did not match this group's commands. */
void init_parsed_cmd_args(ParsedCmdArgs *cmd_args, char *placeholder_for_default);
void parse_args(char **argv, struct ParsedCmdArgs *cmd_args_out, const char *pre_msg, const char *usage_extra, const char *post_msg);
void parse_option_loop(struct ArgsWork *w);
void finish_args(struct ArgsWork *w);
void free_extra_env_args(ParsedCmdArgs *cmd_args);
char is_mcb_bad(void *mem, unsigned short block_para);
void check_all_mcbs(void *mem);
void init_dos_info(void *mem, unsigned long xmem_size);
int detect_dos_executable_program(int img_fd, const char *prog_filename, char *p);
char *load_dos_executable_program(int img_fd, const char *filename, void *mem, const char *header, int header_size, struct kvm_regs *regs, struct kvm_sregs *sregs, unsigned short *block_size_para_out, unsigned psp_para);
int load_dos_overlay_program(int img_fd, const char *filename, void *mem, const char *header, int header_size, unsigned short load_para, unsigned short reloc_para);
enum mz_subformat_t detect_mz_subformat(const char *path);
int file_contains_text(const char *path, const char *needle);
int is_probable_borland_dual_mode_ne(const char *path);
int is_probable_windows_message_stub(const char *path);
int is_probable_dos_extender_program(const char *path);
int map_fd_open(int fd);
void map_handle_close(unsigned short handle);
int get_linux_fd(unsigned short handle);
int open_dos_file(const char *dos_filename, const char *dos_prog_abs, int flags, DirState *dir_state);
void get_dos_abspath_r(const char *p, const DirState *dir_state, char *out_buf, unsigned out_size);
unsigned char run_dos_prog(struct EmuState *emu, const char *prog_filename, const char *dpmi_host, const char *args_str, const char* const *args, DirState *dir_state, TtyState *tty_state, const EmuParams *emu_params, const char* const *envp0, const char* const *extra_env, unsigned extra_env_count);
int tty_getc(TtyState *tty_state, int ms);
/* tty_decode() event returns: >=0 is a BIOS keycode word; KEV_NONE means a
 * consumed event with no guest keycode (mouse report, key release, unknown
 * sequence); KEV_SHIFT means a modifier/lock transition — tty_mods then
 * holds the new BDA 0x417 byte for the caller to apply. */
#define KEV_NONE  (-1)
#define KEV_SHIFT (-2)
extern unsigned tty_mods;  /* BDA 0x417-format modifier+lock bits of the last decoded event. */
int tty_decode(TtyState *tty_state, int c);
void process_key(TtyState *tty_state, unsigned char ah, unsigned short *ax, unsigned short *flags);
void tty_ensure_raw(TtyState *tty_state);
int tty_drain(TtyState *tty_state, void *mem, int bios_push);
/* Nonzero when the guest has hooked int 9 (keyboard IRQ): the IVT entry no
 * longer points to our magic hlt stub.  In that case keys go to the raw
 * scancode ring + an IRQ1 injection instead of the BDA buffer. */
#define guest_int9_hooked(mem) (((const unsigned*)(mem))[9] != MAGIC_INT_VALUE(9))
int tty_wait_key(TtyState *tty_state, int *mods_out);
void kbd_push(void *mem, unsigned key, int mods);
int kbd_pop(void *mem);
int kbd_peek(void *mem);
int tty_raw_pending(void);
int tty_raw_pop(void);
void tty_raw_push(unsigned scan);  /* Feed one byte to the port-0x60 make/break ring (called by ttydec.c). */
int tty_bk_pop(unsigned *key_out, unsigned *mods_out);
int tty_bk_pending(void);
int kbd_can_push(const void *mem);
void kbd_maybe_inject_irq(void);
/* int 33h mouse driver (mouse.c): host pointer events from ttydec.c, the
 * software text cursor for video.c, and per-load reset from vm.c. */
void mouse_reset(void);
void mouse_host_event(int px, int py, int evbtn, int evkind);
void mouse_maybe_call_handler(void);
void mouse_cbk_return(void);
int mouse_cursor_cell(void);
unsigned short mouse_cursor_and(void);
unsigned short mouse_cursor_xor(void);
/* Push a real interrupt frame (FLAGS,CS,IP) on the guest stack and jump to
 * the guest's IVT[n] handler — manual IRQ injection, no irqchip needed.
 * Only inject when IF=1; the ISR's iret returns to the interrupted flow. */
void guest_inject_irq(struct hv *hv, void *mem, unsigned int_no);
void bda_update_ticks(void *mem);
unsigned long bda_ticks_now(void);
void init_tty_state(TtyState *tty_state, int tty_in_fd);
unsigned char cga_to_ansi(unsigned char c);
char *vid_utf8(char *o, unsigned cp);
void vid_term_release(void);
void vid_enter(void);
void vid_render(void *mem);
unsigned char *vid_page(void *mem, unsigned page);
void vid_bda_init(void *mem, unsigned char mode);
void vid_fill(void *mem, unsigned page, int top, int left, int bottom, int right, unsigned char ch, unsigned char attr);
void vid_scroll(void *mem, unsigned page, unsigned char al, unsigned char bh, unsigned short cx, unsigned short dx, int down);
void vid_putc(void *mem, unsigned page, unsigned char ch);
void vid_write_str(void *mem, const char *p, const char *end);
int run_dos_child_subprocess(const char *dos_filename, const char *dos_args, const char *env, const char *env_end, const DirState *dir_state, unsigned char *exit_code_out);
int run_with_wine(const char *prog_filename, const char *const *args, const char *linux_cwd);
int has_wine_in_path(void);
int is_linux_native_executable(const char *path);
int run_native_execvp(const char *prog_filename, const char *const *args);
void init_emu(struct EmuState *emu);
char *guest_ptr(const EmuState *emu, unsigned long gpa);
void reset_emu(struct EmuState *emu, const EmuParams *emu_params);
unsigned char run_dos_batch(struct EmuState *emu, const char *prog_filename, const char* const *args, DirState *dir_state, TtyState *tty_state, const EmuParams *emu_params, const char* const *envp0, const char* const *extra_env, unsigned extra_env_count);
unsigned char batch_process_line(struct BatchCtx *bctx);
int batch_dispatch(struct BatchCtx *bctx);
int batch_cmd_a(struct BatchCtx *bctx);
int batch_cmd_b(struct BatchCtx *bctx);
int batch_cmd_c(struct BatchCtx *bctx);
int main(int argc, char **argv);

#endif
