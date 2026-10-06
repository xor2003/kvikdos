#include "kvikdos.h"

/* DOS batch internal-command dispatch, split from run_dos_batch.
 * Each batch_cmd_* returns one of the DC_* codes; DC_PASS means the line did
 * not match any of its commands, so the next group is tried. */
int batch_cmd_c(struct BatchCtx *bctx) {
if (0 == memcmp(bctx->p_line, "if", bctx->cmd_size)) {
        char cond_ok = 0, is_not = 0;
        char *tail = bctx->arg;
        while (*tail == ' ' || *tail == '\t') ++tail;
        if ((tail[0] == 'n' || tail[0] == 'N') && (tail[1] == 'o' || tail[1] == 'O') && (tail[2] == 't' || tail[2] == 'T') && (tail[3] == ' ' || tail[3] == '\t')) {
          is_not = 1;
          tail += 4;
          while (*tail == ' ' || *tail == '\t') ++tail;
        }
        if ((tail[0] == 'e' || tail[0] == 'E') && (tail[1] == 'r' || tail[1] == 'R') && (tail[2] == 'r' || tail[2] == 'R') &&
            (tail[3] == 'o' || tail[3] == 'O') && (tail[4] == 'r' || tail[4] == 'R') && (tail[5] == 'l' || tail[5] == 'L') &&
            (tail[6] == 'e' || tail[6] == 'E') && (tail[7] == 'v' || tail[7] == 'V') && (tail[8] == 'e' || tail[8] == 'E') &&
            (tail[9] == 'l' || tail[9] == 'L') && (tail[10] == ' ' || tail[10] == '\t')) {
          unsigned n = 0;
          char *pnum = tail + 10;
          while (*pnum == ' ' || *pnum == '\t') ++pnum;
          while (*pnum >= '0' && *pnum <= '9') { n = n * 10 + (*pnum++ - '0'); }
          while (*pnum == ' ' || *pnum == '\t') ++pnum;
          cond_ok = bctx->exit_code >= n;
          tail = pnum;
        } else if ((tail[0] == 'e' || tail[0] == 'E') && (tail[1] == 'x' || tail[1] == 'X') && (tail[2] == 'i' || tail[2] == 'I') && (tail[3] == 's' || tail[3] == 'S') && (tail[4] == 't' || tail[4] == 'T') && (tail[5] == ' ' || tail[5] == '\t')) {
          char fn[260];
          char *pf = tail + 5;
          struct stat st;
          size_t fi = 0;
          while (*pf == ' ' || *pf == '\t') ++pf;
          if (*pf == '"') {
            ++pf;
            while (*pf && *pf != '"' && fi + 1 < sizeof(fn)) fn[fi++] = *pf++;
            if (*pf == '"') ++pf;
          } else {
            while (*pf && *pf != ' ' && *pf != '\t' && fi + 1 < sizeof(fn)) fn[fi++] = *pf++;
          }
          fn[fi] = '\0';
          while (*pf == ' ' || *pf == '\t') ++pf;
          if (fn[0] && (strchr(fn, '*') || strchr(fn, '?'))) {
            const char *pat_base = get_dos_basename(fn);
            size_t pat_dir_size = pat_base - fn;
            char dos_dir[DOS_PATH_SIZE + 4], linux_dir[LINUX_PATH_SIZE];
            DIR *dd = NULL;
            struct dirent *de;
            cond_ok = 0;
            if (pat_dir_size >= sizeof(dos_dir)) pat_dir_size = sizeof(dos_dir) - 1;
            memcpy(dos_dir, fn, pat_dir_size);
            dos_dir[pat_dir_size] = '\0';
            if (dos_dir[0] == '\0') strcpy(dos_dir, ".");
            if (*get_linux_filename_r(dos_dir, bctx->dir_state, linux_dir, NULL) && (dd = opendir(linux_dir)) != NULL) {
              while ((de = readdir(dd)) != NULL) {
                if (de->d_name[0] == '.') continue;
                if (dos_wildcard_match(pat_base, de->d_name)) { cond_ok = 1; break; }
              }
              closedir(dd);
            }
          } else {
            cond_ok = fn[0] && *get_linux_filename_r(fn, bctx->dir_state, fnbuf, NULL) && stat(fnbuf, &st) == 0;
          }
          tail = pf;
        } else {
          char *eqeq = strstr(tail, "==");
          if (eqeq) {
            char lhs[256], rhs[256];
            char *pc = tail;
            size_t li = 0, ri = 0;
            while (pc < eqeq && (*pc == ' ' || *pc == '\t')) ++pc;
            if (*pc == '"') ++pc;
            while (pc < eqeq && *pc != '"' && li + 1 < sizeof(lhs)) lhs[li++] = *pc++;
            while (li > 0 && (lhs[li - 1] == ' ' || lhs[li - 1] == '\t')) --li;
            lhs[li] = '\0';
            pc = eqeq + 2;
            while (*pc == ' ' || *pc == '\t') ++pc;
            if (*pc == '"') ++pc;
            while (*pc && *pc != '"' && *pc != ' ' && *pc != '\t' && ri + 1 < sizeof(rhs)) rhs[ri++] = *pc++;
            while (ri > 0 && (rhs[ri - 1] == ' ' || rhs[ri - 1] == '\t')) --ri;
            rhs[ri] = '\0';
            if (*pc == '"') ++pc;
            while (*pc == ' ' || *pc == '\t') ++pc;
            cond_ok = strcmp(lhs, rhs) == 0;
            tail = pc;
          }
        }
        if (is_not) cond_ok = !cond_ok;
        if (cond_ok && *tail) {
          memmove(bctx->p_line, tail, strlen(tail) + 1);
          return DC_REPARSE;
        }
        /* DOS IF does not update ERRORLEVEL by itself. */
        return DC_DONE;
      } else {
        char *args_str = bctx->p_line, args_buf[0x80], c2;
        const char* run_prog_fn;
        char prog_drive;
        size_t size;
        for (; (c2 = *args_str) != '\0' && c2 != ' ' && c2 != '\t' && c2 != '=' && c2 != ','; ++args_str) {}  /* MS-DOS 6.22. */
        if (args_str == bctx->p_line) {
          fprintf(stderr, "Empty DOS program name to run\r\n");
          bctx->exit_code = 1;
        } else if ((size = bctx->q - args_str) >= sizeof(args_buf) - 1) {  /* DOS doesn't support longer than 0x7e, including leading spaces. */
          fprintf(stderr, "DOS program arguments too long\r\n");
          bctx->exit_code = 1;
        } else {
          memcpy(args_buf, args_str, size + 1);  /* Including the trailing '\0'. */
          *args_str = '\0';  /* So that p_line becomes terminated by '\0'. */
          {
            const char *path_value = bctx->has_path_override ? bctx->path_override : getenv_prefix_nocase0("PATH=", (const char* const*)bctx->batch_env);
            bctx->dir_state->dos_prog_abs = bctx->dos_prog_abs;  /* Of the .bat file. */
            run_prog_fn = NULL;
            prog_drive = bctx->dir_state->drive;
            if (!strchr(bctx->p_line, ':') && !strchr(bctx->p_line, '\\') && !strchr(bctx->p_line, '/')) {
              struct stat st2;
              const char *cand = get_linux_filename_r(bctx->p_line, bctx->dir_state, fnbuf2, NULL);
              if (getenv("KVIKDOS_DEBUG_RESOLVE")) {
                fprintf(g_diag_file, "debug: resolve cmd=(%s) cand=(%s) cwd=%c:%s\n",
                        bctx->p_line, cand ? cand : "(null)", bctx->dir_state->drive, bctx->dir_state->current_dir[bctx->dir_state->drive - 'A']);
              }
              if (cand && *cand && stat(cand, &st2) == 0 && S_ISREG(st2.st_mode)) {
                copy_cstr0(fnbuf, sizeof(fnbuf), cand);
                run_prog_fn = fnbuf;
              }
            }
            if (!run_prog_fn) {
              run_prog_fn = find_prog_on_path(bctx->p_line, bctx->dir_state, path_value, &prog_drive);
            }
          }
          if (!run_prog_fn) {
            /* DOSBox 0.74-4 prints "Illegal command: %s.\r\n" to stdout, we print our error to stderr. */
            /* MS-DOS 6.22 prints this to stderr: "Bad command or file name\r\n". */
            fprintf(stderr, "Illegal command - %s\r\n", bctx->p_line);
            bctx->exit_code = 1;
          } else if (*run_prog_fn == '\0') {
            fprintf(stderr, "Invalid DOS program name - %s\r\n", bctx->p_line);
            bctx->exit_code = 1;
          } else {
            bctx->dir_state->dos_prog_abs = get_dos_abs_filename_r(run_prog_fn, prog_drive, bctx->dir_state, dosfnbuf);
            if (bctx->dir_state->dos_prog_abs[0] == '\0') {
              fprintf(stderr, "Error getting absolute filelename - %s\r\n", bctx->p_line);
              bctx->exit_code = 1;
            } else {
              const char *batch_extra_env[300];
              unsigned batch_extra_env_count = 0;
              char batch_path_env[1024 + 5];
              unsigned ei;
              if (bctx->has_path_override) {
                memcpy(batch_path_env, "PATH=", 5);
                strncpy(batch_path_env + 5, bctx->path_override, sizeof(batch_path_env) - 6);
                batch_path_env[sizeof(batch_path_env) - 1] = '\0';
                batch_extra_env[batch_extra_env_count++] = batch_path_env;
              }
              for (ei = 0; ei < bctx->batch_env_count && batch_extra_env_count < sizeof(batch_extra_env) / sizeof(batch_extra_env[0]); ++ei) {
                if (bctx->has_path_override &&
                    ((bctx->batch_env[ei][0] | 32) == 'p') &&
                    ((bctx->batch_env[ei][1] | 32) == 'a') &&
                    ((bctx->batch_env[ei][2] | 32) == 't') &&
                    ((bctx->batch_env[ei][3] | 32) == 'h') &&
                    bctx->batch_env[ei][4] == '=') continue;
                batch_extra_env[batch_extra_env_count++] = bctx->batch_env[ei];
              }
              if (has_dos_ext_nocase(run_prog_fn, ".bat")) {
                const char *child_args[64];
                char *ab = args_buf, *ae;
                unsigned ac = 0;
                while (*ab == ' ' || *ab == '\t') ++ab;
                while (*ab && ac + 1 < sizeof(child_args) / sizeof(child_args[0])) {
                  ae = ab;
                  while (*ae && *ae != ' ' && *ae != '\t') ++ae;
                  if (*ae) *ae++ = '\0';
                  child_args[ac++] = ab;
                  while (*ae == ' ' || *ae == '\t') ++ae;
                  ab = ae;
                }
                child_args[ac] = NULL;
                bctx->exit_code = run_dos_batch(bctx->emu, run_prog_fn, child_args, bctx->dir_state, bctx->tty_state, bctx->emu_params, bctx->envp0, batch_extra_env, batch_extra_env_count);
              } else {
                const enum mz_subformat_t subfmt = detect_mz_subformat(run_prog_fn);
                if (subfmt == MZ_SUBFMT_PE && has_wine_in_path() && !is_probable_dos_extender_program(run_prog_fn)) {
                  const char *child_argv[64];
                  char *ab = args_buf, *ae;
                  unsigned ac = 0;
                  char dos_cwd[DOS_PATH_SIZE + 4], dos_cwd_norm[DOS_PATH_SIZE + 4], linux_cwd[LINUX_PATH_SIZE];
                  size_t cwd_len;
                  const char *cdp = bctx->dir_state->current_dir[bctx->dir_state->drive - 'A'];
                  if (!cdp || !*cdp) cdp = "\\";
                  snprintf(dos_cwd, sizeof(dos_cwd), "%c:%s", bctx->dir_state->drive, cdp);
                  copy_cstr0(dos_cwd_norm, sizeof(dos_cwd_norm), dos_cwd);
                  cwd_len = strlen(dos_cwd_norm);
                  if (cwd_len > 3 && dos_cwd_norm[cwd_len - 1] == '\\') dos_cwd_norm[cwd_len - 1] = '\0';
                  get_linux_filename_r(dos_cwd_norm, bctx->dir_state, linux_cwd, NULL);
                  while (*ab == ' ' || *ab == '\t') ++ab;
                  while (*ab && ac + 1 < sizeof(child_argv) / sizeof(child_argv[0])) {
                    ae = ab;
                    while (*ae && *ae != ' ' && *ae != '\t') ++ae;
                    if (*ae) *ae++ = '\0';
                    child_argv[ac++] = ab;
                    while (*ae == ' ' || *ae == '\t') ++ae;
                    ab = ae;
                  }
                  child_argv[ac] = NULL;
                  bctx->exit_code = run_with_wine(run_prog_fn, child_argv, linux_cwd);
                } else {
                  bctx->exit_code = run_dos_prog(bctx->emu, run_prog_fn, NULL, args_buf, NULL, bctx->dir_state, bctx->tty_state, bctx->emu_params, bctx->envp0, batch_extra_env, batch_extra_env_count);
                }
              }
            }
          }
          bctx->dir_state->dos_prog_abs = NULL;  /* For security. */
        }
      }
  return DC_DONE;
}

int batch_dispatch(struct BatchCtx *bctx) {
  int dc = batch_cmd_a(bctx);
  if (dc == DC_PASS) dc = batch_cmd_b(bctx);
  if (dc == DC_PASS) dc = batch_cmd_c(bctx);
  return dc;
}
