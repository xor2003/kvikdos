#include "kvikdos.h"

/* DOS batch internal-command dispatch, split from run_dos_batch.
 * Each batch_cmd_* returns one of the DC_* codes; DC_PASS means the line did
 * not match any of its commands, so the next group is tried. */
int batch_cmd_b(struct BatchCtx *bctx) {
  unsigned ai;
if (0 == memcmp(bctx->p_line, "goto", bctx->cmd_size)) {
        const char *gl = bctx->arg;
        while (*gl == ' ' || *gl == '\t') ++gl;
        if (*gl == ':') ++gl;
        if (*gl == '\0') {
          fprintf(stderr, "Label not found - %s\r\n", bctx->arg);
          bctx->exit_code = 1;
        } else if (is_same_ascii_nocase(gl, "eof", 4)) {
          return DC_EXIT;  /* End current batch context. */
        } else {
          size_t gls = strlen(gl);
          if (gls >= sizeof(bctx->goto_label)) gls = sizeof(bctx->goto_label) - 1;
          memcpy(bctx->goto_label, gl, gls);
          bctx->goto_label[gls] = '\0';
          bctx->have_goto = 1;
          lseek(bctx->batch_fd, 0, SEEK_SET);
          bctx->p = bctx->p_line = bctx->buf;
          bctx->rewound = 1;
          bctx->exit_code = 0;
        }
      } else if (0 == memcmp(bctx->p_line, "shift", bctx->cmd_size)) {
        for (ai = 1; ai < 9; ++ai) bctx->batch_args[ai] = bctx->batch_args[ai + 1];
        bctx->batch_args[9] = "";
        if (bctx->batch_argc > 0) --bctx->batch_argc;
        bctx->exit_code = 0;
      } else if (0 == memcmp(bctx->p_line, "call", bctx->cmd_size)) {
        while (*bctx->arg == ' ' || *bctx->arg == '\t') ++bctx->arg;
        if (*bctx->arg) {
          memmove(bctx->p_line, bctx->arg, strlen(bctx->arg) + 1);
          return DC_REPARSE;
        }
        bctx->exit_code = 0;
      } else if (0 == memcmp(bctx->p_line, "del", bctx->cmd_size) || 0 == memcmp(bctx->p_line, "erase", bctx->cmd_size) || 0 == memcmp(bctx->p_line, "delete", bctx->cmd_size)) {
        if (*bctx->arg == '\0') {
          fprintf(stderr, "Required parameter missing\r\n");
          bctx->exit_code = 1;
        } else {
          char *a = bctx->arg;
          while (*a) {
            char *b = a;
            while (*b && *b != ' ' && *b != '\t') ++b;
	            if (*b) *b++ = '\0';
	            if (strchr(a, '*') || strchr(a, '?')) {
	              const char *pat_base = get_dos_basename(a);
	              size_t pat_dir_size = pat_base - a;
	              char dos_dir[DOS_PATH_SIZE + 4], linux_dir[LINUX_PATH_SIZE];
	              const char *scan_dir;
	              DIR *dd;
	              struct dirent *de;
	              int removed;
	              if (pat_dir_size >= sizeof(dos_dir)) pat_dir_size = sizeof(dos_dir) - 1;
	              memcpy(dos_dir, a, pat_dir_size);
	              dos_dir[pat_dir_size] = '\0';
	              if (dos_dir[0] == '\0') strcpy(dos_dir, ".");
	              scan_dir = linux_dir;
	              if (*get_linux_filename_r(dos_dir, bctx->dir_state, linux_dir, NULL) == '\0' || !(dd = opendir_with_case_fallback(linux_dir, fnbuf2, sizeof(fnbuf2)))) {
	                bctx->exit_code = 1;
	              } else {
	                if (fnbuf2[0] != '\0') scan_dir = fnbuf2;
	                removed = 0;
	                while ((de = readdir(dd)) != NULL) {
	                  char full[LINUX_PATH_SIZE];
	                  const char *nm = de->d_name;
	                  struct stat st;
	                  size_t dlen;
	                  if (nm[0] == '.') continue;
	                  if (!dos_wildcard_match(pat_base, nm)) continue;
	                  dlen = strlen(scan_dir);
	                  if (dlen + 1 + strlen(nm) + 1 >= sizeof(full)) continue;
	                  memcpy(full, scan_dir, dlen);
	                  if (dlen && full[dlen - 1] != '/') full[dlen++] = '/';
	                  strcpy(full + dlen, nm);
	                  if (stat(full, &st) == 0 && S_ISREG(st.st_mode) && unlink(full) == 0) removed = 1;
	                }
	                closedir(dd);
	                if (!removed) bctx->exit_code = 1;
	              }
	            } else if (*get_linux_filename_r(a, bctx->dir_state, fnbuf, NULL) == '\0' || unlink(fnbuf) != 0) {
	              bctx->exit_code = 1;
	            }
            while (*b == ' ' || *b == '\t') ++b;
            a = b;
          }
        }
      } else if (0 == memcmp(bctx->p_line, "mkdir", bctx->cmd_size) || 0 == memcmp(bctx->p_line, "md", bctx->cmd_size)) {
        if (*bctx->arg == '\0') {
          fprintf(stderr, "Required parameter missing\r\n");
          bctx->exit_code = 1;
        } else if (*get_linux_filename_r(bctx->arg, bctx->dir_state, fnbuf, NULL) == '\0' || mkdir(fnbuf, 0777) != 0) {
          bctx->exit_code = 1;
        } else {
          bctx->exit_code = 0;
        }
      } else if (0 == memcmp(bctx->p_line, "copy", bctx->cmd_size)) {
        char *a = bctx->arg, *b, *c;
        int fd1 = -1, fd2 = -1;
        while (*a == ' ' || *a == '\t') ++a;
        b = a;
        while (*b && *b != ' ' && *b != '\t') ++b;
        if (*b) *b++ = '\0';
        while (*b == ' ' || *b == '\t') ++b;
        c = b;
        while (*c && *c != ' ' && *c != '\t') ++c;
        *c = '\0';
        if (*a == '\0' || *b == '\0') {
          fprintf(stderr, "Required parameter missing\r\n");
          bctx->exit_code = 1;
        } else {
          if (strchr(a, '*') || strchr(a, '?')) {
              const char *pat_base = get_dos_basename(a);
              size_t pat_dir_size = pat_base - a;
              char dos_dir[DOS_PATH_SIZE + 4], linux_src_dir[LINUX_PATH_SIZE];
              const char *src_scan_dir = linux_src_dir;
              DIR *dd = NULL;
              struct dirent *de;
              struct stat dst_st;
              int copied = 0;
            if (pat_dir_size >= sizeof(dos_dir)) pat_dir_size = sizeof(dos_dir) - 1;
            memcpy(dos_dir, a, pat_dir_size);
            dos_dir[pat_dir_size] = '\0';
              if (dos_dir[0] == '\0') strcpy(dos_dir, ".");
              if (*get_linux_filename_r(dos_dir, bctx->dir_state, linux_src_dir, NULL) == '\0' ||
                *get_linux_filename_r(b, bctx->dir_state, fnbuf2, NULL) == '\0' ||
                stat_with_case_fallback(fnbuf2, &dst_st, 0) != 0 || !S_ISDIR(dst_st.st_mode) ||
                (dd = opendir_with_case_fallback(linux_src_dir, exec_fnbuf, sizeof(exec_fnbuf))) == NULL) {
                bctx->exit_code = 1;
              } else {
                if (exec_fnbuf[0] != '\0') src_scan_dir = exec_fnbuf;
                while ((de = readdir(dd)) != NULL) {
                char src_full[LINUX_PATH_SIZE], dst_full[LINUX_PATH_SIZE];
                const char *nm = de->d_name;
                struct stat st;
                size_t sdl, ddl;
                if (nm[0] == '.') continue;
                if (!dos_wildcard_match(pat_base, nm)) continue;
                sdl = strlen(src_scan_dir);
                ddl = strlen(fnbuf2);
                if (sdl + 1 + strlen(nm) + 1 >= sizeof(src_full) || ddl + 1 + strlen(nm) + 1 >= sizeof(dst_full)) continue;
                memcpy(src_full, src_scan_dir, sdl);
                if (sdl && src_full[sdl - 1] != '/') src_full[sdl++] = '/';
                strcpy(src_full + sdl, nm);
                if (stat_with_case_fallback(src_full, &st, 0) != 0 || !S_ISREG(st.st_mode)) continue;
                memcpy(dst_full, fnbuf2, ddl);
                if (ddl && dst_full[ddl - 1] != '/') dst_full[ddl++] = '/';
                strcpy(dst_full + ddl, nm);
                fd1 = open_with_case_fallback(src_full, O_RDONLY, 0666);
                fd2 = open_with_case_fallback(dst_full, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd1 < 0 || fd2 < 0) {
                  if (fd1 >= 0) close(fd1);
                  if (fd2 >= 0) close(fd2);
                  bctx->exit_code = 1;
                  break;
                } else {
                  char cbuf[4096];
                  int n;
                  while ((n = read(fd1, cbuf, sizeof(cbuf))) > 0) {
                    if (write(fd2, cbuf, n) != n) { bctx->exit_code = 1; break; }
                  }
                  if (n < 0) bctx->exit_code = 1;
                  close(fd1);
                  close(fd2);
                  fd1 = fd2 = -1;
                  if (bctx->exit_code) break;
                  copied = 1;
                }
              }
              closedir(dd);
              if (!copied) bctx->exit_code = 1;
            }
          } else if (*get_linux_filename_r(a, bctx->dir_state, fnbuf, NULL) == '\0' || *get_linux_filename_r(b, bctx->dir_state, fnbuf2, NULL) == '\0') {
            bctx->exit_code = 1;
          } else if ((fd1 = open_with_case_fallback(fnbuf, O_RDONLY, 0666)) < 0 || (fd2 = open_with_case_fallback(fnbuf2, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
            if (fd1 >= 0) close(fd1);
            if (fd2 >= 0) close(fd2);
            bctx->exit_code = 1;
          } else {
            char cbuf[4096];
            int n;
            bctx->exit_code = 0;
            while ((n = read(fd1, cbuf, sizeof(cbuf))) > 0) {
              if (write(fd2, cbuf, n) != n) { bctx->exit_code = 1; break; }
            }
            if (n < 0) bctx->exit_code = 1;
            close(fd1);
            close(fd2);
            fd1 = fd2 = -1;
          }
        }
      } else if (0 == memcmp(bctx->p_line, "pause", bctx->cmd_size)) {
        unsigned short dummy_ax;
        /* Ignore arguments arg...endarg, like MS-DOS 6.22 does. */
        fprintf(stdout, "Press any key to continue.\r\n");  /* DOSBox 0.74-4. MS-DOS 6.22 prints more dots. */
        process_key(bctx->tty_state, 0, &dummy_ax, &dummy_ax);
        bctx->exit_code = 0;
      } else if (0 == memcmp(bctx->p_line, "type", bctx->cmd_size)) {
        char *arg2 = bctx->arg, c2;
        for (; (c2 = (*arg2 != '\0')) && c2 != ' ' && c2 != '\t' && c2 != '+' && c2 != '=' && c2 != '/' && c2 != '[' && c2 != ']' && c2 != ';' && c2 != ',' && c2 != '"'; ++arg2) {}  /* MS-DOS 6.22. */
        if (c2 != '\0') {
          fprintf(stderr, "Too many parameters - %s\r\n", arg2 + 1);
          bctx->exit_code = 1;
        } else if (*bctx->arg == '\0') {  /* Read from stdin (useful for pipelines). */
          char fbuf[4096];
          while ((bctx->got = read(0, fbuf, sizeof(fbuf))) > 0) (void)!write(1, fbuf, bctx->got);
          bctx->exit_code = bctx->got < 0;
        } else if (strchr(bctx->arg, '*') || strchr(bctx->arg, '?')) {
          const char *pat_base = get_dos_basename(bctx->arg);
          size_t pat_dir_size = pat_base - bctx->arg;
          char dos_dir[DOS_PATH_SIZE + 4], linux_dir[LINUX_PATH_SIZE];
          const char *scan_dir;
          DIR *dd = NULL;
          struct dirent *de;
          int any = 0;
          if (pat_dir_size >= sizeof(dos_dir)) pat_dir_size = sizeof(dos_dir) - 1;
          memcpy(dos_dir, bctx->arg, pat_dir_size);
          dos_dir[pat_dir_size] = '\0';
          if (dos_dir[0] == '\0') strcpy(dos_dir, ".");
          scan_dir = linux_dir;
          if (*get_linux_filename_r(dos_dir, bctx->dir_state, linux_dir, NULL) == '\0' || (dd = opendir_with_case_fallback(linux_dir, fnbuf2, sizeof(fnbuf2))) == NULL) {
            fprintf(stderr, "File not found - %s\r\n", bctx->arg);
            bctx->exit_code = 1;
          } else {
            if (fnbuf2[0] != '\0') scan_dir = fnbuf2;
            bctx->exit_code = 0;
            while ((de = readdir(dd)) != NULL) {
              char full[LINUX_PATH_SIZE];
              struct stat st;
              int fd;
              size_t dlen;
              char fbuf[4096], *ep;
              if (de->d_name[0] == '.') continue;
              if (!dos_wildcard_match(pat_base, de->d_name)) continue;
              dlen = strlen(scan_dir);
              if (dlen + 1 + strlen(de->d_name) + 1 >= sizeof(full)) continue;
              memcpy(full, scan_dir, dlen);
              if (dlen && full[dlen - 1] != '/') full[dlen++] = '/';
              strcpy(full + dlen, de->d_name);
              if (stat_with_case_fallback(full, &st, 0) != 0 || !S_ISREG(st.st_mode)) continue;
              fd = open_with_case_fallback(full, O_RDONLY, 0666);
              if (fd < 0) { bctx->exit_code = 1; continue; }
              any = 1;
              while ((bctx->got = read(fd, fbuf, sizeof(fbuf))) > 0) {
                if ((ep = memchr(fbuf, '\x1a', bctx->got)) != NULL) { (void)!write(1, fbuf, ep - fbuf); break; }
                (void)!write(1, fbuf, bctx->got);
              }
              if (bctx->got < 0) bctx->exit_code = 1;
              close(fd);
            }
            closedir(dd);
            if (!any) {
              fprintf(stderr, "File not found - %s\r\n", bctx->arg);
              bctx->exit_code = 1;
            }
          }
        } else {  /* Now filename is in arg. */
          int fd = open_dos_file(bctx->arg, bctx->dos_prog_abs, O_RDONLY, bctx->dir_state);
          if (fd < 0) {
            if (errno == ENOENT) {
              fprintf(stderr, "File not found - %s\r\n", bctx->arg);  /* MS-DOS 6.22. */
            } else {
              fprintf(stderr, "Error opening (%s) - %s\r\n", strerror(errno), bctx->arg);
            }
            bctx->exit_code = 1;
          } else {
            char fbuf[4096], *ep;
            fflush(stdout);
            while ((bctx->got = read(fd, fbuf, sizeof(fbuf))) > 0) {
              if ((ep = memchr(fbuf, '\x1a', bctx->got)) != NULL) {  /* Stop at Ctrl-<Z>. DOSBox ignores it. */
                (void)!write(1, fbuf, ep - fbuf);  /* STDOUT_FILENO. */
                break;
              }
              (void)!write(1, fbuf, bctx->got);  /* STDOUT_FILENO. */
            }
            if ((bctx->exit_code = (bctx->got < 0)) != 0) {
              fprintf(stderr, "\r\nError reading (%s) - %s\r\n", strerror(errno), bctx->arg);
            }
            close(fd);
          }
        }
      } else if (memcmp(bctx->p_line, "dir", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "chdir", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "attrib", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "call", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "cd", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "choice", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "help", bctx->cmd_size) == 0 ||
                 0 ||
                 memcmp(bctx->p_line, "loadhigh", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "lh", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "rmdir", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "rd", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "rem", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "rename", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "ren", bctx->cmd_size) == 0 ||
                 memcmp(bctx->p_line, "subst", bctx->cmd_size) == 0) {
        *bctx->r = '\0';
        if (bctx->emu_params->strict_mode) {
          fprintf(stderr, "fatal: DOS command not supported: %s\n", bctx->p_line);
          exit(252);
        } else {
          fprintf(stderr, "Unsupported DOS command: %s\r\n", bctx->p_line);
          bctx->exit_code = 1;
        }
        /**r = c;*/
      }
      else { return DC_PASS; }
  return DC_DONE;
}

