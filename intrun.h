#ifndef INTRUN_H
#define INTRUN_H

#include "kvikdos.h"

/* Internal shared state for the DOS run loop and its per-interrupt service
 * handlers. These were locals of run_dos_prog(); run_dos_prog() is not
 * re-entrant (program exec goes through a separate subprocess and overlays use
 * the in-place do_exec reload), so a single shared instance is safe.
 */

enum { XMS_HANDLE_COUNT = 64 };
/* KiB reserved just below the top of int15/88-visible extended memory: DOS
 * extenders park their resident kernel + page tables there and rely on XMS not
 * handing out those physical pages. */
enum { XMS_KERNEL_RESERVE_KB = 1024 };
enum { EMS_HANDLE_COUNT = 64 };
enum { FCB_FILE_COUNT = 32 };
enum malloc_strategy_t { MS_FIRST_FIT = 0, MS_BEST_FIT = 1, MS_LAST_FIT = 2 };

/* Update a segment register's selector and recompute its base. */
#define FIX_SREG(name) do { sregs.name.base = sregs.name.selector << 4; } while(0)
#define SET_SREG(name, value) do { sregs.name.base = (sregs.name.selector = (value)) << 4; } while(0)

/* Action codes returned by the int_*_dispatch() service handlers, consumed by
 * the KVM run loop in run.c. */
enum {
  IA_NEXT,       /* Return from the interrupt and continue the guest (done_int_call). */
  IA_EXEC,       /* Reload the program image (do_exec). */
  IA_EXIT,       /* Terminate the guest; regs.rax holds the DOS exit code (do_exit). */
  IA_FATAL,      /* Fatal error (goto fatal). */
  IA_FATAL_INT,  /* Unexpected/unsupported interrupt (fatal_int label logic). */
  IA_FATAL_UIC   /* Unimplemented call (fatal_uic label logic). */
};

extern struct EmuState *emu;
extern struct hv *hv;
extern void *mem;
extern struct hv_exit hx;
extern struct kvm_regs regs;
extern struct kvm_sregs sregs;
extern DirState *dir_state;
extern TtyState *tty_state;
extern const EmuParams *emu_params;

/* Program loading state (do_exec reload point). */
extern int img_fd;
extern const char *load_prog;
extern const char *load_args_str;
extern const char* const *load_args;
extern unsigned load_psp_para;
extern char dpmi_host_active;
extern char preserve_low;
extern char header[PROGRAM_HEADER_SIZE];
extern unsigned header_size;
extern const char *dos_prog_abs;
extern const char *g_prog_filename;
extern const char *g_prog_args_str;
extern const char* const *g_prog_args;
extern char dpmi_linux_buf[LINUX_PATH_SIZE];
extern char target_prog_buf[LINUX_PATH_SIZE];

/* Int-call context captured at each magic-interrupt HLT. */
extern unsigned int_num;
extern unsigned short int_cs, int_ip;
extern unsigned short *csip_ptr;
extern unsigned char ah;

/* Persistent DOS/dispatch state. */
extern char had_get_ints;
extern unsigned char tasm30_bitset;
extern unsigned tick_count;
extern unsigned char sphinx_cmm_flags;
extern char ctrl_break_checking;
extern unsigned dta_seg_ofs;
extern unsigned ongoing_set_int;
extern unsigned short last_dos_error_code;
extern char port_0x40_tick;
extern char port_0x92_a20;
extern char port_0x70_index;
extern char port_0x3b8, port_0x3d8, port_0x3d9;
extern unsigned char pic_isr, pic_imr;
extern char port_0x61;
extern char port_crtc_index;
extern char crtc_regs[0x20];
extern unsigned char video_write_step;
extern char video_byte_written;
extern const char *stdout_write_p;
extern const char *stdout_write_end;
extern char is_stdout_write_cursor;
extern char dpmi_warned;
extern unsigned long xms_block_kb[XMS_HANDLE_COUNT];
extern unsigned short xms_block_sizes_kb[XMS_HANDLE_COUNT];
extern unsigned short xms_lock_counts[XMS_HANDLE_COUNT];
extern unsigned long xms_free_kb;
extern char umb_link_state;
extern unsigned short ems_pages_by_handle[EMS_HANDLE_COUNT];
extern unsigned short ems_handle_base[EMS_HANDLE_COUNT];
extern unsigned short ems_page_map[4];
extern unsigned short ems_phys_handle[4];
extern unsigned short ems_free_pages, ems_total_pages, ems_pool_next;
extern unsigned short call_hlt_cs, call_hlt_ip;
extern unsigned malloc_strategy;
extern char cleanup_fn[16];
extern DIR *find_dirp;
extern char find_linux_dir[LINUX_PATH_SIZE];
extern char find_dos_pattern[13];
extern unsigned short find_attrs;
extern unsigned char last_exec_return_code;
extern unsigned hlt_spin_count;
extern unsigned fcb_lin_table[FCB_FILE_COUNT];
extern int fcb_fd_table[FCB_FILE_COUNT];
extern unsigned current_psp_para;
extern unsigned vid_tick;

/* Shared helpers used by several int handlers. */
void emit_stdout(void);            /* Write stdout_write_p..stdout_write_end (and paint). */
unsigned char dos_exit(void);      /* do_exit: cleanup, returns regs.rax exit code. */
int int_dispatch(void);            /* Route int_num to its handler; returns IA_*. */
int io_dispatch(void);             /* KVM_EXIT_IO port I/O; returns IA_*. */
int mmio_dispatch(void);           /* KVM_EXIT_MMIO memory access; returns IA_*. */

/* Per-interrupt handlers (each returns an IA_* code). */
int int21_dispatch(void);
int int10_dispatch(void);
int int1a_dispatch(void);
int int16_dispatch(void);
int int2a_dispatch(void);
int int11_dispatch(void);
int int09_dispatch(void);
int int17_dispatch(void);
int int2f_dispatch(void);
int int15_dispatch(void);
int int67_dispatch(void);
int int43_dispatch(void);
int int29_dispatch(void);
int int20_dispatch(void);
int int22_dispatch(void);
int int0d_dispatch(void);
int int00_dispatch(void);
int int03_dispatch(void);

/* int 21h sub-handlers by service family (each returns an IA_* code). */
int i21_con(void);   /* Console input/output. */
int i21_open(void);  /* Open/create to handle. */
int i21_io(void);    /* Handle I/O: read/write/close/dup/seek/ioctl. */
int i21_mem(void);   /* Memory block alloc/free/resize/strategy. */
int i21_fcb(void);   /* FCB file functions. */
int i21_find(void);  /* findfirst/findnext. */
int i21_exec(void);  /* exec/exit/terminate-stay-resident. */
int i21_misc(void);  /* Remaining DOS services. */

/* Shared DOS-error helpers used by the int 21h sub-handlers. */
int dos_error_21(void);   /* error_on_21: record AX, set CF, log. */
int dos_err_ax(unsigned short code);  /* Set AX=code then dos_error_21. */
int dos_err_linux(void);  /* errno->DOS code then dos_error_21. */
int dos_unknown21(void);  /* nonfatal_unknown_int_21_call: AL=0, CF=1. */

#endif
