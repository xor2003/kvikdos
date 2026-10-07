#include "kvikdos.h"

/* DOS batch internal-command dispatch, split from run_dos_batch.
 * Each batch_cmd_* returns one of the DC_* codes; DC_PASS means the line did
 * not match any of its commands, so the next group is tried. */
int batch_cmd_a(struct BatchCtx *bctx) {
  unsigned bi;
if (bctx->cmd_size == 1 && (bctx->p_line[0] & ~32)  - 'A' + 0U <= 'Z' - 'A' + 0U) {
        /* Ignore arguments arg...endarg, like MS-DOS 6.22 does. */
        char drive = bctx->p_line[0] & ~32;
        if (drive >= 'A' + DRIVE_COUNT || !bctx->dir_state->linux_mount_dir[drive - 'A']) {
          fprintf(stderr, "Invalid drive specification\r\n");  /* Like: MS-DOS 6.22. */
          bctx->exit_code = 1;
        }
      } else if (0 == memcmp(bctx->p_line, "rem", bctx->cmd_size)) {
        /* Comment, do nothing. */
      } else if (0 == memcmp(bctx->p_line, "cls", bctx->cmd_size)) {
        /* Ignore arguments arg...endarg, like MS-DOS 6.22 does. */
        fprintf(stdout, "\x1b[3J\x1b[H\x1b[2J");  /* xterm: tput clear */
        fflush(stdout);
        bctx->exit_code = 0;
      } else if (0 == memcmp(bctx->p_line, "echo", bctx->cmd_size)) {
        *bctx->endarg = bctx->c_endarg;  /* Don't strip trailing spaces. MS-DOS 6.22 doesn't strip, DOSBox 0.74-4 does strip. */
        if (*bctx->r == '\0') {
          fprintf(stdout, "ECHO is %s\r\n", bctx->do_echo ? "on" : "off");
        } else {
          if (0 == memcmp(bctx->arg, "on", 3)) {
            bctx->do_echo = 1;
          } else if (0 == memcmp(bctx->arg, "off", 4)) {
            bctx->do_echo = 0;
          } else {
            fprintf(stdout, "%s\r\n", bctx->arg);
          }
        }
        fflush(stdout);
        bctx->exit_code = 0;
      } else if (0 == memcmp(bctx->p_line, "set", bctx->cmd_size)) {
        if (*bctx->r != '\0') {
          char *sv = bctx->arg;
          char *name;
          char *eq = strchr(sv, '=');
          char *val;
          unsigned ei;
          char newev[1024];
          while (*sv == ' ' || *sv == '\t') ++sv;
          if (!eq || eq == sv) {
            fprintf(stderr, "Syntax error - %s\r\n", bctx->arg);
            bctx->exit_code = 1;
            return DC_DONE;
          }
          name = sv;
          val = eq + 1;
          *eq = '\0';
          for (; *sv; ++sv) if (*sv - 'a' + 0U <= 'z' - 'a' + 0U) *sv &= ~32;
          snprintf(newev, sizeof(newev), "%s=%s", name, val);
          for (ei = 0; ei < bctx->batch_env_count; ++ei) {
            char *ev = bctx->batch_env[ei];
            char *ee = strchr(ev, '=');
            if (ee && strcmp(ev, sv) == 0) { /* exact with '=' cut, impossible */ }
            if (ee && (size_t)(ee - ev) == strlen(name) && memcmp(ev, name, ee - ev) == 0) {
              free(bctx->batch_env[ei]);
              bctx->batch_env[ei] = xstrdup(newev);
              if (!bctx->batch_env[ei]) { perror("fatal: xstrdup"); exit(252); }
              break;
            }
          }
          if (ei == bctx->batch_env_count) {
            if (bctx->batch_env_count >= sizeof(bctx->batch_env) / sizeof(bctx->batch_env[0])) {
              fprintf(stderr, "fatal: too many set variables\r\n");
              exit(252);
            }
            bctx->batch_env[bctx->batch_env_count] = xstrdup(newev);
            if (!bctx->batch_env[bctx->batch_env_count]) { perror("fatal: xstrdup"); exit(252); }
            ++bctx->batch_env_count;
          }
          if (strcmp(name, "PATH") == 0) {
            strncpy(bctx->path_override, val, sizeof(bctx->path_override) - 1);
            bctx->path_override[sizeof(bctx->path_override) - 1] = '\0';
            bctx->has_path_override = 1;
          }
          *eq = '=';
        } else {
          if (bctx->batch_env_count) {
            for (bi = 0; bi < bctx->batch_env_count; ++bi) {
              fprintf(stdout, "%s\r\n", bctx->batch_env[bi]);
            }
          } else {
            fprintf(stdout, "$=\r\n");  /* See run_dos_prog above. */
          }
          fflush(stdout);
        }
        bctx->exit_code = 0;
      } else if (0 == memcmp(bctx->p_line, "for", bctx->cmd_size)) {
        char *fp = bctx->arg;
        char items[1024], body[2048];
        char *ip, *bp;
        char for_var = '\0';
        while (*fp == ' ' || *fp == '\t') ++fp;
        if (fp[0] == '%' && fp[1] == '%' && fp[2] != '\0') { for_var = fp[2]; fp += 3; }
        else if (fp[0] == '%' && fp[1] != '\0') { for_var = fp[1]; fp += 2; }
        while (*fp == ' ' || *fp == '\t') ++fp;
        if ((fp[0] | 32) != 'i' || (fp[1] | 32) != 'n') { bctx->exit_code = 1; return DC_DONE; }
        fp += 2;
        while (*fp == ' ' || *fp == '\t') ++fp;
        if (*fp != '(') { bctx->exit_code = 1; return DC_DONE; }
        ++fp;
        ip = items;
        while (*fp && *fp != ')' && ip + 1 < items + sizeof(items)) *ip++ = *fp++;
        *ip = '\0';
        if (*fp != ')') { bctx->exit_code = 1; return DC_DONE; }
        ++fp;
        while (*fp == ' ' || *fp == '\t') ++fp;
        if ((fp[0] | 32) != 'd' || (fp[1] | 32) != 'o') { bctx->exit_code = 1; return DC_DONE; }
        fp += 2;
        while (*fp == ' ' || *fp == '\t') ++fp;
        strncpy(body, fp, sizeof(body) - 1);
        body[sizeof(body) - 1] = '\0';
        bp = items;
        bctx->exit_code = 0;
        while (*bp) {
          char tok[256], *tp = tok;
          char exp[2048], *ep = exp;
          const char *bs = body;
          char tmpbat[] = "/tmp/kvikdos_forXXXXXX";
          int tfd;
          while (*bp == ' ' || *bp == '\t') ++bp;
          if (!*bp) break;
          while (*bp && *bp != ' ' && *bp != '\t' && tp + 1 < tok + sizeof(tok)) *tp++ = *bp++;
          *tp = '\0';
          while (*bs && ep + 2 < exp + sizeof(exp)) {
            if (bs[0] == '%' && bs[1] == '%' && upper_ascii(bs[2]) == upper_ascii(for_var)) {
              const char *tv = tok;
              while (*tv && ep + 1 < exp + sizeof(exp)) *ep++ = *tv++;
              bs += 3;
            } else if (bs[0] == '%' && upper_ascii(bs[1]) == upper_ascii(for_var)) {
              const char *tv = tok;
              while (*tv && ep + 1 < exp + sizeof(exp)) *ep++ = *tv++;
              bs += 2;
            } else {
              *ep++ = *bs++;
            }
          }
          *ep++ = '\n';
          *ep = '\0';
          tfd = mkstemp(tmpbat);
          if (tfd < 0) { bctx->exit_code = 1; break; }
          (void)!write(tfd, exp, strlen(exp));
          close(tfd);
          bctx->exit_code = run_dos_batch(bctx->emu, tmpbat, NULL, bctx->dir_state, bctx->tty_state, bctx->emu_params, bctx->envp0, (const char* const*)bctx->batch_env, bctx->batch_env_count);
          unlink(tmpbat);
        }
      } else if (0 == memcmp(bctx->p_line, "setlocal", bctx->cmd_size)) {
        unsigned ei;
        if (bctx->setlocal_depth >= sizeof(bctx->setlocal_env) / sizeof(bctx->setlocal_env[0])) {
          bctx->exit_code = 1;
        } else {
          bctx->setlocal_env_count[bctx->setlocal_depth] = bctx->batch_env_count;
          for (ei = 0; ei < bctx->batch_env_count; ++ei) {
            bctx->setlocal_env[bctx->setlocal_depth][ei] = xstrdup(bctx->batch_env[ei]);
            if (!bctx->setlocal_env[bctx->setlocal_depth][ei]) { perror("fatal: xstrdup"); exit(252); }
          }
          ++bctx->setlocal_depth;
          bctx->exit_code = 0;
        }
      } else if (0 == memcmp(bctx->p_line, "endlocal", bctx->cmd_size)) {
        unsigned ei;
        if (bctx->setlocal_depth == 0) {
          bctx->exit_code = 1;
        } else {
          while (bctx->batch_env_count) free(bctx->batch_env[--bctx->batch_env_count]);
          --bctx->setlocal_depth;
          for (ei = 0; ei < bctx->setlocal_env_count[bctx->setlocal_depth]; ++ei) {
            bctx->batch_env[bctx->batch_env_count++] = bctx->setlocal_env[bctx->setlocal_depth][ei];
            bctx->setlocal_env[bctx->setlocal_depth][ei] = NULL;
          }
          bctx->exit_code = 0;
        }
      } else if (0 == memcmp(bctx->p_line, "ver", bctx->cmd_size)) {
        if (*bctx->r != '\0') {
          fprintf(stderr, "Too many parameters - %s\r\n", bctx->r);  /* Like: MS-DOS 6.22. */
          bctx->exit_code = 1;
        } else {
          fprintf(stdout, "\r\nkvikdos\r\n\r\n");
          fflush(stdout);
          bctx->exit_code = 0;
        }
      } else if (0 == memcmp(bctx->p_line, "exit", bctx->cmd_size)) {
        /* pts-fast-dosbox. */
        unsigned exit_code2, n;
        if (is_same_ascii_nocase(bctx->arg, "/and", 5)) {  /* Exit only if the previous command has failed (errorlevel 1 or larger). */
          if (bctx->exit_code != 0) return DC_EXIT;
        } else if (is_same_ascii_nocase(bctx->arg, "/or", 4)) {  /* Exit only if the previous command has succeeded. */
          if (bctx->exit_code == 0) return DC_EXIT;
        } else if (is_same_ascii_nocase(bctx->arg, "/ec", 4)) {
          /* Use `exit /ec' to propagate the exit code (al in int 0x21 call with ah == 0x4c) of the last program. */
          return DC_EXIT;
        } else if (is_same_ascii_nocase(bctx->arg, "/true", 6)) {
          bctx->exit_code = 0; /* Don't exit, but reset errorlevel to 0. */
        } else if (sscanf(bctx->arg, "%u%n", &exit_code2, (int*)&n) == 1 && strlen(bctx->arg) == n && exit_code2 < 256) {
          bctx->exit_code = exit_code2;  /* No need for `& 255', it's already Bit8u. */
          return DC_EXIT;
        } else {
          bctx->exit_code = 0;
          return DC_EXIT;
        }
      } else if (0 == memcmp(bctx->p_line, "cd", bctx->cmd_size)) {
        if (*bctx->r == '\0') {
          const char *current_dir = bctx->dir_state->current_dir[bctx->dir_state->drive - 'A'];
          fprintf(stdout, "%c:%s\r\n", bctx->dir_state->drive, *current_dir == '\0' ? "\\" : current_dir);
          fflush(stdout);
          bctx->exit_code = 0;
        } else {
          char tmp[DOS_PATH_SIZE];
          char *t = tmp;
          const char *s = bctx->arg;
          char drive = bctx->dir_state->drive;
          struct stat st;
          if ((s[0] & ~32) - 'A' + 0U < DRIVE_COUNT && s[1] == ':') {
            drive = s[0] & ~32;
            s += 2;
            if (*s == '\\' || *s == '/') {
              ++s;
            }
          } else if (*s == '\\' || *s == '/') {
            ++s;
          }
          while (*s && t + 2 < tmp + sizeof(tmp)) {
            char c3 = *s++;
            if (c3 == '/') c3 = '\\';
            *t++ = ((unsigned)c3 - 'a' + 0U <= 'z' - 'a' + 0U) ? (c3 & ~32) : c3;
          }
          if (t != tmp && t[-1] != '\\') *t++ = '\\';
          *t = '\0';
          if (tmp[0] == '\0') strcpy(tmp, "\\");
          {
            char dos_abs[DOS_PATH_SIZE + 4];
            char tmp_for_check[DOS_PATH_SIZE];
            size_t tlen = strlen(tmp);
            copy_cstr0(tmp_for_check, sizeof(tmp_for_check), tmp);
            /* get_linux_filename_r rejects a trailing slash, so trim for existence checks.
             * Keep "\" intact for drive root.
             */
            if (tlen > 1 && tmp_for_check[tlen - 1] == '\\') tmp_for_check[tlen - 1] = '\0';
            snprintf(dos_abs, sizeof(dos_abs), "%c:%s", drive, tmp_for_check);
            {
              char *linux_path = get_linux_filename_r(dos_abs, bctx->dir_state, fnbuf, NULL);
              if (getenv("KVIKDOS_DEBUG_CD")) {
                fprintf(g_diag_file, "debug: cd arg=(%s) dos_abs=(%s) linux=(%s) drive=%c case=%d\n",
                        bctx->arg, dos_abs, linux_path, drive, bctx->dir_state->case_mode[drive - 'A']);
              }
              if (*linux_path == '\0' || stat(linux_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                fprintf(stderr, "Invalid directory - %s\r\n", bctx->arg);
                bctx->exit_code = 1;
              } else {
                copy_cstr0(bctx->dir_state->current_dir[drive - 'A'], sizeof(bctx->dir_state->current_dir[drive - 'A']), tmp);
                bctx->dir_state->drive = drive;
                bctx->exit_code = 0;
              }
            }
          }
        }
      } else if (0 == memcmp(bctx->p_line, "path", bctx->cmd_size)) {
        if (*bctx->r != '\0') {
          while (*bctx->arg == ' ' || *bctx->arg == '\t') ++bctx->arg;
          if (*bctx->arg == '=') ++bctx->arg;
          while (*bctx->arg == ' ' || *bctx->arg == '\t') ++bctx->arg;
          strncpy(bctx->path_override, bctx->arg, sizeof(bctx->path_override) - 1);
          bctx->path_override[sizeof(bctx->path_override) - 1] = '\0';
          bctx->has_path_override = 1;
          { /* sync PATH in batch env */
            char pe[sizeof(bctx->path_override) + 6];
            unsigned ei;
            memcpy(pe, "PATH=", 5);
            strcpy(pe + 5, bctx->path_override);
            for (ei = 0; ei < bctx->batch_env_count; ++ei) {
              char *eq = strchr(bctx->batch_env[ei], '=');
              if (eq && (size_t)(eq - bctx->batch_env[ei]) == 4 &&
                  ((bctx->batch_env[ei][0] | 32) == 'p') &&
                  ((bctx->batch_env[ei][1] | 32) == 'a') &&
                  ((bctx->batch_env[ei][2] | 32) == 't') &&
                  ((bctx->batch_env[ei][3] | 32) == 'h')) {
                free(bctx->batch_env[ei]);
                bctx->batch_env[ei] = xstrdup(pe);
                break;
              }
            }
            if (ei == bctx->batch_env_count && bctx->batch_env_count < sizeof(bctx->batch_env) / sizeof(bctx->batch_env[0])) {
              bctx->batch_env[bctx->batch_env_count++] = xstrdup(pe);
            }
            if (bctx->batch_env_count < sizeof(bctx->batch_env) / sizeof(bctx->batch_env[0])) bctx->batch_env[bctx->batch_env_count] = NULL;
          }
          bctx->exit_code = 0;
          return DC_DONE;
        }
        if (bctx->has_path_override) {
          fprintf(stdout, "PATH=%s\r\n", bctx->path_override);
          bctx->exit_code = 0;
        } else {
          {
            const char *path_value = getenv_prefix_nocase0("PATH=", (const char* const*)bctx->batch_env);
            if (path_value) {
              fprintf(stdout, "PATH=%s\r\n", path_value);
              bctx->exit_code = 0;
            } else {
              fprintf(stdout, "No Path\r\n\r\n");  /* MS-DOS 6.22. */
              bctx->exit_code = 1;
            }
          }
        }
        fflush(stdout);
      }
      else { return DC_PASS; }
  return DC_DONE;
}

