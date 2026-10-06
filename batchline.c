#include "kvikdos.h"

unsigned char batch_process_line(struct BatchCtx *bctx) {
      char c;
      
      bctx->do_echo_line = bctx->do_echo;
      bctx->c_endarg = 0;
      bctx->r = bctx->arg = bctx->endarg = NULL;
      bctx->cmd_size = 0;
      bctx->q_src = bctx->q;
      bctx->rewound = 0;
      bctx->pipe_fd = bctx->pipe_save_out = bctx->pipe_save_in = -1;
      bctx->pipe_stage = 0;
      bctx->saved_stdin = bctx->saved_stdout = bctx->saved_stderr = -1;
      bctx->has_redir_in = bctx->has_redir_out = bctx->has_redir_err = 0;
      bctx->append_out = bctx->append_err = 0;
          bctx->redir_in[0] = bctx->redir_out[0] = bctx->redir_err[0] = '\0';
          if (*bctx->q == '\x1a') bctx->batch_eof = 1;
	      *bctx->q = '\0';  /* Make it ASCIIZ (terminated by \0). */
	      if (DEBUG || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: batch line: (%s)\n", bctx->p_line);
      if (*bctx->p_line == ':') {  /* label */
        if (bctx->have_goto) {
          const char *ln = bctx->p_line + 1;
          while (*ln == ' ' || *ln == '\t') ++ln;
          if (is_same_ascii_nocase(ln, bctx->goto_label, strlen(bctx->goto_label) + 1)) bctx->have_goto = 0;
        }
        goto done_command;
      }
      if (bctx->have_goto) goto done_command;
	      { /* %VAR% substitution. */
	        char subst[4096];
	        char *d = subst;
	        const char *s2 = bctx->p_line;
	        while (*s2 && d + 2 < subst + sizeof(subst)) {
          if (*s2 == '%' && s2[1] >= '0' && s2[1] <= '9') {
            const char *av = bctx->batch_args[s2[1] - '0'];
            while (*av && d + 1 < subst + sizeof(subst)) *d++ = *av++;
            s2 += 2;
            continue;
          } else if (*s2 == '%' && s2[1] == '%') {
            *d++ = '%';
            s2 += 2;
            continue;
          }
          if (*s2 == '%') {
            const char *e2 = strchr(s2 + 1, '%');
            if (e2 && e2 > s2 + 1) {
              char name[128];
              size_t ns = e2 - (s2 + 1);
              unsigned ei;
              if (ns >= sizeof(name)) ns = sizeof(name) - 1;
              memcpy(name, s2 + 1, ns);
              name[ns] = '\0';
              for (ei = 0; ei < ns; ++ei) if (name[ei] - 'a' + 0U <= 'z' - 'a' + 0U) name[ei] &= ~32;
              for (ei = 0; ei < bctx->batch_env_count; ++ei) {
                const char *ev = bctx->batch_env[ei];
                const char *eq = strchr(ev, '=');
                if (eq && (size_t)(eq - ev) == ns && memcmp(ev, name, ns) == 0) {
                  const char *vv = eq + 1;
                  while (*vv && d + 1 < subst + sizeof(subst)) *d++ = *vv++;
                  break;
                }
              }
              s2 = e2 + 1;
              continue;
            }
          }
          *d++ = *s2++;
	        }
	        *d = '\0';
	        strncpy(bctx->cmdline, subst, sizeof(bctx->cmdline) - 1);
	        bctx->cmdline[sizeof(bctx->cmdline) - 1] = '\0';
	        bctx->p_line = bctx->cmdline;
	        bctx->q = bctx->p_line + strlen(bctx->p_line);
	      }
      for (; *bctx->p_line == ' ' || *bctx->p_line == '\t'; ++bctx->p_line) {}  /* MS-DOS 6.22 doesn't ignore leading whitespace, at least not before `rem'. */
      if (*bctx->p_line == '@') { bctx->do_echo_line = 0; ++bctx->p_line; }
      if (bctx->do_echo_line) {
        fprintf(stdout, "%s\r\n", bctx->p_line);
        fflush(stdout);
      }
      for (bctx->r = bctx->p_line; ((c = *bctx->r) + 0U > 31U && c != '\x7f') || c == ' ' || c == '\t'; ++bctx->r) {}
      if (c != '\0') {
        fprintf(stderr, "fatal: invalid character in DOS .bat batch file: %s\n", bctx->prog_filename);
        exit(252);
      }
      {  /* Parse simple redirections: <, >, >>, 2>, 2>> */
        const char *rs = bctx->p_line;
        char *cd = bctx->cleaned;
        while (*rs && cd + 2 < bctx->cleaned + sizeof(bctx->cleaned)) {
          if ((rs[0] == '2' || rs[0] == '1') && rs[1] == '>' && rs[2] == '&' && (rs[3] == '1' || rs[3] == '2')) {
            int from_fd = rs[0] - '0', to_fd = rs[3] - '0';
            int dfd = dup(to_fd);
            if (dfd >= 0) { dup2(dfd, from_fd); close(dfd); }
            rs += 4;
            continue;
          }
          if ((rs[0] == '1' || rs[0] == '2') && rs[1] == '>') {
            char is_err = (rs[0] == '2');
            char *dst = is_err ? bctx->redir_err : bctx->redir_out;
            char *dst_end = (is_err ? bctx->redir_err : bctx->redir_out) + sizeof(bctx->redir_out) - 1;
            rs += 2;
            if (is_err) bctx->append_err = (*rs == '>');
            else bctx->append_out = (*rs == '>');
            if (*rs == '>') ++rs;
            while (*rs == ' ' || *rs == '\t') ++rs;
            if (is_err) bctx->has_redir_err = 1; else bctx->has_redir_out = 1;
            if (*rs == '"') {
              ++rs;
              while (*rs && *rs != '"' && dst != dst_end) *dst++ = *rs++;
              if (*rs == '"') ++rs;
            } else {
              while (*rs && *rs != ' ' && *rs != '\t' && *rs != '<' && *rs != '>' && dst != dst_end) *dst++ = *rs++;
            }
            *dst = '\0';
            continue;
          }
          if (rs[0] == '2' && rs[1] == '>') {
            char *dst = bctx->redir_err, *dst_end = bctx->redir_err + sizeof(bctx->redir_err) - 1;
            rs += 2;
            bctx->append_err = (*rs == '>');
            if (bctx->append_err) ++rs;
            while (*rs == ' ' || *rs == '\t') ++rs;
            bctx->has_redir_err = 1;
            if (*rs == '"') {
              ++rs;
              while (*rs && *rs != '"' && dst != dst_end) *dst++ = *rs++;
              if (*rs == '"') ++rs;
            } else {
              while (*rs && *rs != ' ' && *rs != '\t' && *rs != '<' && *rs != '>' && dst != dst_end) *dst++ = *rs++;
            }
            *dst = '\0';
            continue;
          } else if (*rs == '>' || *rs == '<') {
            char is_out = (*rs == '>');
            char *dst = is_out ? bctx->redir_out : bctx->redir_in;
            char *dst_end = dst + DOS_PATH_SIZE + 4 - 1;
            char *app = is_out ? &bctx->append_out : &bctx->append_err;  /* append_err ignored for input */
            ++rs;
            if (is_out && *rs == '>') { *app = 1; ++rs; } else *app = 0;
            while (*rs == ' ' || *rs == '\t') ++rs;
            if (is_out) bctx->has_redir_out = 1; else bctx->has_redir_in = 1;
            if (*rs == '"') {
              ++rs;
              while (*rs && *rs != '"' && dst != dst_end) *dst++ = *rs++;
              if (*rs == '"') ++rs;
            } else {
              while (*rs && *rs != ' ' && *rs != '\t' && *rs != '<' && *rs != '>' && dst != dst_end) *dst++ = *rs++;
            }
            *dst = '\0';
            continue;
          }
          *cd++ = *rs++;
        }
        *cd = '\0';
        {  /* Single pipeline: left | right */
          char *pp = bctx->cleaned;
          while (*pp && *pp != '|') ++pp;
          if (*pp == '|') {
            char *ls_end;
            char *rs2;
            *pp++ = '\0';
            while (*pp == ' ' || *pp == '\t') ++pp;
            strncpy(bctx->pipe_right, pp, sizeof(bctx->pipe_right) - 1);
            bctx->pipe_right[sizeof(bctx->pipe_right) - 1] = '\0';
            ls_end = bctx->cleaned + strlen(bctx->cleaned);
            while (ls_end != bctx->cleaned && (ls_end[-1] == ' ' || ls_end[-1] == '\t')) *--ls_end = '\0';
            rs2 = bctx->pipe_right;
            while (*rs2 == ' ' || *rs2 == '\t') ++rs2;
            if (rs2 != bctx->pipe_right) memmove(bctx->pipe_right, rs2, strlen(rs2) + 1);
            bctx->pipe_stage = 1;
          }
        }
        strncpy(bctx->cmdline, bctx->cleaned, sizeof(bctx->cmdline) - 1);
        bctx->cmdline[sizeof(bctx->cmdline) - 1] = '\0';
        bctx->p_line = bctx->cmdline;
        bctx->q = bctx->p_line + strlen(bctx->p_line);
      }
      /* Allow unresolved %...% to pass through as literal text. */
     reparse_command:
      /* MS-DOS 6.22 terminator characters. */
      for (bctx->r = bctx->p_line; (c = *bctx->r) != '\0' && c != ' ' && c != '\t' && c != '+' && c != '=' && c != '[' && c != ']' && c != '"' && c != '\\' && c != ':' && c != ';' /* && c != '|' && c != '<' && c != '>' */ && c != ',' && c != '.' && c != '/'; ++bctx->r) {}
      bctx->cmd_size = bctx->r - bctx->p_line;
      for (bctx->arg = bctx->p_line; bctx->arg != bctx->r; ++bctx->arg) {
        if (*bctx->arg - 'A' + 0U <= 'Z' - 'A' + 0U) *bctx->arg |= 32;  /* Convert to lowercase. */
      }
      if (bctx->cmd_size == 0) goto done_command;  /* Empty command. */
      for (bctx->arg = bctx->r; *bctx->arg == ' ' || *bctx->arg == '\t'; ++bctx->arg) {}
      for (bctx->endarg = bctx->q; bctx->endarg != bctx->r && (bctx->endarg[-1] == ' ' || bctx->endarg[-1] == '\t'); --bctx->endarg) {}
      bctx->c_endarg = *bctx->endarg;
      *bctx->endarg = '\0';  /* MS-DOS 6.22 passes trailing spaces to .com or .exe programs, but DOSBox 0.74-4 doesn't. We don't. This also affects the `echo' command in DOSBox 0.74-4, but for that we add trailing spaces. */
      {  /* Apply redirections for this command. */
        int fd;
        if (bctx->pipe_stage == 1) {
          char pipe_tmp[] = "/tmp/kvikdos_pipeXXXXXX";
          bctx->pipe_fd = mkstemp(pipe_tmp);
          if (bctx->pipe_fd < 0) {
            fprintf(stderr, "Cannot create pipe temp\r\n");
            bctx->exit_code = 1;
            goto done_command;
          }
          unlink(pipe_tmp);
          bctx->pipe_save_out = dup(1);
          dup2(bctx->pipe_fd, 1);
        }
        if (bctx->has_redir_in) {
          if (*get_linux_filename_r(bctx->redir_in, bctx->dir_state, fnbuf, NULL) == '\0' || (fd = open_with_case_fallback(fnbuf, O_RDONLY, 0666)) < 0) {
            fprintf(stderr, "File not found - %s\r\n", bctx->redir_in);
            bctx->exit_code = 1;
            goto done_command;
          }
          bctx->saved_stdin = dup(0);
          dup2(fd, 0);
          close(fd);
        }
        if (bctx->has_redir_out) {
          int flags = O_WRONLY | O_CREAT | (bctx->append_out ? O_APPEND : O_TRUNC);
          if (*get_linux_filename_r(bctx->redir_out, bctx->dir_state, fnbuf, NULL) == '\0' || (fd = open_with_case_fallback(fnbuf, flags, 0666)) < 0) {
            fprintf(stderr, "Access denied - %s\r\n", bctx->redir_out);
            bctx->exit_code = 1;
            goto done_command;
          }
          bctx->saved_stdout = dup(1);
          dup2(fd, 1);
          close(fd);
        }
        if (bctx->has_redir_err) {
          int flags = O_WRONLY | O_CREAT | (bctx->append_err ? O_APPEND : O_TRUNC);
          if (*get_linux_filename_r(bctx->redir_err, bctx->dir_state, fnbuf, NULL) == '\0' || (fd = open_with_case_fallback(fnbuf, flags, 0666)) < 0) {
            fprintf(stderr, "Access denied - %s\r\n", bctx->redir_err);
            bctx->exit_code = 1;
            goto done_command;
          }
          bctx->saved_stderr = dup(2);
          dup2(fd, 2);
          close(fd);
        }
      }
      {
      int dc;
      dc = batch_dispatch(bctx);
      if (dc == DC_EXIT) return BL_EXIT;
      if (dc == DC_REPARSE) goto reparse_command;
      /* DC_DONE: fall through to done_command. */
    }

	     done_command:
          if (bctx->saved_stderr >= 0) { dup2(bctx->saved_stderr, 2); close(bctx->saved_stderr); }
          if (bctx->saved_stdout >= 0) { dup2(bctx->saved_stdout, 1); close(bctx->saved_stdout); }
          if (bctx->saved_stdin >= 0) { dup2(bctx->saved_stdin, 0); close(bctx->saved_stdin); }
          if (bctx->pipe_stage == 1) {
            fflush(stdout);
            dup2(bctx->pipe_save_out, 1);
            close(bctx->pipe_save_out); bctx->pipe_save_out = -1;
            lseek(bctx->pipe_fd, 0, SEEK_SET);
            bctx->pipe_save_in = dup(0);
            dup2(bctx->pipe_fd, 0);
            close(bctx->pipe_fd); bctx->pipe_fd = -1;
            strncpy(bctx->cmdline, bctx->pipe_right, sizeof(bctx->cmdline) - 1);
            bctx->cmdline[sizeof(bctx->cmdline) - 1] = '\0';
            bctx->p_line = bctx->cmdline;
            bctx->q = bctx->p_line + strlen(bctx->p_line);
            bctx->pipe_stage = 2;
            goto reparse_command;
          } else if (bctx->pipe_stage == 2) {
            dup2(bctx->pipe_save_in, 0);
            close(bctx->pipe_save_in); bctx->pipe_save_in = -1;
            bctx->pipe_stage = 0;
          }
          if (bctx->rewound) {
            bctx->q = bctx->p_line = bctx->buf;
            return BL_NEXT;
          }
	      bctx->q = bctx->q_src;
	      ++bctx->q;  /* Skip over the '\0', formerly '\r' or '\n'. */
	      return BL_NEXT;
	    
}
