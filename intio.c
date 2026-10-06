#include "kvikdos.h"
#include "intrun.h"

/* DOS int 21h services: io group. Returns an IA_* action. */
int i21_io(void) {
  if (ah == 0x40) {
  /* Write using handle or truncate. */
            const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
            if (fd < 0) {

              *(unsigned short*)&regs.rax = 6;  /* Invalid handle. */

              { last_dos_error_code = *(unsigned short*)&regs.rax;
                if (last_dos_error_code > 0x12) *(unsigned short*)&regs.rax = 0x0d;  /* Invalid data. Use int 0x21 call with ah == 0x59 to get the real error. */
              }
              *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
              if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: int 0x21 call error\n");
            } else {
              const char *p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
              const int size = (int)*(unsigned short*)&regs.rcx;
              int got;
              if (size == 0) {  /* Truncate. */
                const int got1 = lseek(fd, 0, SEEK_CUR);
                if (got1 < 0) {
                  if (errno == ESPIPE) {  /* Without this hack, A86 4.05 falls to an infinite loop displaying `Sorry, output failed'. */
                    struct stat st;
                    if (fstat(fd, &st) == 0 && !S_ISREG(st.st_mode)) { got = 0; goto write_success; }  /* Typically, it's isatty(fd). */
                  }
                  got = got1;
                  goto write_fault;
                } else {
                  if ((got = ftruncate(fd, got1)) != 0) goto write_fault;
                }
              } else {
                got = write(fd, p, size);
                if (got < 0) { write_fault:  /* errno may not be valid now, fstat(2) after lseek(3) failure may have reset it. */
                  *(unsigned short*)&regs.rax = 0x1d;  /* Write fault. */
                  return dos_error_21();
                }
              }
             write_success:
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              *(unsigned short*)&regs.rax = got;
            }
  }   else if (ah == 0x3f) {
  /* Read using handle. */
            const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
            if (fd < 0) {
              return dos_err_ax(6);
            } else {
              char *p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
              const int size = (int)*(unsigned short*)&regs.rcx;
              const int got = read(fd, p, size);
              if (got < 0) {
                *(unsigned short*)&regs.rax = 0x1e;  /* Read fault. */
                return dos_error_21();
              }
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              *(unsigned short*)&regs.rax = got;
            }
  }   else if (ah == 0x57) {
  /* Get/set file date and time using handle. */
            const unsigned char al = (unsigned char)regs.rax;
            if (al < 2) {
              const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
              if (fd < 0) return dos_err_ax(6);
              if (al == 0) {  /* Get. */
                struct stat st;
                struct tm *tm;
                if (fstat(fd, &st) != 0) return dos_err_linux();
                tm = localtime(&st.st_mtime);
                *(unsigned short*)&regs.rcx = tm->tm_sec >> 1 | tm->tm_min << 5 | tm->tm_hour << 11;
                *(unsigned short*)&regs.rdx = tm->tm_mday | (tm->tm_mon + 1) << 5 | (tm->tm_year - 1980) << 9;
                tasm30_bitset |= 0x80;
              } else {  /* Set if al == 1. */
                const time_t ts = dos_datetime_to_time(*(unsigned short*)&regs.rdx, *(unsigned short*)&regs.rcx);
                struct timespec tv[2];
                tv[0].tv_sec = ts; tv[0].tv_nsec = 0;
                tv[1].tv_sec = ts; tv[1].tv_nsec = 0;
                if (futimens(fd, tv) != 0) return dos_err_linux();
              }
            } else { 
              *(unsigned short*)&regs.rax = 0x57;  /* Invalid parameter. */
              return dos_error_21();
            }
  }   else if (ah == 0x3e) {
  /* Close using handle. */
            const unsigned short handle = *(unsigned short*)&regs.rbx;
            if (handle >= 5) {  /* Don't close the standard handles, just pretend. */
              const int fd = get_linux_fd(handle, &kvm_fds);
              if (fd < 0) return dos_err_ax(6);  /* Not strictly needed, close(...) would check. */
              map_handle_close(handle);
              if (close(fd) != 0) return dos_err_linux();
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x45) {
  /* Duplicate handle (dup()). */
            const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
            int fd2;
            if (fd < 0) return dos_err_ax(6);
            fd2 = dup(fd);
            if (fd2 < 0) {
              *(unsigned short*)&regs.rax = get_dos_error_code(errno, 4);  /* By default: Too many open files. */
              return dos_error_21();
            }
            if (fd2 < 5) fd2 = ensure_fd_is_at_least(fd2, 5);  /* Skip the first 5 DOS standard handles. */
            fd2 = map_fd_open(fd2);
            if ((fd2 + 0U) >> 16) {
              *(unsigned short*)&regs.rax = 4;  /* Too many open files. */
              return dos_error_21();
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            *(unsigned short*)&regs.rax = fd2;
  }   else if (ah == 0x46) {
  /* Force duplicate handle (dup2()). */
            const unsigned short src_handle = *(unsigned short*)&regs.rbx;
            const unsigned short dst_handle = *(unsigned short*)&regs.rcx;
            const int src_fd = get_linux_fd(src_handle, &kvm_fds);
            if (src_fd < 0) return dos_err_ax(6);
            if (src_handle != dst_handle) {
              if (dst_handle < 5) {
                const int dst_fd = get_linux_fd(dst_handle, &kvm_fds);
                if (dst_fd < 0 || dup2(src_fd, dst_fd) != dst_fd) return dos_err_linux();
              } else if (dst_handle < 5 + sizeof(mapped_handles) / sizeof(mapped_handles[0])) {
                const unsigned dst_idx = dst_handle - 5;
                const int old_fd = mapped_handles[dst_idx];
                int new_fd = dup(src_fd);
                if (new_fd < 0) {
                  *(unsigned short*)&regs.rax = get_dos_error_code(errno, 4);  /* By default: Too many open files. */
                  return dos_error_21();
                }
                if (new_fd < 5) new_fd = ensure_fd_is_at_least(new_fd, 5);
                if (old_fd > 0) close(old_fd);
                mapped_handles[dst_idx] = new_fd;
              } else {
                const int dst_fd = (int)(dst_handle - (5 + sizeof(mapped_handles) / sizeof(mapped_handles[0])));
                if (dst_fd == kvm_fds.kvm_fd || dst_fd == kvm_fds.vm_fd || dst_fd == kvm_fds.vcpu_fd) return dos_err_ax(6);
                if (dup2(src_fd, dst_fd) != dst_fd) return dos_err_linux();
              }
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            *(unsigned short*)&regs.rax = dst_handle;
  }   else if (ah == 0x42) {
  /* Seek using handle. */
            const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
            if (fd < 0) return dos_err_ax(6);
            {
              const unsigned whence = *(unsigned char*)&regs.rax;  /* SEEK_SET == 0, SEEK_CUR == 1, SEEK_END == 2, same in DOS and Linux. */
              const int offset = *(unsigned short*)&regs.rcx << 16 | *(unsigned short*)&regs.rdx;  /* It's important that this is signed, because we may want to pass -1 to lseek() even on 64-bit systems. */
              int got;
              if (whence > 2) return dos_err_ax(0x57);
              got = lseek(fd, offset, whence);
              if (got < 0) {
                *(unsigned short*)&regs.rax = 0x19;  /* Seek error. (Is this the relevant code?) */
                return dos_error_21();
              }
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              *(unsigned short*)&regs.rdx = (unsigned)got >> 16;
              *(unsigned short*)&regs.rax = got;
            }
  }   else if (ah == 0x44) {
    /* I/O control (ioctl). */
              const unsigned char al = (unsigned char)regs.rax;
              char ioctl_ok = 1;
              if (al == 1 && (*(unsigned short*)&regs.rdx >> 8)) return dos_err_ax(0x57);
              if (al < 2) {  /* Get device information (1), set device information (2). */
                const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
                struct stat st;
                if (fd < 0) return dos_err_ax(6);
                if (fstat(fd, &st) != 0) return dos_err_linux();
                if (al == 0) {  /* Get. */
                  /* DOSBox 0.74-4 PRN: 0x80a0; DOSBox 0.74-4 CON: 0x80d3; DOSBox 0.74-4 file on drive C: 0x0002. */
                  static const char fake_drive = 'C';
                  /* Without the 1 << 15 bit, the VAL 1995-05-27 linker val.exe fprintf(stdout, ...) function wouldn't write anything to stdout. DOSBox 0.74-4 doesn't set 1 << 15 on regular files. */
                  *(unsigned short*)&regs.rdx = S_ISCHR(st.st_mode) ? 1 << 15 /* reserved */ | 1 << 5  /* binary */ | 1 << 7  /* character device */ : 1 << 15 | (fake_drive - 'A') /* regular file on block device */;
                  if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: ioctl get_device_info dos_fd=%d linux_fd=%d result=0x%04x\n", *(unsigned short*)&regs.rbx, fd, *(unsigned short*)&regs.rdx);
                  *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
                } else {
                  if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: ioctl get_device_info dos_fd=%d linux_fd=%d value=0x%04x\n", *(unsigned short*)&regs.rbx, fd, *(unsigned short*)&regs.rdx);
                  if (!S_ISCHR(st.st_mode)) return dos_err_ax(0xf);  /* We want to indicate that it's not a character device. */
                  /* TLIB 3.01 sets (dx & 0x80) to zero, to disable binary mode (and enable translation). */
                  /* We just ignore the setting. */
                }
              } else if (al == 8) {  /* Get whether drive is removable. */
                unsigned char bl = (unsigned char)regs.rax;
                if (bl == 0) bl = dir_state->drive - 'A' + 1;
                if (bl > DRIVE_COUNT || !dir_state->linux_mount_dir[(int)bl - 1]) return dos_err_ax(0xf);
                *(unsigned char*)&regs.rax = bl > 2;  /* A: (1) and B: (2) are removable (0), C: (3) etc. aren't (1). */
              } else if (al == 0x0a) {  /* Get whether handle is local or remote. */
                const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
                if (fd < 0) return dos_err_ax(6);
                *(unsigned short*)&regs.rdx = 0;  /* Drive is local. */
              } else if (al == 0x06) {  /* Get input status. */
                const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
                struct stat st;
                if (fd < 0) return dos_err_ax(6);
                if (fstat(fd, &st) != 0) return dos_err_linux();
                if (S_ISREG(st.st_mode)) {
                  *(unsigned short*)&regs.rax = 0xff;  /* Regular files are ready. */
                } else {
                  struct pollfd pfd;
                  int pr;
                  pfd.fd = fd;
                  pfd.events = POLLIN;
                  pfd.revents = 0;
                  pr = poll(&pfd, 1, 0);
                  if (pr < 0) return dos_err_linux();
                  *(unsigned short*)&regs.rax = (pr > 0 && (pfd.revents & (POLLIN | POLLHUP))) ? 0xff : 0x00;
                }
  #if 0
              } else if (al == 6) {
                const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
                if (fd < 0) return dos_err_ax(6);
  #if 0
                *(unsigned short*)&regs.rax = 0xd;  /* Invalid data. */
                return dos_error_21();
  #endif
                *(unsigned short*)&regs.rax = 0xff;  /* Input is ready (0xff). */
  #endif
              } else {
                if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "debug: unsupported DOS ioctl call ignored: call=0x%02x dos_fd=%d\n", al, *(unsigned short*)&regs.rbx);
                ioctl_ok = 0;
              }
              if (ioctl_ok) *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              else return dos_unknown21();
  }   else if (ah == 0x68 || ah == 0x6a) {
  /* Commit file (fflush). 0x6a is a DOS 4.x alias. */
            const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
            if (fd < 0) return dos_err_ax(6);
            if (fsync(fd) != 0 && errno != EINVAL) return dos_err_linux();  /* EINVAL: not a regular file (e.g. tty). */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  } 
  return IA_NEXT;
}
