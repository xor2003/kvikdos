#include "kvikdos.h"
#include "intrun.h"

int main(int argc, char **argv) {
  ParsedCmdArgs cmd_args;
  (void)argc;
  g_diag_file = stderr; /* default diag stream: valid before parse_args (compile-time DEBUG paths may print early) */
  parse_args(
      argv_with_env_flags(argv), &cmd_args,
      "kvikdos: run DOS programs headless (a very fast DOS emulator)\nUsage: ", "",
      "This is free software, GNU GPL >=2.0. There is NO WARRANTY. Use at your risk.\n");
  g_case_fallback_mode = cmd_args.emu_params.case_fallback_mode;
  g_diag_mask = cmd_args.emu_params.diag_mask;
  if (cmd_args.emu_params.diag_filename) {
    g_diag_file = fopen(cmd_args.emu_params.diag_filename, "ab");
    if (!g_diag_file) {
      perror("fatal: cannot open --diag-file");
      exit(1);
    }
  } else {
    g_diag_file = stderr;
  }
  /* Unbuffered so a hung or looping guest's diagnostics stream out live instead
   * of sitting in the stdio buffer until (a never-arriving) exit. */
  setvbuf(g_diag_file, NULL, _IONBF, 0);
  maybe_open_io_trace_file();
  maybe_parse_mem_dump();
  maybe_parse_exit_regs();
  if (0) {  /* Just dump the parsed command-line. */
    /* cmd_args.dir_state.linux_prog is still NULL, use cmd_args.prog_filename instead. */
    printf("linux prog: %s\n", cmd_args.prog_filename);
    printf("dos prog: %s\n", cmd_args.dir_state.dos_prog_abs);
    printf("drive %c:\n", cmd_args.dir_state.drive);
    { char drive_idx;
      for (drive_idx = 0; drive_idx < DRIVE_COUNT; ++drive_idx) {
        const char *mount_dir = cmd_args.dir_state.linux_mount_dir[(int)drive_idx];
        if (mount_dir) {
          printf("mount dir of   %c: %s\n", 'A' + drive_idx, mount_dir[0] != '\0' ? mount_dir : "./");
          /*printf("current dir on %c: %s\n", 'A' + drive_idx, cmd_args.dir_state.current_dir[(int)drive_idx]);*/  /* Currently always empty. */
          printf("case mode of   %c: %d\n", 'A' + drive_idx, cmd_args.dir_state.case_mode[(int)drive_idx]);
        }
      }
    }
    printf("end of drives\n");
    { const char* const *arg;
      for (arg = cmd_args.args; *arg; ++arg) {
        printf("arg: %s\n", *arg);
      }
      printf("end of args\n");
    }
    { const char* const *env;
      for (env = cmd_args.envp0; *env; ++env) {
        printf("env: %s\n", *env);
      }
      printf("end of envs\n");
    }
    printf("tty_in_fd: %d\n", cmd_args.tty_in_fd);
    printf("mem_mb: %u\n", cmd_args.emu_params.mem_mb);
    printf("is_hlt_ok: %d\n", cmd_args.emu_params.is_hlt_ok);
    return 0;
  }
  if (!cmd_args.force_dos && is_linux_native_executable(cmd_args.prog_filename)) {
    int rc = run_native_execvp(cmd_args.prog_filename, cmd_args.args);
    free_extra_env_args(&cmd_args);
    return rc;
  }
  {
    const enum mz_subformat_t subfmt = detect_mz_subformat(cmd_args.prog_filename);
    const int is_ext = is_probable_dos_extender_program(cmd_args.prog_filename);
    if (!cmd_args.force_dos && !is_ext && subfmt == MZ_SUBFMT_PE && is_windows_host()) {
      /* On Windows a PE executable is native: 64-bit runs directly, 32-bit
       * via WoW64 — no wine needed. Spawn it and propagate its exit code. */
      int rc = run_native_execvp(cmd_args.prog_filename, cmd_args.args);
      free_extra_env_args(&cmd_args);
      return rc;
    }
    if (!cmd_args.force_dos && !is_ext &&
        (subfmt == MZ_SUBFMT_PE ||
         ((subfmt == MZ_SUBFMT_NE || subfmt == MZ_SUBFMT_LE || subfmt == MZ_SUBFMT_LX) &&
          !is_probable_borland_dual_mode_ne(cmd_args.prog_filename) &&
          is_probable_windows_message_stub(cmd_args.prog_filename)))) {
    if (!has_wine_in_path()) {
        fprintf(stderr, "error: detected Windows executable, but 'wine' is not in PATH: %s\n", cmd_args.prog_filename);
        free_extra_env_args(&cmd_args);
      return 1;
    }
      fprintf(stderr, "info: detected Windows executable, delegating to wine: %s\n", cmd_args.prog_filename);
      {
        int rc = run_with_wine(cmd_args.prog_filename, cmd_args.args, NULL);
        free_extra_env_args(&cmd_args);
        return rc;
      }
    }
  }
  { int exit_code;
    const char *ext = get_linux_ext(cmd_args.prog_filename);
    EmuState emu;
    TtyState tty_state;
    init_emu(&emu);  /* This is lightweight, it doesn't initialize KVM. */
    init_tty_state(&tty_state, cmd_args.tty_in_fd);
    if (is_same_ascii_nocase(ext, "bat", 4)) {
      exit_code = run_dos_batch(&emu, cmd_args.prog_filename, cmd_args.args, &cmd_args.dir_state, &tty_state, &cmd_args.emu_params, cmd_args.envp0, (const char* const*)cmd_args.extra_env, cmd_args.extra_env_count);
    } else {
      exit_code = run_dos_prog(&emu, cmd_args.prog_filename, cmd_args.dpmi_prog, NULL, cmd_args.args, &cmd_args.dir_state, &tty_state, &cmd_args.emu_params, cmd_args.envp0, (const char* const*)cmd_args.extra_env, cmd_args.extra_env_count);
    }
    if (DEBUG || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "debug: DOS program exited with code: 0x%02x", exit_code);
    con_teardown_reset(mem);  /* Emit console text still withheld by the dump filter. */
    free_extra_env_args(&cmd_args);
    return exit_code;
  }
}
