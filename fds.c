#include "kvikdos.h"

int mapped_handles[20 - 5];

int map_fd_open(int fd) {
  int *p, *pend;
  if (fd < 5) return fd;
  p = mapped_handles;
  pend = mapped_handles + sizeof(mapped_handles) / sizeof(mapped_handles[0]);
  for (; p != pend; ++p) {
    if (!*p) {
      *p = fd;
      return p - mapped_handles + 5;
    }
  }
  return fd + 5 + sizeof(mapped_handles) / sizeof(mapped_handles[0]);
}

void map_handle_close(unsigned short handle) {
  if (handle < 5 || handle >= 5 + sizeof(mapped_handles) / sizeof(mapped_handles[0])) return;
  mapped_handles[handle - 5] = 0;  /* Mark it as available. */
}

int get_linux_fd(unsigned short handle) {
  extern struct hv *hv;  /* From intrun.h (not visible through kvikdos.h). */
  int hv_fds[3];
  const int hv_fd_count = hv ? hv_get_fds(hv, hv_fds, 3) : 0;
  /* Redirection (`./kvikdos prog >prog.out') just works and redirects DOS
   * STDOUT (not DOS STDERR), and because of the conditions below, STDPRN as
   * well. This matches the behavior of `pts-fast-dosbox noscreenprn'. In
   * MS-DOS 6.22, STDAUX and STDPRN redirect to nothing by default. In both
   * DOSBox and MS-DOS 6.22, running `prog >prog.out' in the DOS command line
   * redirects DOS STDOUT only (not DOS STDERR or others).
   */
  int fd;
  if (handle < 5) {
    return handle == 3 ? 2  /* Emulate STDAUX with stderr. */
         : handle == 4 ? 1  /* Emulate STDPRN with stdout. */
         : handle;
  }
  fd = handle >= 5 + sizeof(mapped_handles) / sizeof(mapped_handles[0]) ? (int)(handle - (5 + sizeof(mapped_handles) / sizeof(mapped_handles[0]))) :
      mapped_handles[handle - 5] ? mapped_handles[handle - 5] : -1;
  { int i;
    for (i = 0; i < hv_fd_count; ++i) {  /* Disallow hypervisor fds from DOS for security. */
      if (fd == hv_fds[i]) return -1;
    }
  }
  return fd;
}

int open_dos_file(const char *dos_filename, const char *dos_prog_abs, int flags, DirState *dir_state) {
  const int flags3 = (flags & 3);
  int fd;
  const char *linux_filename;
  char *linux_lastc;  /* Last component of linux_filename. */
  dir_state->dos_prog_abs = flags3 == O_RDONLY ? dos_prog_abs : NULL;  /* For loading the overlay from prog_filename, even if not mounted. */
  linux_filename = get_linux_filename_r(dos_filename, dir_state, fnbuf, &linux_lastc);
  dir_state->dos_prog_abs = NULL;  /* For security. */
  /* There is some code duplication here with open() in run_dos_prog(). */
  if (is_same_ascii_nocase(linux_lastc, "nul", 3) && (linux_lastc[3] == '.' || linux_lastc[3] == '\0')) {
    strcpy(fnbuf, "/dev/null");
  } else if (is_same_ascii_nocase(linux_lastc, "aux", 3) && (linux_lastc[3] == '\0' || linux_lastc[3] == '.')) {
    if (flags3 != O_WRONLY) { eacces: /* Don't let the user open aux for non-writing. This is for (partial) comaptibility with `pts-fast-dosbox. */
      errno = EACCES; return -1;  /* Permission denied. */
    } else {
      if ((fd = dup(2)) < 0) { einval: errno = EINVAL; return -1; }
    }
    goto after_open;
  } else if ((is_same_ascii_nocase(linux_lastc, "con", 3) && (linux_lastc[3] == '\0' || linux_lastc[3] == '.')) ||
             (is_same_ascii_nocase(linux_lastc, "prn", 3) && (linux_lastc[3] == '\0' || linux_lastc[3] == '.')) ||
             (is_same_ascii_nocase(linux_lastc, "lpt1", 4) && (linux_lastc[4] == '\0' || linux_lastc[4] == '.'))) {
    if (flags3 == O_RDONLY) {
      if ((fd = dup(0)) < 0) goto einval;
    } else if (flags3 == O_WRONLY) {
      if ((fd = dup(1)) < 0) goto einval;
    } else {
      goto eacces;  /* Don't let the user open prn for both reading and writing. This is for (partial) comaptibility with `pts-fast-dosbox. */
    }
    goto after_open;
  }
  if ((fd = open_with_case_fallback(linux_filename, flags, 0644)) < 0) {
    if (flags3 == O_RDONLY && dos_prog_abs && dos_prog_abs[0] &&
        strchr(dos_filename, ':') == NULL && strchr(dos_filename, '\\') == NULL && strchr(dos_filename, '/') == NULL) {
      /* Fallback: resolve bare filename in program directory as well.
       * Needed by tools that expect argv-relative files next to the executable.
       */
      const char *base = dos_prog_abs + strlen(dos_prog_abs);
      char ovl_dos[260];
      size_t dir_size;
      for (; base != dos_prog_abs + 3 && base[-1] != '\\' && base[-1] != '/'; --base) {}
      dir_size = (size_t)(base - dos_prog_abs);
      if (dir_size > 3 && dir_size + strlen(dos_filename) + 1 < sizeof(ovl_dos)) {
        memcpy(ovl_dos, dos_prog_abs, dir_size);
        strcpy(ovl_dos + dir_size, dos_filename);
        dir_state->dos_prog_abs = dos_prog_abs;
        linux_filename = get_linux_filename_r(ovl_dos, dir_state, fnbuf2, &linux_lastc);
        dir_state->dos_prog_abs = NULL;
        if ((fd = open_with_case_fallback(linux_filename, flags, 0644)) >= 0) goto after_open;
      }
    }
    return -1;
  }
 after_open:
  return fd;
}
