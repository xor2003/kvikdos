#include "kvikdos.h"

char get_case_mode_from_last_component(const char *p) {
  const char *q = p + strlen(p);
  for (; q != p && q[-1] != '/'; --q) {}
  for (; *q != '\0' && *q - 'a' + 0U > 'z' - 'a' + 0U; ++q) {}
  return (*q == '\0') ? CASE_MODE_UPPERCASE : CASE_MODE_LOWERCASE;
}

char detect_prog_filename_type(const char *prog_filename) {
  struct stat st;
  if ((prog_filename[0] & ~32) - 'A' + 0U <= 'Z' - 'A' + 0U && prog_filename[1] == ':') return PFT_DOS;
  if (strchr(prog_filename, '/')) return PFT_LINUX;
  if (strchr(prog_filename, '\\')) return PFT_DOS;
  if (strchr(prog_filename, '.') && stat(prog_filename, &st) == 0 && S_ISREG(st.st_mode)) return PFT_LINUX;
  return PFT_PATH;
}

void case_fold_on_drive(char *p, char drive, const DirState *dir_state) {
  const char drive_idx = (drive & ~32) - 'A';
  if ((unsigned char)drive_idx >= DRIVE_COUNT || !dir_state->linux_mount_dir[(int)drive_idx]) {  /* Bad drive. */
    *p = '\0'; return;
  } else {
    char const case_flip = dir_state->case_mode[(int)drive_idx] == CASE_MODE_LOWERCASE ? 32 : 0;
    char c;
    while ((c = *p) != '\0') {
      const char c_uc = c & ~32;
      *p++ = !(c_uc - 'A' + 0U <= 'Z' - 'A' + 0U) ? c : c_uc ^ case_flip;
    }
  }
}

const char *get_dos_abs_filename_r(const char *p, char drive, const DirState *dir_state, char *out_buf) {
  const char *p0 = skip_dot_slash(p);
  p = p0;
  if (drive == '\0') {  /* Find the best drive, i.e. the drive with the longest matching linux_mount_dir. */
    char best_drive = '\0';
    size_t best_mp_size = 0;
    for (drive = 'A'; drive < 'A' + DRIVE_COUNT; ++drive) {
      const char *mp = dir_state->linux_mount_dir[drive - 'A'];
      if (mp) {
        const size_t mp_size = strlen(mp);
        if ((p0[0] != '/' || mp[0] == '/') && strncmp(p0, mp, mp_size) == 0 && mp_size + 1 > best_mp_size) {  /* Upon equality, use the earlier drive. */
          best_drive = drive;
          best_mp_size = mp_size + 1;
        }
      }
    }
    if (best_drive == '\0') goto error;  /* No mounted drives. */
    drive = best_drive;
  } else {  /* Use the specified drive. */
    const char *mp = dir_state->linux_mount_dir[drive - 'A'];
    if (mp) {
      const size_t mp_size = strlen(mp);
      if (!((p0[0] != '/' || mp[0] == '/') && strncmp(p0, mp, mp_size) == 0)) goto error;  /* File not on the mount point of drive. */
    } else {
      goto error;  /* No such drive. */
    }
  }
  {
    const char *mp = dir_state->linux_mount_dir[drive - 'A'];
    const size_t mp_size = strlen(mp);
    if (0 == strncmp(p0, mp, mp_size)) {
      char *r = out_buf;
      char *rend = out_buf + DOS_PATH_SIZE - 1;
      *r++ = drive;
      *r++ = ':';
      *r++ = '\\';
      for (p = p0 + mp_size; *p != '\0';) {
        const char c = *p++;
        if (r == rend) goto error;  /* DOS pathname too long. */
        if (c == '/') {
          for (; *p == '/'; ++p) {}  /* Skip subsequent slashes. */
        }
        /* TODO(pts): Check that each pathname component is at most 8.3 bytes long. */
        *r++ = (c == '/') ? '\\'  /* Convert '/' to '\\'. */
             : (c - 'a' + 0U <= 'z' - 'a' + 0U) ? c & ~32 : c;  /* Convert to uppercase. */
      }
      *r = '\0';
      return out_buf;
    }
  }
 error:
  *out_buf = '\0';  /* Mount point not found. */
  return out_buf;
}

char *get_linux_filename_r(const char *p, const DirState *dir_state, char *out_buf, char **out_lastc_out) {
  char *out_p = out_buf, *out_pend, *out_lastc = out_buf;
  const char *in_linux;
  const char *linux_prog_base, *slashp;
  const char *in_dos[2] = { "", "" };
  char drive_idx, case_flip = 0, case_mode;
  if (*p == '\0') goto done;  /* Empty pathname is an error. */
  if (!dir_state) {  /* Convert to relative Linux pathname. */
    in_linux = NULL;
    in_dos[1] = p;
  } else if (dir_state->linux_prog && dir_state->dos_prog_abs && strcmp(p, dir_state->dos_prog_abs) == 0 &&
             (linux_prog_base = ((slashp = strrchr(dir_state->linux_prog, '/')) != NULL ? slashp + 1 : dir_state->linux_prog)) != NULL &&
             strchr(linux_prog_base, '.') != NULL) {
    in_linux = dir_state->linux_prog;
  } else {
    if (p[0] != '\0' && p[1] == ':') {
      drive_idx = (p[0] & ~32) - 'A';
      p += 2;
    } else {
      drive_idx = dir_state->drive - 'A';
    }
    if ((unsigned char)drive_idx >= DRIVE_COUNT) {  /* Bad or unknown drive letter. */  /* !! Report error 0x3 (Path not found) */
      /*fprintf(stderr, "fatal: DOS filename on wrong drive: 0x%02x\n", (unsigned char)p[0]);*/  /* !! Report error 0x3 (Path not found) */
      /*exit(252);*/
      goto done;
    }
    in_linux = dir_state->linux_mount_dir[(int)drive_idx];
    if (!in_linux) goto done;  /* Drive not available. !! Report error 0x3 (Path not found) */
    if ((case_mode = dir_state->case_mode[(int)drive_idx]) == CASE_MODE_UNSPECIFIED) {
      /* This signifies a genuine bug in kvikdos.c. !! Typically still happens in find_prog(). */
      fprintf(stderr, "assert: case mode not yet specified for drive %c:\n", 'A' + drive_idx);
      exit(252);
    }
    case_flip = case_mode == CASE_MODE_LOWERCASE ? 32 : 0;
    if (*p == '\\' || *p == '/') {
      for (++p; *p == '\\' || *p == '/'; ++p) {}
    } else {
      in_dos[0] = dir_state->current_dir[(int)drive_idx];
    }
    in_dos[1] = p;
  }
  out_pend = out_p + LINUX_PATH_SIZE - 1;
  if (0) { too_long:  /* Pathname too long. !! Handle this error. !! Report error 0x3 (Path not found) or 0x44 (Network name limit exceeded). */
   error:
    out_p = out_buf;
    goto done;
  }
  if (in_linux) {
    const size_t size = strlen(in_linux);
    if (size > (size_t)(out_pend - out_p)) goto too_long;
    memcpy(out_p, in_linux, size);
    out_p += size;
  }
  {  /* Convert pathnames in in_dos */
    const char * const *in_dosi;
    unsigned component_count = 0;
    for (in_dosi = in_dos; in_dosi != in_dos + 2; ++in_dosi) {
      p = *in_dosi;
      for (; *p != '\0';) {
        unsigned dot_count = 0;
        char *out_limit83 = out_p + 8;
        out_lastc = out_p;
        if (CMD_PARSE_DEBUG && !(p[0] != '\0' && p[0] != '/' && p[0] != '\\')) {
          fprintf(stderr, "assert: pathname component empty or starts with / or \\: %s\n", p);
          exit(252);
        }
        if (p[0] == '.' && (p[1] == '\0' || p[1] == '\\' || p[1] == '/' || (p[1] == '.' && (p[2] == '\0' || p[2] == '\\' || p[2] == '/')))) {
          if (p[1] == '.') {
            /* Security: Too many levels up, outside linux_mount_dir with `..'. It's still possible to escape up if one of the pathname components is a symlink. */
            if (component_count-- == 0) goto error;
          }
          dot_count = LINUX_PATH_SIZE + 2;
        } else {
          ++component_count;
          if (*p == '.') goto error;  /* First character in component is '.'. */
        }
        for (; *p != '\0';) {
          const char c = *p;
          if (out_p == out_pend) goto too_long;
          if (c == '\\' || c == '/') {
            *out_p++ = '/';  /* Convert '\\' to '/'. */
            break;
          } else if (c == '.') {
            if (dot_count == 0) out_limit83 = out_p + (1 + 3);
            ++dot_count;
          } else if (c + 0U <= ' ' + 0U || c =='"' || c == '*' || c == '?' || c == ':' || c == '[' || c == ']' || c == '=' || c == '|' || c == '<' || c == '>' || c == ',' || c == ';' || c == '\x7f') {
            goto error; /* Character not allowed in DOS pathname. DOSBox allows '+'. */
          }
          ++p;
          if (out_p != out_limit83) {  /* Truncate the basename to 8 characters, and the extension to 3 characters. DOSBox does the same. */
            const char c_uc = c & ~32;
            *out_p++ = !(c_uc - 'A' + 0U <= 'Z' - 'A' + 0U) ? c : c_uc ^ case_flip;
          }
          /* TODO(pts) Truncate each component to 8.3 characters? */
        }
        if (dot_count <= LINUX_PATH_SIZE) {
          if (dot_count > 1) goto error;  /* More than 1 '.' in component. DOSBox doesn't allow it either. */
          if (p[-1] == '.') goto error;  /* Last character in component is '.'. DOSBox doesn't allow it either. It's safe to check here because of the assertion above. */
        }
        for (; *p == '\\' || *p == '/'; ++p) {}
      }
    }
    if (p > in_dos[1] && (p[-1] == '/' || p[-1] == '\\')) goto error;  /* If pathname ends with a slash, that's an error. */
  }
 done:
  *out_p = '\0';
  if (is_same_ascii_nocase(out_lastc, "nul", 3) && (out_lastc[3] == '.' || out_lastc[3] == '\0')) strcpy(out_buf, "/dev/null");
  if (out_lastc_out) *out_lastc_out = out_lastc;
  return out_buf;
}

char fnbuf[LINUX_PATH_SIZE], fnbuf2[LINUX_PATH_SIZE], argv0_fnbuf[LINUX_PATH_SIZE];

static const char * const find_prog_on_path_exts[] = { ".com", ".exe", ".bat", /* ".cmd", for Windows NT+. */ NULL };

static const char * const find_prog_on_path_no_exts[] = { "", NULL };

char *find_prog_on_path(const char *prog_filename, const DirState *dir_state, const char *dos_path, char *drive_out) {
  size_t size;
  const char *p, *pp, *pq;
  char *r;
  char c;
  char drive = dir_state->drive;
  const char * const * exts0;
  for (p = prog_filename; (c = *p) != '\0' && c != ':' && c != '/' && c != '\\'; ++p) {}
  if (*p != '\0') {
    /* Call detect_prog_filename_type first, and if it returns PFT_PATH, only then call find_prog_on_path. */
    fprintf(stderr, "assert: prog_filename contains disallowed characters: %s\n", prog_filename);
    exit(252);
  }
  get_linux_filename_r(prog_filename, NULL /* dir_state */, fnbuf2, NULL);
  if ((fnbuf2[0] == '.' && fnbuf2[1] == '\0') ||  /* "." is an invalid program name. */
      fnbuf2[0] == '\0') { too_long:
    fnbuf[0] = '\0'; return fnbuf;  /* Invalid program name. */
  }
  size = strlen(prog_filename);
  for (p = prog_filename + size; p != prog_filename && *--p != '.';) {}
  /* DOS allows only exts in find_prog_on_path_exts, we allow anything the user specifies here. */
  exts0 = (*p != '.') ? find_prog_on_path_exts : find_prog_on_path_no_exts;
  if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: DOS path lookup prog_filename=%s p=%s dos_path=%s\n", prog_filename, p, dos_path);
  pp = pq = NULL;
  for (;;) {  /* Find in current directory first, then continue finding on DOS %PATH%. */
    const char * const * exts;
    r = fnbuf;
    if (pp) {
      char c, *pt, ptc;
      for (pq = pp; (c = *pp) != ';' && c != '\0'; ++pp) {}
      drive = pq[0] & ~32;
      if (drive - 'A' + 0U <= 'Z' - 'A' + 0U && pq[1] == ':') {
        if (pq[2] != '\\' && pq[2] != '/') goto end_of_pp;  /* Not an absolute pathname within a drive. */
      } else {
        drive = dir_state->drive;
      }
      *(char*)pp = '\0';  /* Temporary terminator within dos_path, for get_linux_filename_r. */
      for (pt = (char*)pp; pt != pq && ((ptc = pt[-1]) == '\\' || ptc == '/'); --pt) {}
      if (pt != pq) { ptc = *pt; *pt = '\0'; }  /* Temporarily remove trailing backslashes. */
      get_linux_filename_r(pq, dir_state, fnbuf, NULL);
      if (pt != pq) *pt = ptc;  /* Restore trailing backlashes, if any. */
      *(char*)pp = c;  /* Restore the terminator. */
      if (*fnbuf == '\0') goto end_of_pp;  /* Skip if filename is invalid. */
      r = fnbuf + strlen(fnbuf);
      if (fnbuf[0] != '\0' && r[-1] != '/') {  /* fnbuf ends with a slash if %PATH% component is just a drive letter, e.g. C: */
        if ((unsigned)(r - fnbuf)  >= sizeof(fnbuf)) goto too_long;
        *r++ = '/';
      }
    }
    if ((unsigned)(r - fnbuf) + size >= sizeof(fnbuf)) goto too_long;
    memcpy(r, prog_filename, size + 1);  /* Including the trailing '\0'. */
    case_fold_on_drive(r, drive, dir_state);
    if (*r == '\0') goto end_of_pp;  /* Invalid pathname or invalid drive. */
    r += size;
    *r = '\0';
    if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: trying progs: %s\n", fnbuf);
    for (exts = exts0; *exts; ++exts) {
      const size_t ext_size = strlen(*exts);
      struct stat st;
      if ((unsigned)(r - fnbuf) + ext_size >= sizeof(fnbuf)) goto too_long;
      memcpy(r, *exts, ext_size + 1);  /* Including the trailing '\0'. */
      case_fold_on_drive(r, drive, dir_state);
      if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: trying prog: %s\n", fnbuf);
      if (stat(fnbuf, &st) == 0 && S_ISREG(st.st_mode)) {
        if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: found prog on drive=%c: %s\n", drive, fnbuf);
        *drive_out = drive;
        return fnbuf;  /* Found executable program file. */
      } else if (g_case_fallback_mode != 0) {
        char *s, *base = fnbuf;
        strcpy(fnbuf2, fnbuf);
        for (s = fnbuf2; *s; ++s) if (*s == '/') base = s + 1;
        for (s = base; *s; ++s) if ((unsigned char)(*s - 'a') <= 'z' - 'a') *s &= ~32;
        if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: trying prog case fallback upper: %s\n", fnbuf2);
        if (stat(fnbuf2, &st) == 0 && S_ISREG(st.st_mode)) {
          strcpy(fnbuf, fnbuf2);
          *drive_out = drive;
          return fnbuf;
        }
        strcpy(fnbuf2, fnbuf);
        for (s = base = fnbuf2; *s; ++s) if (*s == '/') base = s + 1;
        for (s = base; *s; ++s) if ((unsigned char)(*s - 'A') <= 'Z' - 'A') *s |= 32;
        if (CMD_PARSE_DEBUG) fprintf(stderr, "debug: trying prog case fallback lower: %s\n", fnbuf2);
        if (stat(fnbuf2, &st) == 0 && S_ISREG(st.st_mode)) {
          strcpy(fnbuf, fnbuf2);
          *drive_out = drive;
          return fnbuf;
        }
      }
    }
   end_of_pp:
    if (pp) {
    } else if (dos_path) {
      pp = dos_path;
    } else {
      break;
    }
    for (; *pp == ';'; ++pp) {}  /* Skip over %PATH% separator characters ';'. */
    if (*pp == '\0') break;
  }
  return NULL;  /* Not found on %PA%H. */
}

char dosfnbuf[DOS_PATH_SIZE];

void dos_normalize_abspath(const char *p, const DirState *dir_state, char *out, unsigned out_size) {
  char abs_path[DOS_PATH_SIZE + 4];
  char *segs[DOS_PATH_SIZE / 2] = {0};
  unsigned nseg = 0, i;
  char *r, *e, *w;
  get_dos_abspath_r(p, dir_state, abs_path, sizeof(abs_path));
  if (abs_path[0] == '\0' || out_size < 4) { if (out_size) out[0] = '\0'; return; }
  out[0] = abs_path[0]; out[1] = ':'; out[2] = '\\'; out[3] = '\0';
  for (r = abs_path + 3; *r; ) {
    char next;
    e = r;
    while (*e && *e != '\\') ++e;
    next = *e;
    *e = '\0';
    if (r[0] == '\0' || (r[0] == '.' && r[1] == '\0')) {
      /* Skip empty and "." components. */
    } else if (r[0] == '.' && r[1] == '.' && r[2] == '\0') {
      if (nseg) --nseg;
    } else {
      if (nseg >= sizeof(segs) / sizeof(segs[0])) { out[0] = '\0'; return; }
      segs[nseg++] = r;
    }
    r = next ? e + 1 : e;
  }
  w = out + 3;
  for (i = 0; i < nseg; ++i) {
    const unsigned n = (unsigned)strlen(segs[i]);
    if ((unsigned)(w - out) + n + 1 >= out_size) { out[0] = '\0'; return; }
    memcpy(w, segs[i], n);
    w += n;
    *w++ = '\\';
  }
  if (w > out + 3) --w;  /* Drop trailing backslash. */
  *w = '\0';
}

void get_dos_abspath_r(const char *p, const DirState *dir_state, char *out_buf, unsigned out_size) {
  char *out_p = out_buf, *out_pend = out_buf + out_size;
  char drive_idx;
  const char *in_dos[2];
  if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: get_dos_abspath_r (%s)\n", p);
  if (*p == '\0' || out_size < 5) goto done;  /* Empty pathname is an error. */
  if (p[0] != '\0' && p[1] == ':') {
    drive_idx = (p[0] & ~32) - 'A';
    if ((unsigned char)drive_idx >= DRIVE_COUNT) {  /* Bad or unknown drive letter. */  /* !! Report error 0x3 (Path not found) */
      /*fprintf(stderr, "fatal: DOS filename on wrong drive: 0x%02x\n", (unsigned char)p[0]);*/  /* !! Report error 0x3 (Path not found) */
      /*exit(252);*/
      goto done;
    }
    p += 2;
  } else {
    drive_idx = dir_state->drive - 'A';
  }
  if (*p == '\\' || *p == '/') {
    for (++p; *p == '\\' || *p == '/'; ++p) {}
    in_dos[0] = "";
  } else {
    in_dos[0] = dir_state->current_dir[(int)drive_idx];
  }
  in_dos[1] = p;
  {  /* Convert pathnames in in_dos */
    const char * const *in_dosi;
    *out_p++ = drive_idx + 'A';
    *out_p++ = ':';
    *out_p++ = '\\';
    for (in_dosi = in_dos; in_dosi != in_dos + 2; ++in_dosi) {
      p = *in_dosi;
      for (; *p != '\0';) {
        const char c = *p++;
        if (out_p == out_pend) { out_p = out_buf; goto done; }
        if (c == '\\' || c == '/') {
          *out_p++ = '\\';  /* Convert '/' to '\\'. */
          for (++p; *p == '\\' || *p == '/'; ++p) {}
        } else {
          *out_p++ = (c - 'a' + 0U <= 'z' - 'a' + 0U) ? c & ~32 : c;  /* Convert to uppercase. */
          /* TODO(pts) Truncate each component to 8.3 characters? */
        }
      }
    }
  }
 done:
  *out_p = '\0';
  if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: get_dos_abspath_r=(%s)\n", out_buf);
}
