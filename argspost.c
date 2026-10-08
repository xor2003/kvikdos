#include "kvikdos.h"

void finish_args(struct ArgsWork *w) {
  /* Now: w->argv contains remaining (non-flag) arguments. */
  w->prog_name_arg = *w->argv++;  /* This is a Linux filename. */
  if (w->prog_name_arg) {
  } else if (w->is_kvm_check) {
    --w->argv;
    w->prog_name_arg = "kvmcheck.com";  /* It won't be actually loaded. */
  } else {
    fprintf(stderr, "fatal: missing <dos-executable-file> DOS program filename\n");
    exit(1);
  }
#if 0  /* Tests for replacing the output with "" */
  fprintf(stderr, "GLF (%s)\n", get_linux_filename("C:\\foo\\.\\.\\\\bar\\."));
  fprintf(stderr, "GLF (%s)\n", get_linux_filename(".\\.\\."));
  fprintf(stderr, "GLF (%s)\n", get_linux_filename(".\\aaa\\..\\..\\.."));  /* "" */
  fprintf(stderr, "GLF (%s)\n", get_linux_filename(".\\aaa\\..\\.."));  /* "" */
  fprintf(stderr, "GLF (%s)\n", get_linux_filename(".\\aaa\\.."));
  fprintf(stderr, "GLF (%s)\n", get_linux_filename("C:\\foo\\.\\\\\\.\\bar\\.\\..\\.\\bazzzz\\.."));
#endif
  if (w->path_dos_flag) {
    char *pd;
    size_t n = strlen(w->path_dos_flag);
    pd = (char*)malloc(n + 6);
    if (!pd) { perror("fatal: malloc"); exit(252); }
    memcpy(pd, "PATH=", 5);
    memcpy(pd + 5, w->path_dos_flag, n + 1);
    if (w->cmd.extra_env_count >= sizeof(w->cmd.extra_env) / sizeof(w->cmd.extra_env[0])) {
      fprintf(stderr, "fatal: too many extra env vars\n");
      exit(1);
    }
    w->cmd.extra_env[w->cmd.extra_env_count++] = pd;
  }
  *w->envp = NULL;
  /* Remaining arguments in w->argv will be passed to the DOS program in PSP:0x80. */
  w->dos_path = getenv_prefix("PATH=", (char const**)w->envp0, (char const**)w->envp);
  /* --path-dos feeds bare-name resolution too; find_prog_on_path mutates the
   * string (temporary NULs), so it needs a writable copy. */
  if (!w->dos_path && w->path_dos_flag) w->dos_path = xstrdup(w->path_dos_flag);

  if (w->cmd.dir_state.linux_mount_dir['C' - 'A'] == w->placeholder) {  /* Set to current directory in Linux. */
    w->cmd.dir_state.linux_mount_dir['C' - 'A'] = "";  /* Either --mount=C:. (uppercase) or --mount=C-. (lowercase). */
    /*if (w->cmd.dir_state.case_mode['C' - 'A'] == CASE_MODE_UNSPECIFIED) { ... }*/  /* Will be changed below. */
  }
  if (w->cmd.dir_state.linux_mount_dir['D' - 'A'] == w->placeholder) {  /* Set to emulator directory (based on w->argv0). !! Process readlink(2). */
    const char *p_base = skip_dot_slash(w->argv0), *p = p_base + strlen(p_base);
    size_t size;
    for (; p != p_base && p[-1] != '/'; --p) {}
    if ((size = p - p_base) >= sizeof(argv0_fnbuf)) {
      fprintf(stderr, "fatal: emulator program name (w->argv[0]) too long for mount: %s\n", p_base);
      exit(252);
    }
    memcpy(argv0_fnbuf, p_base, size);  /* Empty or ends with slash. */
    argv0_fnbuf[size] = '\0';
    remove_duplicate_slashes(argv0_fnbuf);
    w->cmd.dir_state.linux_mount_dir['D' - 'A'] = argv0_fnbuf;  /* Either --mount=C:. (uppercase) or --mount=C-. (lowercase). */
    /*if (w->cmd.dir_state.case_mode['D' - 'A'] == CASE_MODE_UNSPECIFIED) { ... }*/  /* Will be changed below. */
  }
  w->dos_prog_drive = '\0';

  w->prog_filename_type = w->is_kvm_check ? PFT_LINUX : detect_prog_filename_type(w->prog_name_arg);
  if (w->prog_filename_type == PFT_LINUX) {
    w->prog_name_arg = (char*)skip_dot_slash(w->prog_name_arg);
    remove_duplicate_slashes(w->prog_name_arg);
    w->cmd.prog_filename = w->prog_name_arg;
    if (w->cmd.dir_state.linux_mount_dir['E' - 'A'] == w->placeholder) {  /* If not explicitly mounted, mount E: to the directory of prog_filename.  */
      const char *p = w->prog_name_arg + strlen(w->prog_name_arg), *q;
      size_t q_size;
      for (; p != w->prog_name_arg && p[-1] != '/'; --p) {}
      for (q = p; *q != '\0' && *q - 'a' + 0U > 'z' - 'a' + 0U; ++q) {}
      if (w->cmd.dir_state.case_mode['E' - 'A'] == CASE_MODE_UNSPECIFIED) {
        w->cmd.dir_state.case_mode['E' - 'A'] = (*q == '\0') ? CASE_MODE_UPPERCASE : CASE_MODE_LOWERCASE;  /* Mount as lowercase iff the executable program name has at least one lowercase character. */
      }
      q = w->prog_name_arg;
      while (q != p && q[0] == '.' && q[1] == '/') {  /* Skip ./ at the beginning. */
        for (q += 2; q != p && q[0] == '/'; ++q) {}
      }
      q_size = strlen(q) + 1;
      if (q_size > sizeof(fnbuf)) {
        fprintf(stderr, "fatal: Linux name of executable program too long: %s\n", q);
        exit(252);
      }
      memcpy(fnbuf, q, q_size);  /* Including the trailing '\0'. */
      w->cmd.prog_filename = fnbuf;
      if (p == q) {  /* Avoid writing read-only memory for --kvm-check. */
        q = "";
      } else {
        if (*p != '\0') *(char*)p = '\0';  /* Modify it in place in w->argv. */
      }
      w->cmd.dir_state.linux_mount_dir['E' - 'A'] = q;  /* Empty or ends with '/'. */
      w->dos_prog_drive = 'E';
      if (!w->is_drive_specified && !w->cmd.dir_state.linux_mount_dir[w->cmd.dir_state.drive - 'A']) w->cmd.dir_state.drive = 'E';
    }
    if (w->cmd.dir_state.case_mode['D' - 'A'] == CASE_MODE_UNSPECIFIED && w->cmd.dir_state.linux_mount_dir['D' - 'A']) {
      w->cmd.dir_state.case_mode['D' - 'A'] = get_case_mode_from_last_component(w->cmd.prog_filename);  /* Mount as lowercase iff the executable program has at least one lowercase character. */
    }
    if (w->is_kvm_check) w->cmd.prog_filename = NULL;
    /* We will set w->cmd.dir_state.case_mode['C' - 'A'] later. */
  } else {
    if (w->cmd.dir_state.linux_mount_dir['E' - 'A'] == w->placeholder) w->cmd.dir_state.linux_mount_dir['E' - 'A'] = NULL;  /* Drive E: not mounted by default. */
    if (w->prog_filename_type == PFT_PATH && !w->dos_path && w->cmd.dir_state.drive == 'C' && w->cmd.dir_state.linux_mount_dir['C' - 'A'] && w->cmd.dir_state.case_mode['C' - 'A'] == CASE_MODE_UNSPECIFIED) {  /* Just a command without a filename extension, e.g. "guest" or "GUEST". */
      w->cmd.dir_state.case_mode['C' - 'A'] = get_case_mode_from_last_component(w->prog_name_arg);
    }
    { char drive;
      for (drive = 'C'; drive <= 'E'; ++drive) {
        if (w->cmd.dir_state.case_mode[drive - 'A'] == CASE_MODE_UNSPECIFIED && w->cmd.dir_state.linux_mount_dir[drive - 'A']) {
          w->cmd.dir_state.case_mode[drive - 'A'] = CASE_MODE_UPPERCASE;
        }
      }
    }
    /* w->cmd.dir_state.linux_mount_dir[...] and w->cmd.dir_state.case_mode[...] are used below. */
    if (w->prog_filename_type == PFT_DOS) {
      w->cmd.prog_filename = get_linux_filename_r(w->prog_name_arg, &w->cmd.dir_state, fnbuf, NULL);  /* Return value is fnbuf. */
      if (*w->cmd.prog_filename == '\0') {
        fprintf(stderr, "fatal: <dos-executable-file> is not a valid DOS pathname or contains an invalid drive: %s\n", w->prog_name_arg);
        exit(252);
      }
    } else if (w->prog_filename_type == PFT_PATH) {
      if (w->cmd.dir_state.linux_mount_dir['E' - 'A'] == w->placeholder) w->cmd.dir_state.linux_mount_dir['E' - 'A'] = NULL;  /* Drive E: not mounted by default. */
      w->cmd.prog_filename = get_linux_filename_r(w->prog_name_arg, NULL /* w->cmd.dir_state */, fnbuf, NULL);  /* Return value is fnbuf. */
      if (*w->cmd.prog_filename == '\0') {
        fprintf(stderr, "fatal: <dos-executable-file> is not a valid DOS filename: %s\n", w->prog_name_arg);
        exit(252);
      }
      w->cmd.prog_filename = find_prog_on_path(w->prog_name_arg, &w->cmd.dir_state, w->dos_path, &w->dos_prog_drive);  /* Return value is fnbuf or NULL. */
      if (!w->cmd.prog_filename) {
        fprintf(stderr, "error: DOS command not found on %c:\\ or %%PATH%%: %s\n", w->cmd.dir_state.drive, w->prog_name_arg);
        exit(1);
      }
      if (*w->cmd.prog_filename == '\0') {
        fprintf(stderr, "fatal: invalid <dos-executable-file> DOS program name: %s\n", w->prog_name_arg);
        exit(252);
      }
    } else {
      fprintf(stderr, "assert: bad prog_filenam_type: %d\n", w->prog_filename_type);
      exit(252);
    }
  }
  w->prog_name_arg = NULL;  /* Make sure we don't use it later, we've already modified it for w->cmd.dir_state.linux_mount_dir['E' - 'A']. */

  if (!w->cmd.dir_state.linux_mount_dir[w->cmd.dir_state.drive - 'A']) {
    fprintf(stderr, "fatal: no mount point for default drive (specify --mount=...): %c:\n", w->cmd.dir_state.drive);
    exit(1);
  }
  if (w->cmd.dir_state.dos_prog_abs == NULL && w->cmd.prog_filename) {
    w->cmd.dir_state.dos_prog_abs = get_dos_abs_filename_r(w->cmd.prog_filename, w->dos_prog_drive, &w->cmd.dir_state, dosfnbuf);
    if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: prog_filename=(%s) dos_prog_abs=(%s) w->dos_prog_drive=%c\n", w->cmd.prog_filename, w->cmd.dir_state.dos_prog_abs, w->dos_prog_drive);
  }
  if (!w->is_drive_specified && w->prog_filename_type == PFT_LINUX && w->dos_prog_drive >= 'A' && w->dos_prog_drive <= 'Z') {
    w->cmd.dir_state.drive = w->dos_prog_drive;  /* Resolve relative filenames in the executable directory mount by default. */
  }
  if (w->prog_filename_type == PFT_LINUX && w->cmd.dir_state.case_mode['C' - 'A'] == CASE_MODE_UNSPECIFIED && w->cmd.dir_state.linux_mount_dir['C' - 'A']) {
    const char *mount_c = w->cmd.dir_state.linux_mount_dir['C' - 'A'];
    const char *q;
    if (!w->cmd.prog_filename) {
      w->cmd.dir_state.case_mode['C' - 'A'] = CASE_MODE_LOWERCASE;
    } else if (w->cmd.dir_state.dos_prog_abs[0] == 'C' || strncmp(w->cmd.prog_filename, mount_c, strlen(mount_c)) == 0) {  /* Set case mode from the entire pathname. */
      for (q = w->cmd.prog_filename; *q != '\0' && *q - 'a' + 0U > 'z' - 'a' + 0U; ++q) {}
      w->cmd.dir_state.case_mode['C' - 'A'] = (*q == '\0') ? CASE_MODE_UPPERCASE : CASE_MODE_LOWERCASE;
    } else {  /* Set case mode from the basename only. */
      w->cmd.dir_state.case_mode['C' - 'A'] = get_case_mode_from_last_component(w->cmd.prog_filename);
    }
  }

  if (!w->cwd_dos_flag && w->cmd.dir_state.dos_prog_abs && w->cmd.dir_state.dos_prog_abs[0] &&
      (w->cmd.dir_state.dos_prog_abs[0] & ~32) - 'A' + 0U < DRIVE_COUNT &&
      w->cmd.dir_state.dos_prog_abs[1] == ':' && w->cmd.dir_state.dos_prog_abs[2] == '\\') {
    /* Default DOS CWD to the executable directory, so relative file args resolve like in DOSBox.
     * Example: `kvikdos C:\\TOOLS\\UNP.EXE t unp.exe` should search in C:\\TOOLS\\.
     */
    char drive = w->cmd.dir_state.dos_prog_abs[0] & ~32;
    const char *base = w->cmd.dir_state.dos_prog_abs + strlen(w->cmd.dir_state.dos_prog_abs);
    const char *p = w->cmd.dir_state.dos_prog_abs + 3;
    char tmp[DOS_PATH_SIZE];
    size_t n;
    for (; base > p && base[-1] != '\\' && base[-1] != '/'; --base) {}
    if (base > p) {
      n = (size_t)(base - p);
      if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
      memcpy(tmp, p, n);
      if (n > 0 && tmp[n - 1] != '\\') tmp[n++] = '\\';
      tmp[n] = '\0';
      copy_cstr0(w->cmd.dir_state.current_dir[drive - 'A'], sizeof(w->cmd.dir_state.current_dir[drive - 'A']), tmp);
      w->cmd.dir_state.drive = drive;
    }
  }

  if (w->cwd_dos_flag && w->cwd_dos_flag[0]) {
    char drive = w->cmd.dir_state.drive;
    const char *p = w->cwd_dos_flag;
    char tmp[DOS_PATH_SIZE];
    char *q = tmp;
    if ((p[0] & ~32) - 'A' + 0U < DRIVE_COUNT && p[1] == ':') {
      drive = p[0] & ~32;
      p += 2;
      if (*p == '\\' || *p == '/') ++p;
      w->cmd.dir_state.drive = drive;
    } else if (*p == '\\' || *p == '/') {
      ++p;
    }
    while (*p && q + 2 < tmp + sizeof(tmp)) {
      char c = *p++;
      if (c == '/') c = '\\';
      *q++ = ((unsigned)c - 'a' + 0U <= 'z' - 'a' + 0U) ? (c & ~32) : c;
    }
    if (q != tmp && q[-1] != '\\') *q++ = '\\';
    *q = '\0';
    copy_cstr0(w->cmd.dir_state.current_dir[drive - 'A'], sizeof(w->cmd.dir_state.current_dir[drive - 'A']), tmp);
  }
}
