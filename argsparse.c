#include "kvikdos.h"

/* Convert forward slashes to DOS backslashes in a DOS-path flag value. */
static void slashes_to_dos(char *s) {
  for (; *s != '\0'; ++s) if (*s == '/') *s = '\\';
}

void parse_option_loop(struct ArgsWork *w) {
  while (w->argv[0]) {
    char *arg = *w->argv++;
    if (arg[0] != '-' || arg[1] == '\0') {
      --w->argv; break;
    } else if (arg[1] == '-' && arg[2] == '\0') {
      break;
    } else if (0 == strcmp(arg, "--hlt-ok")) {
      w->cmd.emu_params.is_hlt_ok = 1;
    } else if (0 == strcmp(arg, "--env")) {
      if (!w->argv[0]) { missing_argument:
        fprintf(stderr, "fatal: missing argument for flag: %s\n", arg);
        exit(1);
      }
      arg = *w->argv++;
     do_env:
      { char *p = arg, c;
        for (; (c = *p) != '\0' && c != '='; ++p) {
          if (c - 'a' + 0U <= 'z' - 'a' + 0U) *p &= ~32;  /* Convert variable name to uppercase. */
        }
      }
      *w->envp++ = arg;  /* Reuse the w->argv array. */
    } else if (0 == strncmp(arg, "--env=", 6)) {  /* Can be specified multiple times. */
      arg += 6;
      goto do_env;
    } else if (0 == strcmp(arg, "--prog")) {
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_prog:
      slashes_to_dos(arg);
      w->cmd.dir_state.dos_prog_abs = arg;
    } else if (0 == strncmp(arg, "--prog=", 7)) {
      arg += 7;
      goto do_prog;
    } else if (0 == strcmp(arg, "--dpmi")) {  /* Typical example: --dpmi=E:hdpmi32.exe */
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_dpmi:
      slashes_to_dos(arg);
      w->cmd.dpmi_prog = (const char*)arg;
    } else if (0 == strncmp(arg, "--dpmi=", 7)) {
      arg += 7;
      goto do_dpmi;
    } else if (0 == strcmp(arg, "--force-dos")) {
      w->cmd.force_dos = 1;
    } else if (0 == strcmp(arg, "--hlt-dump")) {  /* Typical example: --hlt-dump=kvikdos.dmp */
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_hlt_dump:
      w->cmd.emu_params.hlt_dump_filename = (const char*)arg;
    } else if (0 == strncmp(arg, "--hlt-dump=", 11)) {
      arg += 11;
      goto do_hlt_dump;
    } else if (0 == strcmp(arg, "--call-near")) {
      char *endp;
      unsigned long value;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_near:
      value = strtoul(arg, &endp, 0);
      if (*endp != '\0' || value > 0xffffUL) {
        fprintf(stderr, "fatal: call-near argument must be a 16-bit offset: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_near_enabled = 1;
      w->cmd.emu_params.call_near_ip = (unsigned short)value;
      w->cmd.emu_params.is_hlt_ok = 1;
    } else if (0 == strncmp(arg, "--call-near=", 12)) {
      arg += 12;
      goto do_call_near;
    } else if (0 == strcmp(arg, "--call-far")) {
      char *endp;
      unsigned long seg, ofs;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_far:
      seg = strtoul(arg, &endp, 0);
      if (*endp != ':') {
        fprintf(stderr, "fatal: call-far argument must be <seg>:<ip>: %s\n", arg);
        exit(1);
      }
      ofs = strtoul(endp + 1, &endp, 0);
      if (*endp != '\0' || ofs > 0xffffUL || seg > 0xffffUL) {
        fprintf(stderr, "fatal: call-far argument must be <seg>:<ip>: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_far_enabled = 1;
      w->cmd.emu_params.call_far_seg = (unsigned short)seg;
      w->cmd.emu_params.call_far_ip = (unsigned short)ofs;
      w->cmd.emu_params.is_hlt_ok = 1;
    } else if (0 == strncmp(arg, "--call-far=", 11)) {
      arg += 11;
      goto do_call_far;
    } else if (0 == strcmp(arg, "--call-ss")) {
      char *endp;
      unsigned long seg, ofs;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_ss:
      seg = strtoul(arg, &endp, 0);
      if (*endp != ':') {
        fprintf(stderr, "fatal: call-ss argument must be <seg>:<sp>: %s\n", arg);
        exit(1);
      }
      ofs = strtoul(endp + 1, &endp, 0);
      if (*endp != '\0' || ofs > 0xffffUL || seg > 0xffffUL) {
        fprintf(stderr, "fatal: call-ss argument must be <seg>:<sp>: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_ss_enabled = 1;
      w->cmd.emu_params.call_ss = (unsigned short)seg;
      w->cmd.emu_params.call_sp = (unsigned short)ofs;
    } else if (0 == strncmp(arg, "--call-ss=", 10)) {
      arg += 10;
      goto do_call_ss;
    } else if (0 == strcmp(arg, "--call-cs")) {
      char *endp;
      unsigned long value;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_cs:
      value = strtoul(arg, &endp, 0);
      if (*endp != '\0' || value > 0xffffUL) {
        fprintf(stderr, "fatal: call-cs argument must be a 16-bit segment: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_cs_enabled = 1;
      w->cmd.emu_params.call_cs = (unsigned short)value;
    } else if (0 == strncmp(arg, "--call-cs=", 10)) {
      arg += 10;
      goto do_call_cs;
    } else if (0 == strcmp(arg, "--call-ds")) {
      char *endp;
      unsigned long value;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_ds:
      value = strtoul(arg, &endp, 0);
      if (*endp != '\0' || value > 0xffffUL) {
        fprintf(stderr, "fatal: call-ds argument must be a 16-bit segment: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_ds_enabled = 1;
      w->cmd.emu_params.call_ds = (unsigned short)value;
    } else if (0 == strncmp(arg, "--call-ds=", 10)) {
      arg += 10;
      goto do_call_ds;
    } else if (0 == strcmp(arg, "--call-arg")) {
      char *endp;
      unsigned long value;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_arg:
      if (w->cmd.emu_params.call_arg_count >= sizeof(w->cmd.emu_params.call_args) / sizeof(w->cmd.emu_params.call_args[0])) {
        fprintf(stderr, "fatal: too many call-arg values, maximum is 16\n");
        exit(1);
      }
      value = strtoul(arg, &endp, 0);
      if (*endp != '\0' || value > 0xffffUL) {
        fprintf(stderr, "fatal: call-arg argument must be a 16-bit word: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_args[w->cmd.emu_params.call_arg_count++] = (unsigned short)value;
    } else if (0 == strncmp(arg, "--call-arg=", 11)) {
      arg += 11;
      goto do_call_arg;
    } else if (0 == strcmp(arg, "--call-set")) {
      char *endp;
      unsigned long value;
      unsigned idx;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_call_set:
      if (0 == strncmp(arg, "ax:", 3)) idx = 0;
      else if (0 == strncmp(arg, "cx:", 3)) idx = 1;
      else if (0 == strncmp(arg, "dx:", 3)) idx = 2;
      else if (0 == strncmp(arg, "bx:", 3)) idx = 3;
      else if (0 == strncmp(arg, "si:", 3)) idx = 4;
      else if (0 == strncmp(arg, "di:", 3)) idx = 5;
      else if (0 == strncmp(arg, "bp:", 3)) idx = 6;
      else {
        fprintf(stderr, "fatal: call-set argument must be <ax|cx|dx|bx|si|di|bp>:<word>: %s\n", arg);
        exit(1);
      }
      value = strtoul(arg + 3, &endp, 0);
      if (*endp != '\0' || value > 0xffffUL) {
        fprintf(stderr, "fatal: call-set argument must be a 16-bit word: %s\n", arg);
        exit(1);
      }
      w->cmd.emu_params.call_set_regs[idx] = (unsigned short)value;
      w->cmd.emu_params.call_set_mask |= 1U << idx;
    } else if (0 == strncmp(arg, "--call-set=", 11)) {
      arg += 11;
      goto do_call_set;
    } else if (0 == strcmp(arg, "--poke-word")) {
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
      goto do_poke_word;
    } else if (0 == strncmp(arg, "--poke-word=", 12)) {
      arg += 12;
     do_poke_word:
      {
        char *p1, *p2, *endp;
        unsigned long seg, ofs, value;
        if (w->cmd.emu_params.poke_word_count >= sizeof(w->cmd.emu_params.poke_word_values) / sizeof(w->cmd.emu_params.poke_word_values[0])) {
          fprintf(stderr, "fatal: too many poke-word values, maximum is 1024\n");
          exit(1);
        }
        seg = strtoul(arg, &p1, 0);
        if (*p1 != ':') {
          fprintf(stderr, "fatal: poke-word argument must be <seg>:<off>:<word>: %s\n", arg);
          exit(1);
        }
        ofs = strtoul(p1 + 1, &p2, 0);
        if (*p2 != ':') {
          fprintf(stderr, "fatal: poke-word argument must be <seg>:<off>:<word>: %s\n", arg);
          exit(1);
        }
        value = strtoul(p2 + 1, &endp, 0);
        if (*endp != '\0' || seg > 0xffffUL || ofs > 0xffffUL || value > 0xffffUL) {
          fprintf(stderr, "fatal: poke-word values must be 16-bit numbers: %s\n", arg);
          exit(1);
        }
        w->cmd.emu_params.poke_word_segs[w->cmd.emu_params.poke_word_count] = (unsigned short)seg;
        w->cmd.emu_params.poke_word_ofs[w->cmd.emu_params.poke_word_count] = (unsigned short)ofs;
        w->cmd.emu_params.poke_word_values[w->cmd.emu_params.poke_word_count] = (unsigned short)value;
        ++w->cmd.emu_params.poke_word_count;
      }
    } else if (0 == strcmp(arg, "--mount")) {  /* Can be specified multiple times. */
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_mount:  /* Default: --mount C:. */
      if ((arg[0] & ~32) - 'A' + 0U >= DRIVE_COUNT || !(arg[1] == ':' || arg[1] == '-' || arg[1] == '0')) {
        fprintf(stderr, "fatal: mount argument must start with <drive>: or <drive>-, <drive> must be A .. %c: %s\n", 'A' + DRIVE_COUNT - 1, arg);
        exit(1);
      } else {
        const char drive_idx = (arg[0] & ~32) - 'A';
        const char case_mode = arg[1] == '0' ? CASE_MODE_UNSPECIFIED : arg[1] == '-' ? CASE_MODE_LOWERCASE : CASE_MODE_UPPERCASE;
        if (arg[1] == '0') {
          if (arg[2] != '\0') {
            fprintf(stderr, "fatal: mount argument for not visibility must stop at 0: %s\n", arg);
            exit(1);
          }
          arg = NULL;  /* Make sure not mounted. */
        } else {
          arg += 2;
          if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: mount %c: %s\n", drive_idx + 'A', arg);
          if (arg[0] == '\0') {
            arg = w->placeholder;
          } else {
            arg = (char*)skip_dot_slash(arg);
            remove_duplicate_slashes(arg);
            if (arg[0] == '.' && arg[1] == '\0') {
              ++arg;
            } else if (arg[0] != '\0') {
              char *p = arg + strlen(arg);
              if (arg[1] != '\0' && p[-1] == '.' && p[-2] == '/') *--p = '\0';  /* Remove trailing . if it ends with /. */
              if (p[-1] != '/') {  /* Missing trailing /: append it. */
                char *m = (char*)malloc((size_t)(p - arg) + 2);
                if (!m) { perror("fatal: malloc"); exit(252); }
                memcpy(m, arg, (size_t)(p - arg));
                m[p - arg] = '/';
                m[p - arg + 1] = '\0';
                arg = m;
              }
            }
          }
        }
        w->cmd.dir_state.linux_mount_dir[(int)drive_idx] = arg;  /* NOLINT(clang-analyzer-unix.Malloc): mount strings live in dir_state for the whole run */
        w->cmd.dir_state.case_mode[(int)drive_idx] = case_mode;
      }
    } else if (0 == strncmp(arg, "--mount=", 8)) {
      arg += 8;
      goto do_mount;
    } else if (0 == strcmp(arg, "--drive")) {  /* Can be specified multiple times. */
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_drive:  /* Default: --drive C: */
      if ((arg[0] & ~32) - 'A' + 0U >= DRIVE_COUNT || !(arg[1] == '\0' || (arg[1] == ':' && arg[2] == '\0'))) {
        fprintf(stderr, "fatal: drive argument must be <drive>:, <drive> must be A .. %c: %s\n", 'A' + DRIVE_COUNT - 1, arg);
        exit(1);
      }
      w->cmd.dir_state.drive = arg[0] & ~32;
      w->is_drive_specified = 1;
    } else if (0 == strncmp(arg, "--drive=", 8)) {
      arg += 8;
      goto do_drive;
    } else if (0 == strcmp(arg, "--root")) {  /* Typical example: --root=/tmp/dos */
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_root:
      {
        const char *home = NULL;
        char *m;
        size_t nd, nh = 0;
        /* Equivalent to --mount=C:<dir> --drive=C: --cwd-dos=C:\ plus
         * PATH=C:\;C:\BIN — later flags can still override. */
        w->cmd.dir_state.drive = 'C';
        w->is_drive_specified = 1;
        w->cwd_dos_flag = "C:\\";
        w->path_dos_flag = "C:\\;C:\\BIN";
        nd = strlen(arg);
        if (arg[0] == '~' && arg[1] == '/') {  /* Support --root=~/dir. */
          home = getenv("HOME");
          if (home) nh = strlen(home);
        }
        m = (char*)malloc(nh + nd + 4);
        if (!m) { perror("fatal: malloc"); exit(252); }
        memcpy(m, "C:", 2);
        if (nh) {
          memcpy(m + 2, home, nh);
          memcpy(m + 2 + nh, arg + 1, nd - 1);  /* arg+1 keeps the '/' of "~/". */
          nd = nh + nd - 1;  /* Length of the dir part in m. */
        } else {
          memcpy(m + 2, arg, nd);
        }
        if (nd && m[nd + 1] != '/') m[2 + nd++] = '/';  /* Append missing trailing /. */
        m[2 + nd] = '\0';
        arg = m;
        goto do_mount;
      }
    } else if (0 == strncmp(arg, "--root=", 7)) {
      arg += 7;
      goto do_root;
    } else if (0 == strcmp(arg, "--tty-in")) {
      int char_count;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_tty_in:
      if (sscanf(arg, "%d%n", &w->cmd.tty_in_fd, &char_count) < 1 || char_count + 0U != strlen(arg) || w->cmd.tty_in_fd < -3) {
        /* -1: use /dev/tty; -2: use 0 (stdin), but don't try to disable buffering; -3: fake keys in round-robin. */
        fprintf(stderr, "fatal: tty-in argument must be nonnegative integer or -1, -2 or -3: %s\n", arg);
        exit(1);
      }
      /* Now we've set w->cmd.tty_in_fd. */
    } else if (0 == strncmp(arg, "--tty-in=", 9)) {
      arg += 9;
      goto do_tty_in;
    } else if (0 == strcmp(arg, "--mem-mb")) {
      int char_count;
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_mem_mb:
      if (sscanf(arg, "%d%n", (int*)&w->cmd.emu_params.mem_mb, &char_count) < 1 || char_count + 0U != strlen(arg) || (int)w->cmd.emu_params.mem_mb <= 0) {
        fprintf(stderr, "fatal: mem-mb argument must be poisitive: %s\n", arg);
        exit(1);
      }
      if (w->cmd.emu_params.mem_mb > 1024) {
        fprintf(stderr, "fatal: --mem-mb too large (max 1024): %s\n", arg);
        exit(1);
      }
      /* Now we've set w->cmd.mem_mb. */
    } else if (0 == strncmp(arg, "--mem-mb=", 9)) {
      arg += 9;
      goto do_mem_mb;
    } else if (0 == strcmp(arg, "--kvm-check")) {
      w->is_kvm_check = 1;
    } else if (0 == strcmp(arg, "--permissive")) {
      w->cmd.emu_params.strict_mode = 0;
    } else if (0 == strcmp(arg, "--strict")) {
      w->cmd.emu_params.strict_mode = 1;
    } else if (0 == strcmp(arg, "--batch-cd-root")) {
      w->cmd.emu_params.batch_cd_root_mode = 1;
    } else if (0 == strcmp(arg, "--path-dos")) {
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
     do_path_dos:
      slashes_to_dos(arg);
      w->path_dos_flag = arg;
    } else if (0 == strncmp(arg, "--path-dos=", 11)) {
      arg += 11;
      goto do_path_dos;
    } else if (0 == strcmp(arg, "--cwd-dos")) {
      if (!w->argv[0]) goto missing_argument;
      w->cwd_dos_flag = *w->argv++;
    } else if (0 == strncmp(arg, "--cwd-dos=", 10)) {
      w->cwd_dos_flag = arg + 10;
    } else if (0 == strcmp(arg, "--env-file")) {
      FILE *f;
      char line[4096];
      if (!w->argv[0]) goto missing_argument;
      f = fopen(*w->argv++, "rb");
      if (!f) {
        perror("fatal: cannot open --env-file");
        exit(1);
      }
      while (fgets(line, sizeof(line), f)) {
        char *p = line, *eq, *e;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') continue;
        for (e = p + strlen(p); e != p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'); --e) {}
        *e = '\0';
        eq = strchr(p, '=');
        if (!eq || eq == p) continue;
        if (w->cmd.extra_env_count >= sizeof(w->cmd.extra_env) / sizeof(w->cmd.extra_env[0])) {
          fprintf(stderr, "fatal: too many extra env vars\n");
          exit(1);
        }
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup(p);
      }
      fclose(f);
    } else if (0 == strncmp(arg, "--env-file=", 11)) {
      FILE *f = fopen(arg + 11, "rb");
      char line[4096];
      if (!f) {
        perror("fatal: cannot open --env-file");
        exit(1);
      }
      while (fgets(line, sizeof(line), f)) {
        char *p = line, *eq, *e;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') continue;
        for (e = p + strlen(p); e != p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'); --e) {}
        *e = '\0';
        eq = strchr(p, '=');
        if (!eq || eq == p) continue;
        if (w->cmd.extra_env_count >= sizeof(w->cmd.extra_env) / sizeof(w->cmd.extra_env[0])) {
          fprintf(stderr, "fatal: too many extra env vars\n");
          exit(1);
        }
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup(p);
      }
      fclose(f);
    } else if (0 == strcmp(arg, "--diag")) {
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
      if (strcmp(arg, "off") == 0) w->cmd.emu_params.diag_mask = 0;
      else if (strcmp(arg, "compat") == 0) w->cmd.emu_params.diag_mask = 1;
      else if (strcmp(arg, "exec") == 0) w->cmd.emu_params.diag_mask = 2;
      else if (strcmp(arg, "int") == 0) w->cmd.emu_params.diag_mask = 4;
      else if (strcmp(arg, "fs") == 0) w->cmd.emu_params.diag_mask = 8;
      else if (strcmp(arg, "all") == 0) w->cmd.emu_params.diag_mask = ~0U;
      else { fprintf(stderr, "fatal: bad --diag value: %s\n", arg); exit(1); }
    } else if (0 == strncmp(arg, "--diag=", 7)) {
      arg += 7;
      if (strcmp(arg, "off") == 0) w->cmd.emu_params.diag_mask = 0;
      else if (strcmp(arg, "compat") == 0) w->cmd.emu_params.diag_mask = 1;
      else if (strcmp(arg, "exec") == 0) w->cmd.emu_params.diag_mask = 2;
      else if (strcmp(arg, "int") == 0) w->cmd.emu_params.diag_mask = 4;
      else if (strcmp(arg, "fs") == 0) w->cmd.emu_params.diag_mask = 8;
      else if (strcmp(arg, "all") == 0) w->cmd.emu_params.diag_mask = ~0U;
      else { fprintf(stderr, "fatal: bad --diag value: %s\n", arg); exit(1); }
    } else if (0 == strcmp(arg, "--diag-file")) {
      if (!w->argv[0]) goto missing_argument;
      w->cmd.emu_params.diag_filename = *w->argv++;
    } else if (0 == strncmp(arg, "--diag-file=", 12)) {
      w->cmd.emu_params.diag_filename = arg + 12;
    } else if (0 == strcmp(arg, "--case-fallback")) {
      if (!w->argv[0]) goto missing_argument;
      arg = *w->argv++;
      if (strcmp(arg, "off") == 0) w->cmd.emu_params.case_fallback_mode = 0;
      else if (strcmp(arg, "prog") == 0) w->cmd.emu_params.case_fallback_mode = 1;
      else if (strcmp(arg, "all") == 0) w->cmd.emu_params.case_fallback_mode = 2;
      else { fprintf(stderr, "fatal: bad --case-fallback value: %s\n", arg); exit(1); }
    } else if (0 == strncmp(arg, "--case-fallback=", 16)) {
      arg += 16;
      if (strcmp(arg, "off") == 0) w->cmd.emu_params.case_fallback_mode = 0;
      else if (strcmp(arg, "prog") == 0) w->cmd.emu_params.case_fallback_mode = 1;
      else if (strcmp(arg, "all") == 0) w->cmd.emu_params.case_fallback_mode = 2;
      else { fprintf(stderr, "fatal: bad --case-fallback value: %s\n", arg); exit(1); }
    } else if (0 == strcmp(arg, "--toolchain") || 0 == strncmp(arg, "--toolchain=", 12)) {
      const char *tc;
      if (arg[11] == '\0') {
        if (!w->argv[0]) goto missing_argument;
        tc = *w->argv++;
      } else {
        tc = arg + 12;
      }
      if (w->cmd.extra_env_count + 4 >= sizeof(w->cmd.extra_env) / sizeof(w->cmd.extra_env[0])) {
        fprintf(stderr, "fatal: too many extra env vars\n");
        exit(1);
      }
      if (strcmp(tc, "msc6") == 0 || strcmp(tc, "bcpp1") == 0 || strcmp(tc, "bc5") == 0) {
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup("PATH=C:\\BIN;C:\\");
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup("LIB=C:\\LIB");
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup("INCLUDE=C:\\INCLUDE");
      } else if (strcmp(tc, "msc4") == 0 || strcmp(tc, "msc5") == 0 || strcmp(tc, "masm5") == 0 || strcmp(tc, "bc2") == 0 || strcmp(tc, "ic86") == 0) {
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup("PATH=C:\\");
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup("LIB=LIB");
        w->cmd.extra_env[w->cmd.extra_env_count++] = xstrdup("INCLUDE=INCLUDE");
      } else {
        fprintf(stderr, "fatal: unknown --toolchain preset: %s\n", tc);
        exit(1);
      }
    } else {
      fprintf(stderr, "fatal: unknown command-line flag: %s\n", arg);
      exit(1);
    }
  }
}
