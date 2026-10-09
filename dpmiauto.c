#include "kvikdos.h"
#include "hdpmibin.h"  /* embedded_dpmi_host[] — HDPMI32.EXE from HX (freeware). */

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
static char host_tmp_buf[LINUX_PATH_SIZE];
static char hx_kit_dir[LINUX_PATH_SIZE];

static int write_if_missing(const char *path, const unsigned char *data, unsigned size) {
  int fd;
  if (access(path, R_OK) == 0) return 0;  /* Same content every run — reuse it. */
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd < 0) return -1;
  if (write(fd, data, size) != (ssize_t)size) {
    close(fd);
    unlink(path);
    return -1;
  }
  return close(fd);
}

static int make_kit_dir(char *out, size_t out_size, const char *name) {
  const char *dir = getenv("TMPDIR");
  if (!dir || !*dir) dir = "/tmp";
  if (snprintf(out, out_size, "%s/%s-%u", dir, name, (unsigned)getuid()) <= 0) return -1;
  if (access(out, R_OK) != 0 && mkdir(out, 0755) != 0) return -1;
  return 0;
}

/* out = "<dir>/<name>" with bounded concat. */
static char *join_path(char *out, size_t out_size, const char *dir, const char *name) {
  size_t n = strlen(dir);
  if (n + 1 + strlen(name) + 1 > out_size) { *out = '\0'; return out; }
  memcpy(out, dir, n);
  out[n] = '/';
  strcpy(out + n + 1, name);
  return out;
}

/* Drop the embedded HDPMI32 next to the user's temp files so it can be
 * loaded like an on-disk host. Deterministic per-user name: the file is
 * written once and reused, no accumulation. Returns a Linux path — fine,
 * run_dos_prog() accepts those for the host too. */
static const char *extract_embedded_host(void) {
  if (make_kit_dir(hx_kit_dir, sizeof(hx_kit_dir), "kvikdos-dpmi") != 0) return NULL;
  join_path(host_tmp_buf, sizeof(host_tmp_buf), hx_kit_dir, "HDPMI32.EXE");
  if (write_if_missing(host_tmp_buf, embedded_dpmi_host, embedded_dpmi_host_size) != 0) return NULL;
  return host_tmp_buf;
}

/* Extract the whole embedded HX kit (HDPMI32 + DPMILD32 + DKRNL32.DLL) to a
 * per-user temp dir and return its Linux path, or NULL. The caller mounts the
 * dir as a DOS drive so the guest can see DPMILD32.EXE and DKRNL32.DLL. */
const char *hx_ensure_kit(void) {
  if (make_kit_dir(hx_kit_dir, sizeof(hx_kit_dir), "kvikdos-hx") != 0) return NULL;
  join_path(host_tmp_buf, sizeof(host_tmp_buf), hx_kit_dir, "HDPMI32.EXE");
  if (write_if_missing(host_tmp_buf, embedded_dpmi_host, embedded_dpmi_host_size) != 0) return NULL;
  join_path(host_tmp_buf, sizeof(host_tmp_buf), hx_kit_dir, "DPMILD32.EXE");
  if (write_if_missing(host_tmp_buf, embedded_dpmild32, embedded_dpmild32_size) != 0) return NULL;
  join_path(host_tmp_buf, sizeof(host_tmp_buf), hx_kit_dir, "DKRNL32.DLL");
  if (write_if_missing(host_tmp_buf, embedded_dkrnl32, embedded_dkrnl32_size) != 0) return NULL;
  return hx_kit_dir;
}

/* PE subsystem is at e_lfanew+92 in the optional header; 3 = console. */
static int pe_is_console(const char *path) {
  unsigned char hdr[96];
  unsigned long lf;
  int fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  if (read(fd, hdr, 64) != 64) { close(fd); return 0; }
  lf = (unsigned long)hdr[0x3c] | ((unsigned long)hdr[0x3d] << 8) |
       ((unsigned long)hdr[0x3e] << 16) | ((unsigned long)hdr[0x3f] << 24);
  if (lseek(fd, (off_t)lf, SEEK_SET) != (off_t)lf ||
      read(fd, hdr, sizeof(hdr)) != sizeof(hdr)) { close(fd); return 0; }
  close(fd);
  return hdr[0] == 'P' && hdr[1] == 'E' && hdr[2] == 0 && hdr[3] == 0 &&
         hdr[24 + 68] == 3 && hdr[24 + 69] == 0;
}

/* Turn `kvikdos app.exe' on a PE32 console binary into
 * `HDPMI32 resident + DPMILD32.EXE <dos-path-of-app> <args>' using the
 * extracted HX kit mounted on a spare DOS drive. Returns 1 when ready. */
int setup_hx_pe_run(ParsedCmdArgs *cmd) {
  static const char *hx_args[64];
  static char loader_path[LINUX_PATH_SIZE], kit_dos[DOS_PATH_SIZE];
  const char *kit, *pe_dos;
  char drv, kit_drv;
  const char *base, *p;
  int n;
  if (cmd->emu_params.dpmi_off || !pe_is_console(cmd->prog_filename)) return 0;
  pe_dos = cmd->dir_state.dos_prog_abs;
  if (!pe_dos || !*pe_dos || pe_dos[1] != ':') return 0;
  kit = hx_ensure_kit();
  if (!kit) return 0;
  /* Mount the kit dir so the guest sees DPMILD32.EXE and DKRNL32.DLL.
   * Prefer the last drive (H:) downwards to stay out of the user's way;
   * C:..D: are typically user/emulator mounts already. */
  for (kit_drv = 'A' + DRIVE_COUNT - 1; kit_drv > 'D' &&
       cmd->dir_state.linux_mount_dir[kit_drv - 'A']; --kit_drv) {}
  if (kit_drv <= 'D') return 0;  /* E:..H: all taken — no room for the kit. */
  join_path(loader_path, sizeof(loader_path), kit, "DPMILD32.EXE");
  if (!*loader_path) return 0;
  { static char kit_slash[LINUX_PATH_SIZE];
    size_t kn = strlen(kit);
    if (kn + 2 > sizeof(kit_slash)) return 0;
    memcpy(kit_slash, kit, kn);
    kit_slash[kn] = '/';
    kit_slash[kn + 1] = '\0';
    cmd->dir_state.linux_mount_dir[kit_drv - 'A'] = kit_slash;
  }
  cmd->dir_state.case_mode[kit_drv - 'A'] = CASE_MODE_UPPERCASE;
  snprintf(kit_dos, sizeof(kit_dos), "%c:\\DPMILD32.EXE", kit_drv);
  /* DPMILD32 gets the target PE's DOS path plus the original args. */
  hx_args[0] = pe_dos;
  for (n = 1; cmd->args[n - 1] && n < 63; ++n) hx_args[n] = cmd->args[n - 1];
  hx_args[n] = NULL;
  cmd->args = hx_args;
  /* DOS CWD = the PE's directory so relative file args resolve. */
  drv = pe_dos[0] & ~32;
  base = pe_dos + strlen(pe_dos);
  for (p = pe_dos + 3; base > p && base[-1] != '\\' && base[-1] != '/'; --base) {}
  if (base > p) {
    static char cwd_buf[DOS_PATH_SIZE];
    size_t m = (size_t)(base - p);
    if (m >= sizeof(cwd_buf)) m = sizeof(cwd_buf) - 1;
    memcpy(cwd_buf, p, m);
    if (m > 0 && cwd_buf[m - 1] != '\\') cwd_buf[m++] = '\\';
    cwd_buf[m] = '\0';
    copy_cstr0(cmd->dir_state.current_dir[(int)(drv - 'A')],
               sizeof(cmd->dir_state.current_dir[(int)(drv - 'A')]), cwd_buf);
    cmd->dir_state.drive = drv;
  }
  cmd->dir_state.dos_prog_abs = kit_dos;
  if (!cmd->dpmi_prog) {  /* An explicit --dpmi= stays in charge. */
    static char host_path[LINUX_PATH_SIZE];
    join_path(host_path, sizeof(host_path), kit, "HDPMI32.EXE");
    cmd->dpmi_prog = host_path;  /* A Linux path — run_dos_prog() takes it. */
  }
  cmd->prog_filename = loader_path;  /* .../DPMILD32.EXE */
  return 1;
}

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
  return extract_embedded_host();  /* Last resort: the bundled HDPMI32. */
}
