#include "kvikdos.h"
#include "intrun.h"

/* DOS int 21h services: open group. Returns an IA_* action. */
int i21_open(void) {
  if (ah == 0x3d || ah == 0x3c) {
  /* Open to handle (open()). Create to handle (creat()). */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const int flags = (ah == 0x3c) ? O_RDWR | O_CREAT | O_TRUNC :
                *(unsigned char*)&regs.rax & 3;  /* O_RDONLY == 0, O_WRONLY == 1, O_RDWR == 2 same in DOS and Linux. */
            const unsigned char flags3 = (flags & 3);
            /* For create, CX contains attributes (read-only, hidden, system, archive), we just ignore it.
             * https://stanislavs.org/helppc/file_attributes.html
             */
            int fd;
            const char *linux_filename;
            char *linux_lastc;  /* Last component of linux_filename. */
            if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: dos_open(%s) flags=0x%x\n", p, flags);
            dir_state->dos_prog_abs = flags3 == O_RDONLY ? dos_prog_abs : NULL;  /* For loading the overlay from g_prog_filename, even if not mounted. */
            linux_filename = get_linux_filename_r(p, dir_state, fnbuf, &linux_lastc);
            dir_state->dos_prog_abs = NULL;  /* For security. */
            if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: dos_open(%s) linux_filename=(%s) current_drive=%c:\n", p, linux_filename, dir_state->drive);
            /* There is some code duplication here with "type" in run_dos_batch(). */
            /* Since we check linux_lastc rather than linux_filename, we
             * recognize foo\aux.bar as aux. DOSBox 0.74-4 and MS-DOS 6.22
             * do the same, but they also fail if directory foo doesn't exist.
             */
            if (is_same_ascii_nocase(linux_lastc, "nul", 3) && (linux_lastc[3] == '.' || linux_lastc[3] == '\0')) {
              strcpy(fnbuf, "/dev/null");
            } else if (is_same_ascii_nocase(linux_lastc, "aux", 3) && (linux_lastc[3] == '\0' || linux_lastc[3] == '.')) {
              if (flags3 != O_WRONLY) { /* Don't let the user open aux for non-writing. This is for (partial) comaptibility with `pts-fast-dosbox. */

                *(unsigned short*)&regs.rax = 5;  /* Access denied. */
                return dos_error_21();
              } else {
                if ((fd = dup(2)) < 0) return dos_err_linux();
              }
              goto after_open;
            } else if ((is_same_ascii_nocase(linux_lastc, "con", 3) && (linux_lastc[3] == '\0' || linux_lastc[3] == '.')) ||
                       (is_same_ascii_nocase(linux_lastc, "prn", 3) && (linux_lastc[3] == '\0' || linux_lastc[3] == '.')) ||
                       (is_same_ascii_nocase(linux_lastc, "lpt1", 4) && (linux_lastc[4] == '\0' || linux_lastc[4] == '.'))) {
              if (flags3 == O_RDONLY) {
                if ((fd = dup(0)) < 0) return dos_err_linux();
              } else if (flags3 == O_WRONLY) {
                if ((fd = dup(1)) < 0) return dos_err_linux();
              } else {
                return dos_err_ax(5);  /* Don't let the user open prn for both reading and writing. This is for (partial) comaptibility with `pts-fast-dosbox. */
              }
              goto after_open;
            }
            if ((fd = open_with_case_fallback(linux_filename, flags, 0644)) < 0) {
              if (fd < 0) { 
              *(unsigned short*)&regs.rax = get_dos_error_code(errno, 0x1f);  /* By default: General failure. */
              return dos_error_21();
              }
            }
            /*dup2(fd, 20); close(fd); fd = 20;*/  /* This breaks .exe files created by `owcc -bdos', which allows fd < 20. We fix it with map_fd_open(...) below. */
           after_open:
            if (fd < 5) fd = ensure_fd_is_at_least(fd, 5);  /* Skip the first 5 DOS standard handles. */
            fd = map_fd_open(fd);
            if ((fd + 0U) >> 16) {
              *(unsigned short*)&regs.rax = 4;  /* Too many open files. */
              return dos_error_21();
            }
            if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: dos_open(%s) dos_fd=%d\n", p, fd);
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            *(unsigned short*)&regs.rax = fd;
  }   else if (ah == 0x6c) {
  /* Extended open/create (DOS 4.0+). */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rsi);  /* DS:SI filename. */
            const unsigned short bx = *(unsigned short*)&regs.rbx;  /* Open mode. */
            const unsigned short dx = *(unsigned short*)&regs.rdx;  /* Action flags in low nibble. */
            const unsigned action = dx & 0x0f;
            const int flags3 = bx & 3;  /* O_RDONLY/O_WRONLY/O_RDWR mapping. */
            int flags = flags3, fd;
            struct stat st;
            const char *linux_filename;
            char *linux_lastc;
            int exists;
            int action_taken = 0;  /* 1=open existing, 2=create new. */
            dir_state->dos_prog_abs = flags3 == O_RDONLY ? dos_prog_abs : NULL;
            linux_filename = get_linux_filename_r(p, dir_state, fnbuf, &linux_lastc);
            dir_state->dos_prog_abs = NULL;
            if (is_same_ascii_nocase(linux_lastc, "nul", 3) && (linux_lastc[3] == '.' || linux_lastc[3] == '\0')) strcpy(fnbuf, "/dev/null");
            exists = stat(linux_filename, &st) == 0;
            if (!exists && errno == ENOENT && g_case_fallback_mode == 2 && linux_filename[0] &&
                resolve_case_fallback_path(linux_filename, fnbuf2, sizeof(fnbuf2), 0) &&
                stat(fnbuf2, &st) == 0) {
              linux_filename = fnbuf2;
              exists = 1;
            }
            if (action == 2) {  /* Create new, fail if exists. */
              if (exists) { *(unsigned short*)&regs.rax = 0x50; return dos_error_21(); }  /* File exists. */
              flags |= O_CREAT | O_EXCL;
              action_taken = 2;
            } else if (action == 1) {  /* Open existing, fail if not exists. */
              if (!exists) { *(unsigned short*)&regs.rax = 2; return dos_error_21(); }  /* File not found. */
              action_taken = 1;
            } else if (action == 3) {  /* Open if exists else create. */
              if (exists) action_taken = 1;
              else { flags |= O_CREAT; action_taken = 2; }
            } else {  /* Default/probe mode: open existing, fail if missing. */
              if (!exists) { *(unsigned short*)&regs.rax = 2; return dos_error_21(); }
              action_taken = 1;
            }
            fd = open_with_case_fallback(linux_filename, flags, 0644);
            if (fd < 0) return dos_err_linux();
            if (fd < 5) fd = ensure_fd_is_at_least(fd, 5);
            fd = map_fd_open(fd);
            if ((fd + 0U) >> 16) {
              *(unsigned short*)&regs.rax = 4;  /* Too many open files. */
              return dos_error_21();
            }
            *(unsigned short*)&regs.rax = fd;
            *(unsigned short*)&regs.rcx = action_taken;
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  } 
  return IA_NEXT;
}
