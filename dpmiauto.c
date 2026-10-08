#include "kvikdos.h"

/* Auto-detect a resident DPMI host for programs that look like DPMI clients,
 * so that --dpmi= is usually unnecessary. If the program's own image mentions
 * DPMI or an extender name (client stubs always reference them) and a known
 * host executable is reachable through the DOS filesystem, return its DOS
 * pathname — run_dos_prog() then loads it first, as if --dpmi= was given.
 * Programs that are themselves hosts are excluded by basename. */

/* Substrings (lowercase) searched in the first bytes of the program image. */
static const char * const dpmi_client_markers[] = {
  "dpmi", "rtm.exe", "32rtm", "dpmi16bi", "pmodew", "causeway",
  "dos/4g", "dos4gw", "cwsdpmi", "hdpmi", NULL
};

/* Basename substrings: the program is itself a host/loader — never auto-load. */
static const char * const host_name_excludes[] = {
  "cwsdpmi", "hdpmi", "dpmires", "pmodew", "dos4gw", "dpmiload",
  "dpmiinst", "dpmi16bi", "dpmi32vm", "windpmi", "rtmres", "rtm.",
  "32rtm", "dpmi.", NULL
};

/* Known resident DPMI hosts, in preference order. */
static const char * const dpmi_host_names[] = {
  "hdpmi32.exe", "cwsdpmi.exe", "hdpmi.exe", "dpmires.exe",
  "32rtm.exe", "rtm.exe", "pmodew.exe", "cwsdpr0.exe", NULL
};

#define DPMI_SCAN_SIZE 32768  /* DPMI client stubs put their strings up front. */

static char host_dos_buf[DOS_PATH_SIZE];
static char probe_dos_buf[DOS_PATH_SIZE];
static char probe_linux_buf[LINUX_PATH_SIZE];

static int name_contains_nocase(const char *name, const char *needle) {
  /* `needle' must already be lowercase. */
  for (; *name; ++name) {
    const char *n = name, *d = needle;
    for (; *d && (*n | 32) == *d; ++n, ++d) {}
    if (*d == '\0') return 1;
  }
  return 0;
}

static int is_dpmi_host_name(const char *prog_filename) {
  const char *base = prog_filename, *p;
  int i;
  for (p = prog_filename; *p; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  for (i = 0; host_name_excludes[i]; ++i) {
    if (name_contains_nocase(base, host_name_excludes[i])) return 1;
  }
  return 0;
}

static int image_mentions_dpmi(const char *prog_filename) {
  static char scanbuf[DPMI_SCAN_SIZE];
  int fd, i, hit = 0;
  ssize_t got;
  fd = open(prog_filename, O_RDONLY);
  if (fd < 0) return 0;
  got = read(fd, scanbuf, sizeof(scanbuf));
  close(fd);
  if (got < 4) return 0;
  { ssize_t k; for (k = 0; k < got; ++k) scanbuf[k] = (char)((unsigned char)scanbuf[k] | 32); }  /* Lowercase ASCII letters in place. */
  for (i = 0; dpmi_client_markers[i]; ++i) {
    if (my_memmem(scanbuf, (size_t)got, dpmi_client_markers[i], strlen(dpmi_client_markers[i]))) { hit = 1; break; }
  }
  return hit;
}

const char *auto_dpmi_host(const char *prog_filename, const DirState *dir_state,
                           const char *dos_path, int force) {
  const char *prog_dir, *dir_end, *name, *lin, *r;
  char drive;
  int i;
  if (!prog_filename || !*prog_filename) return NULL;
  if (is_dpmi_host_name(prog_filename)) return NULL;
  if (!force && !image_mentions_dpmi(prog_filename)) return NULL;

  for (i = 0; dpmi_host_names[i]; ++i) {
    name = dpmi_host_names[i];
    /* Beside the program (its DOS directory). */
    prog_dir = dir_state->dos_prog_abs;
    if (prog_dir && *prog_dir) {
      dir_end = prog_dir + strlen(prog_dir);
      for (; dir_end != prog_dir && dir_end[-1] != '\\' && dir_end[-1] != '/'; --dir_end) {}
      if ((size_t)(dir_end - prog_dir) + strlen(name) + 1 <= sizeof(probe_dos_buf)) {
        char *q = probe_dos_buf;
        const char *s;
        for (s = prog_dir; s != dir_end;) *q++ = *s++;
        for (s = name; (*q++ = *s++) != '\0';) {}
        lin = get_linux_filename_r(probe_dos_buf, dir_state, probe_linux_buf, NULL);
        if (*lin && access(lin, R_OK) == 0) {
          r = get_dos_abs_filename_r(lin, '\0', dir_state, host_dos_buf);
          if (*r) return r;
        }
      }
    }
    /* On the DOS PATH (find_prog_on_path also tries the cwd). */
    lin = find_prog_on_path(name, dir_state, dos_path, &drive);
    if (lin && *lin) {
      r = get_dos_abs_filename_r(lin, drive, dir_state, host_dos_buf);
      if (*r) return r;
    }
    /* In the emulator's own directory (D: mount). */
    if (strlen(name) + 4 <= sizeof(probe_dos_buf)) {
      memcpy(probe_dos_buf, "D:\\", 3);
      strcpy(probe_dos_buf + 3, name);
      lin = get_linux_filename_r(probe_dos_buf, dir_state, probe_linux_buf, NULL);
      if (*lin && access(lin, R_OK) == 0) {
        r = get_dos_abs_filename_r(lin, '\0', dir_state, host_dos_buf);
        if (*r) return r;
      }
    }
  }
  return NULL;
}
