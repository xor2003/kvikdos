#include "kvikdos.h"

void init_parsed_cmd_args(ParsedCmdArgs *cmd_args, char *placeholder_for_default) {
  unsigned u;
  memset(cmd_args, 0, sizeof(*cmd_args));
  for (u = 0; u < DRIVE_COUNT; ++u) {
    cmd_args->dir_state.current_dir[u][0] = '\0';
    cmd_args->dir_state.linux_mount_dir[u] = NULL;
  }
  cmd_args->dir_state.drive = 'C';
  cmd_args->dir_state.dos_prog_abs = NULL;
  cmd_args->dir_state.linux_prog = NULL;
  cmd_args->dir_state.linux_mount_dir['C' - 'A'] = placeholder_for_default;
  cmd_args->dir_state.linux_mount_dir['D' - 'A'] = placeholder_for_default;
  cmd_args->dir_state.linux_mount_dir['E' - 'A'] = placeholder_for_default;
  memset(cmd_args->dir_state.case_mode, CASE_MODE_UNSPECIFIED, DRIVE_COUNT);
  cmd_args->dpmi_prog = NULL;
  cmd_args->extra_env_count = 0;
  cmd_args->tty_in_fd = -1;
  cmd_args->emu_params.mem_mb = 128;
  cmd_args->emu_params.is_hlt_ok = 0;
  cmd_args->emu_params.hlt_dump_filename = NULL;
  cmd_args->emu_params.diag_filename = NULL;
  cmd_args->emu_params.diag_mask = 1;  /* compat */
  cmd_args->emu_params.case_fallback_mode = 2;  /* all */
  cmd_args->emu_params.strict_mode = 0;  /* permissive */
  cmd_args->emu_params.batch_cd_root_mode = 0;  /* legacy */
  cmd_args->emu_params.call_near_enabled = 0;
  cmd_args->emu_params.call_near_ip = 0;
  cmd_args->emu_params.call_far_enabled = 0;
  cmd_args->emu_params.call_far_seg = 0;
  cmd_args->emu_params.call_far_ip = 0;
  cmd_args->emu_params.call_ss_enabled = 0;
  cmd_args->emu_params.call_ss = 0;
  cmd_args->emu_params.call_sp = 0;
  cmd_args->emu_params.call_cs_enabled = 0;
  cmd_args->emu_params.call_cs = 0;
  cmd_args->emu_params.call_ds_enabled = 0;
  cmd_args->emu_params.call_ds = 0;
  cmd_args->emu_params.call_arg_count = 0;
  cmd_args->emu_params.call_set_mask = 0;
  cmd_args->emu_params.poke_word_count = 0;
}

void parse_args(char **argv, struct ParsedCmdArgs *cmd_args_out, const char *pre_msg, const char *usage_extra, const char *post_msg) {
  struct ArgsWork w;
  w.argv = argv;
  w.placeholder = (char*)pre_msg;
  w.path_dos_flag = NULL;
  w.cwd_dos_flag = NULL;

  w.argv0 = w.argv[0];
  /* Ignoring instances of the --cmd flage, for pts-fast-dosbox compatibility. */
  for (; w.argv[1] && 0 == strcmp(w.argv[1], "--cmd"); ++w.argv) {}
  if (!w.argv0 || !w.argv[1] || 0 == strcmp(w.argv[1], "--help")) {
    fprintf(stderr, "%s%s%s [<flag> ...] <dos-executable-file> [<dos-arg> ...]\n%s"
                    "General:\n"
                    "  --kvm-check                Check KVM only (runs fake true.com)\n"
                    "  --strict | --permissive    Interrupt/API handling policy (default: --permissive)\n"
                    "\n"
                    "DOS Runtime:\n"
                    "  --toolchain=<name>         Preset env for msc4|msc5|msc6|masm5|bc2|bcpp1|bc5|ic86\n"
                    "  --env=<NAME>=<value>       Add DOS environment variable\n"
                    "  --env-file=<file>          Load DOS env vars (NAME=VALUE lines)\n"
                    "  --path-dos=<pathlist>      Set DOS PATH directly (e.g. C:\\BIN;C:\\)\n"
                    "  --prog=<dos-pathname>      Set DOS pathname of running program\n"
                    "  --cwd-dos=<path>           Set initial DOS current directory (e.g. C:\\BIN)\n"
                    "  --batch-cd-root            Enable root-absolute `cd \\foo' in .bat built-in `cd'\n"
                    "\n"
                    "Mounts:\n"
                    "  --mount=<drive><case><dirname>/   Mount Linux dir to DOS drive\n"
                    "  --mount=<drive>0                  Hide DOS drive\n"
                    "    <case> ':' uppercase, '-' lowercase\n"
                    "  --drive=<drive>             Set initial DOS drive\n"
                    "\n"
                    "Compatibility:\n"
                    "  --case-fallback=off|prog|all  Case-insensitive program lookup (default: all)\n"
                    "\n"
                    "Diagnostics:\n"
                    "  --diag=compat|exec|int|fs|all|off   Runtime diagnostics (default: compat)\n"
                    "  --diag-file=<file>                  Write diagnostics to file\n"
                    "\n"
                    "I/O and Memory:\n"
                    "  --tty-in=<fd>               -3 fake, -2 buffered stdin, -1 /dev/tty, >=0 fd\n"
                    "  --mem-mb=<n>                DOS memory in MiB, 1..1024 (128: default; >1 adds extended memory for protected mode)\n"
                    "  --force-dos                 Always run the program in the DOS emulator, even if it looks like a Windows executable\n"
                    "  --hlt-ok                    Allow hlt instruction\n"
                    "  --hlt-dump=<filename>       Dump guest memory on hlt\n"
                    "\n"
                    "Function Harness:\n"
                    "  --call-near=<ip>            Run one near function at CS:<ip> after loading the MZ image\n"
                    "  --call-far=<seg>:<ip>       Run one far function at <seg>:<ip> after loading the MZ image\n"
                    "  --call-cs=<seg>             Set CS before entering --call-near\n"
                    "  --call-ss=<seg>:<sp>        Set SS:SP before entering --call-near/--call-far\n"
                    "  --call-ds=<seg>             Set DS and ES before entering --call-near\n"
                    "  --call-arg=<word>           Append one 16-bit stack argument for --call-near\n"
                    "  --call-set=<reg>:<word>     Set ax/cx/dx/bx/si/di/bp before --call-near/--call-far\n"
                    "  --poke-word=<seg>:<off>:<word>  Store one 16-bit word before --call-near\n",
                    pre_msg, w.argv0, usage_extra, post_msg);
    exit(w.argv0 && w.argv[1] ? 0 : 1);
  }
  if (0 == strcmp(w.argv[1], "--version")) {
    fprintf(stdout, "kvikdos v1\n");
    exit(0);
  }

  init_parsed_cmd_args(&w.cmd, w.placeholder);
  /* Used by get_linux_filename_r() alias check; must be initialized in parse-only paths. */
  w.cmd.dir_state.linux_prog = NULL;
  w.envp = w.envp0 = ++w.argv;
  w.is_kvm_check = 0;
  w.is_drive_specified = 0;
  parse_option_loop(&w);
  finish_args(&w);
  w.cmd.args = (const char* const*)w.argv;
  w.cmd.envp0 = (const char* const*)w.envp0;
  *cmd_args_out = w.cmd;
}

void free_extra_env_args(ParsedCmdArgs *cmd_args) {
  unsigned i;
  for (i = 0; i < cmd_args->extra_env_count; ++i) {
    free((void*)cmd_args->extra_env[i]);
    cmd_args->extra_env[i] = NULL;
  }
  cmd_args->extra_env_count = 0;
}
