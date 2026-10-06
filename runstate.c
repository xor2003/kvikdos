#include "kvikdos.h"
#include "intrun.h"


/* Shared DOS run state (was locals of run_dos_prog; see intrun.h). */
struct EmuState *emu;
struct kvm_fds kvm_fds;
void *mem;
struct kvm_run *run;
struct kvm_regs regs;
struct kvm_sregs sregs;
DirState *dir_state;
TtyState *tty_state;
const EmuParams *emu_params;
int img_fd;
const char *load_prog;
const char *load_args_str;
const char* const *load_args;
unsigned load_psp_para;
char dpmi_host_active;
char preserve_low;
char header[PROGRAM_HEADER_SIZE];
unsigned header_size;
const char *dos_prog_abs;
const char *g_prog_filename;
const char *g_prog_args_str;
const char* const *g_prog_args;
char dpmi_linux_buf[LINUX_PATH_SIZE];
char target_prog_buf[LINUX_PATH_SIZE];
unsigned int_num;
unsigned short int_cs, int_ip;
unsigned short *csip_ptr;
unsigned char ah;
char had_get_ints;
unsigned char tasm30_bitset;
unsigned tick_count;
unsigned char sphinx_cmm_flags;
char ctrl_break_checking;
unsigned dta_seg_ofs;
unsigned ongoing_set_int;
unsigned short last_dos_error_code;
char port_0x40_tick;
char port_0x92_a20;
char port_0x70_index;
unsigned char video_write_step;
char video_byte_written;
const char *stdout_write_p;
const char *stdout_write_end;
char is_stdout_write_cursor;
char dpmi_warned;
unsigned long xms_block_kb[XMS_HANDLE_COUNT];
unsigned short xms_block_sizes_kb[XMS_HANDLE_COUNT];
unsigned short xms_lock_counts[XMS_HANDLE_COUNT];
unsigned long xms_free_kb;
char umb_link_state;
unsigned short ems_pages_by_handle[EMS_HANDLE_COUNT];
unsigned short ems_handle_base[EMS_HANDLE_COUNT];
unsigned short ems_page_map[4];
unsigned short ems_phys_handle[4];
unsigned short ems_free_pages, ems_total_pages, ems_pool_next;
unsigned short call_hlt_cs, call_hlt_ip;
unsigned malloc_strategy;
char cleanup_fn[16];
DIR *find_dirp;
char find_linux_dir[LINUX_PATH_SIZE];
char find_dos_pattern[13];
unsigned short find_attrs;
unsigned char last_exec_return_code;
unsigned hlt_spin_count;
unsigned fcb_lin_table[FCB_FILE_COUNT];
int fcb_fd_table[FCB_FILE_COUNT];
unsigned current_psp_para;
unsigned vid_tick;

char exec_fnbuf[LINUX_PATH_SIZE];  /* Used temporarily by run_dos_prog. */


/* do_exit: flush the final video frame, close findfirst state and emit the
 * requested guest-memory dump, then return the DOS exit code to the host. */
unsigned char dos_exit(void) {
  if (vid_active) vid_render(mem);  /* Flush the final frame before the screen is torn down. */
  if (find_dirp) { closedir(find_dirp); find_dirp = NULL; }
  if (g_exit_regs) {
    struct kvm_sregs sr;
    if (ioctl(kvm_fds.vcpu_fd, KVM_GET_SREGS, &sr) == 0)
      fprintf(stderr, "info: exit regs cs=%04x ds=%04x es=%04x ss=%04x ip=%04x sp=%04x ax=%04x cr0=%lx cr2=%lx cr3=%lx\n",
              sregs.cs.selector, sregs.ds.selector, sregs.es.selector, sregs.ss.selector,
              (unsigned short)regs.rip, (unsigned short)regs.rsp, (unsigned short)regs.rax,
              (unsigned long)sr.cr0, (unsigned long)sr.cr2, (unsigned long)sr.cr3);
  }
  maybe_dump_guest_mem(mem, GUEST_MEM_LIMIT);
  /* Optional: dump all guest RAM (low + xmem) to a file for offline analysis. */
  if (getenv("KVIKDOS_DUMPALL")) {
    int fd = open(getenv("KVIKDOS_DUMPALL"), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
      if (write(fd, mem, GUEST_MEM_LIMIT) < 0) perror("dump mem");
      if (emu->xmem && write(fd, emu->xmem, emu->xmem_size) < 0) perror("dump xmem");
      close(fd);
      fprintf(stderr, "dumped %u low + %lu xmem bytes\n", GUEST_MEM_LIMIT, (unsigned long)emu->xmem_size);
    }
  }
  return (unsigned char)regs.rax;
}

