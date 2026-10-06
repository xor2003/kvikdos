#include "kvikdos.h"

unsigned char run_dos_batch(struct EmuState *emu, const char *prog_filename, const char* const *args, DirState *dir_state, TtyState *tty_state, const EmuParams *emu_params, const char* const *envp0, const char* const *extra_env, unsigned extra_env_count) {
  struct BatchCtx BB, *bctx = &BB;
  unsigned ai, bi;
  size_t size;
  bctx->emu = emu; bctx->args = args; bctx->dir_state = dir_state; bctx->tty_state = tty_state;
  bctx->emu_params = emu_params; bctx->envp0 = envp0; bctx->extra_env = extra_env; bctx->extra_env_count = extra_env_count;
  bctx->prog_filename = prog_filename;
  bctx->exit_code = 0; bctx->p = bctx->p_line = bctx->buf; bctx->do_echo = 1;
  bctx->has_path_override = 0; bctx->have_goto = 0; bctx->batch_argc = 0;
  bctx->batch_env_count = 0; bctx->setlocal_depth = 0; bctx->batch_eof = 0;
for (bi = 0; bctx->envp0 && bctx->envp0[bi] && bctx->batch_env_count < sizeof(bctx->batch_env) / sizeof(bctx->batch_env[0]); ++bi) {
    char *cp = xstrdup(bctx->envp0[bi]);
    if (!cp) { perror("fatal: xstrdup"); exit(252); }
    bctx->batch_env[bctx->batch_env_count++] = cp;
  }
  for (bi = 0; bctx->extra_env && bi < bctx->extra_env_count && bctx->batch_env_count < sizeof(bctx->batch_env) / sizeof(bctx->batch_env[0]); ++bi) {
    char *cp = xstrdup(bctx->extra_env[bi]);
    if (!cp) { perror("fatal: xstrdup"); exit(252); }
    bctx->batch_env[bctx->batch_env_count++] = cp;
  }
  if (bctx->batch_env_count < sizeof(bctx->batch_env) / sizeof(bctx->batch_env[0])) bctx->batch_env[bctx->batch_env_count] = NULL;
  bctx->dos_prog_abs = bctx->dir_state->dos_prog_abs;  /* Of the .bat file. */
  bctx->dir_state->dos_prog_abs = NULL;
  for (ai = 0; ai < 10; ++ai) bctx->batch_args[ai] = "";
  if (bctx->args) {
    for (ai = 0; bctx->args[ai] && bctx->batch_argc < 9; ++ai) {
      bctx->batch_args[++bctx->batch_argc] = bctx->args[ai];
    }
  }
  if ((bctx->batch_fd = open(bctx->prog_filename, O_RDONLY)) < 0) {
    fprintf(stderr, "fatal: cannot open DOS .bat batch file: %s: %s\n", bctx->prog_filename, strerror(errno));
    exit(252);
  }
  for (;;) {
    if (bctx->batch_eof && bctx->p_line == bctx->p) break;
    size = bctx->buf + sizeof(bctx->buf) - bctx->p;
    if (size == 0) { line_too_long:
      fprintf(stderr, "fatal: line too long in DOS .bat batch file: %s\n", bctx->prog_filename);
      exit(252);
    }
    if ((bctx->got = read(bctx->batch_fd, bctx->p, size)) < 0) {
      fprintf(stderr, "fatal: error reading from DOS .bat batch file: %s: %s\n", bctx->prog_filename, strerror(errno));
      exit(252);
    }
    if (bctx->got == 0) {
      if (bctx->p_line == bctx->p) break;
      *bctx->p = '\n'; bctx->got = 1;  /* Simulate trailing newline. There is room, `size == 0' has already been checked above. MS-DOS 6.22 does the same. */
    }
    bctx->q = bctx->p; bctx->p += bctx->got;
    if (bctx->q == bctx->p_line) {  /* Remove leading \r and \n from line. */
     next_line:
      for (; bctx->q != bctx->p && (*bctx->q == '\r' || *bctx->q == '\n'); ++bctx->q) {}
      bctx->p_line = bctx->q;
    }
    /* MS-DOS 6.22 doesn't recognize just \n as line terminator, but we do. */
    for (; bctx->q != bctx->p && *bctx->q != '\r' && *bctx->q != '\n' && *bctx->q != '\x1a'; ++bctx->q) {}
    if (bctx->q == bctx->p) {  /* End-of-line not yet read. */
      /* If >= 75% of the buffer is filled with an unfinished line, report an
       * error. This is to make sure that we're not spending most of our time in
       * memmove().
       */
      if ((size_t)(bctx->q - bctx->p_line) >= sizeof(bctx->buf) - (sizeof(bctx->buf) >> 2)) goto line_too_long;
      if (bctx->q == bctx->buf + sizeof(bctx->buf)) {
        memmove(bctx->buf, bctx->p_line, bctx->q - bctx->p_line);
        bctx->p = bctx->q = bctx->buf + (bctx->q - bctx->p_line);
        bctx->p_line = bctx->buf;
      }
	    } else {
      unsigned char lr = batch_process_line(bctx);
      if (lr == BL_EXIT) break;
      goto next_line;
    }
  }
  while (bctx->batch_env_count) free(bctx->batch_env[--bctx->batch_env_count]);
  while (bctx->setlocal_depth) {
    unsigned ei;
    --bctx->setlocal_depth;
    for (ei = 0; ei < bctx->setlocal_env_count[bctx->setlocal_depth]; ++ei) {
      free(bctx->setlocal_env[bctx->setlocal_depth][ei]);
    }
  }
  close(bctx->batch_fd);
  return bctx->exit_code;
}
