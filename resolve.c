#include "kvikdos.h"
int resolve_case_fallback_path(const char *in_path, char *out_path, size_t out_size, int keep_last_case) {
  const int is_abs = in_path[0] == '/';
  const char *p = in_path + is_abs;
  char comp[256];
  if (!in_path[0] || out_size < 2) return 0;
  out_path[0] = is_abs ? '/' : '\0';
  out_path[is_abs ? 1 : 0] = '\0';
  while (*p) {
    const char *q, *r;
    size_t clen, olen;
    int is_last;
    DIR *dd;
    struct dirent *de;
    char picked[256];
    int found = 0;
    const char *scan_dir;
    while (*p == '/') ++p;
    if (!*p) break;
    q = p;
    while (*q && *q != '/') ++q;
    clen = (size_t)(q - p);
    if (clen == 0 || clen >= sizeof(comp)) return 0;
    memcpy(comp, p, clen);
    comp[clen] = '\0';
    r = q;
    while (*r == '/') ++r;
    is_last = *r == '\0';
    if (!(keep_last_case && is_last)) {
      scan_dir = out_path[0] ? out_path : ".";
      dd = opendir(scan_dir);
      if (!dd) return 0;
      while ((de = readdir(dd)) != NULL) {
        size_t dlen = strlen(de->d_name);
        if (dlen == clen && is_same_ascii_nocase(de->d_name, comp, (unsigned)clen)) {
          memcpy(picked, de->d_name, dlen + 1);
          found = 1;
          break;
        }
      }
      closedir(dd);
      if (!found) return 0;
    } else {
      memcpy(picked, comp, clen + 1);
    }
    olen = strlen(out_path);
    if (olen && out_path[olen - 1] != '/') {
      if (olen + 1 >= out_size) return 0;
      out_path[olen++] = '/';
      out_path[olen] = '\0';
    }
    if (olen + strlen(picked) >= out_size) return 0;
    strcpy(out_path + olen, picked);
    p = q;
  }
  return out_path[0] != '\0';
}

int open_with_case_fallback(const char *linux_filename, int flags, mode_t mode) {
  char resolved[1024];
  int fd = open(linux_filename, flags, mode);
  if (fd >= 0) return fd;
  if (errno == ENOENT && g_case_fallback_mode == 2 && linux_filename[0] &&
      resolve_case_fallback_path(linux_filename, resolved, sizeof(resolved), (flags & O_CREAT) != 0)) {
    fd = open(resolved, flags, mode);
  }
  return fd;
}

int stat_with_case_fallback(const char *linux_filename, struct stat *st, int keep_last_case) {
  char resolved[1024];
  if (stat(linux_filename, st) == 0) return 0;
  if (errno == ENOENT && g_case_fallback_mode == 2 && linux_filename[0] &&
      resolve_case_fallback_path(linux_filename, resolved, sizeof(resolved), keep_last_case)) {
    return stat(resolved, st);
  }
  return -1;
}

DIR *opendir_with_case_fallback(const char *linux_dir, char *resolved_out, size_t resolved_out_size) {
  DIR *dd;
  if (resolved_out_size) resolved_out[0] = '\0';
  dd = opendir(linux_dir);
  if (dd) {
    if (resolved_out_size) {
      strncpy(resolved_out, linux_dir, resolved_out_size - 1);
      resolved_out[resolved_out_size - 1] = '\0';
    }
    return dd;
  }
  if (errno != ENOENT || g_case_fallback_mode != 2 || !linux_dir[0]) return NULL;
  if (!resolve_case_fallback_path(linux_dir, resolved_out, resolved_out_size, 0)) return NULL;
  return opendir(resolved_out);
}

const char *getenv_prefix(const char *name_prefix, const char **env, const char **env_end) {
  const size_t name_prefix_size = strlen(name_prefix);
  for (; env != env_end; ++env) {
    if (strncmp(*env, name_prefix, name_prefix_size) == 0) return *env + name_prefix_size;
  }
  return NULL;
}

const char *getenv_prefix_nocase0(const char *name_prefix, const char *const *env) {
  size_t i, nps;
  if (!env) return NULL;
  nps = strlen(name_prefix);
  for (; *env; ++env) {
    const char *s = *env;
    if (!s) continue;
    for (i = 0; i < nps; ++i) {
      char a = s[i], b = name_prefix[i];
      if (a == '\0' || ((a | 32) != (b | 32))) break;
    }
    if (i == nps) return s + nps;
  }
  return NULL;
}

const char *getenv_prefix_block_nocase(const char *name_prefix, const char *env, const char *env_end) {
  size_t i, nps;
  if (!env || !env_end || env >= env_end) return NULL;
  nps = strlen(name_prefix);
  while (env < env_end && *env) {
    const char *eq = strchr(env, '=');
    const char *next = env + strlen(env) + 1;
    if (!eq || eq >= env_end) break;
    if ((size_t)(eq - env + 1) == nps) {
      for (i = 0; i < nps; ++i) {
        char a = env[i], b = name_prefix[i];
        if ((a | 32) != (b | 32)) break;
      }
      if (i == nps) return eq + 1;
    }
    env = next;
  }
  return NULL;
}

char *add_env(char *env, char *env_end, const char *var, char do_check) {
  if (do_check && *var == '=') {
    fprintf(stderr, "fatal: DOS environment variable has empty name\n");
    exit(252);
  }
  for (;;) {
    const char c = *var++;
    if (env == env_end) {
      fprintf(stderr, "fatal: DOS environment too long\n");
      exit(252);
    }
    if (c == '=') do_check = 0;  /*in_name = 0;*/
    *env++ = ((unsigned)c - 'a' + 0U <= 'z' - 'a' + 0U && do_check) ? c & ~32 : c;  /* Convert name to uppercase. */
    if (c == '\0') break;
  }
  if (do_check) {
    fprintf(stderr, "fatal: DOS environment variable has missing value\n");
    exit(252);
  }
  return env;
}

const char *getenv_dos_prefix(const char *name_prefix, const char *env) {
  const size_t name_prefix_size = strlen(name_prefix);
  while (*env != '\0') {
    const size_t var_size1 = strlen(env) + 1;
    if (strncmp(env, name_prefix, name_prefix_size) == 0) return env + name_prefix_size;
    env += var_size1;
  }
  return NULL;
}

char set_int(unsigned char int_num, unsigned value_seg_ofs, void *mem, char had_get_ints, unsigned char *tasm30_bitset) {
  unsigned * const p = (unsigned*)mem + int_num;
  if (DEBUG || DEBUG_INTVEC || DIAG_ON(DIAG_BIT_INT)) {
    fprintf(g_diag_file, "debug: set interrupt vector int:%02x to cs:%04x ip:%04x\n",
            int_num, (unsigned short)(value_seg_ofs >> 16), (unsigned short)value_seg_ofs);
  }
  /* !!! TODO(pts): Make the default permissive in general, and enable these protections only on a flag. */
  if (int_num == 0x23) *tasm30_bitset |= 4;
  if (int_num == 0x18) *tasm30_bitset |= 8;
  if ((unsigned)int_num - 0x22 + 0U <= 0x24 - 022 +0U ||  /* Application Ctrl-<Break> handler == 0x23. We allow 0x22..0x24. */
      value_seg_ofs == *p ||  /* Unchanged. */
      value_seg_ofs == MAGIC_INT_VALUE(int_num) ||  /* Set back to original. */
      ((had_get_ints & 2) && int_num == 0x18) ||  /* TASM 3.2. */
      ((had_get_ints & 2) && int_num == 0x00) ||  /* TASM 2.0 and 2.01, after set_int 0x23, set_int 0x18. */
      ((had_get_ints & 2) && (int_num == 0x1b || int_num == 0x3f)) ||  /* Borland Turbo C++ 1.01 compiler tcc.exe (no `& 8'), Borland C++ 2.0 complier bcc.exe (has also `& 8') */
      ((had_get_ints & 4) && int_num == 0x06) ||  /* TLINK 4.0. */
      ((had_get_ints & 1) && (int_num == 0x00 || int_num == 0x24 || int_num == 0x3f))  /* Turbo Pascal 7.0. */ ||
      ((had_get_ints & 1) && (int_num == 0x04 || int_num == 0x05))  /* Watcom 10.0a bpatch.exe. */ ||
      ((had_get_ints & 1) && int_num == 0x75)  /* Microsoft QuickBASIC 4.50 compiler qbc.exe. */ ||
      ((had_get_ints & 1) && (int_num == 0x00 || int_num == 0x02 || int_num - 0x35 + 0U <= 0x3f - 0x35 + 0U))  /* Microsoft BASIC Professional Development System 7.10 compiler pbc.exe. */ ||
      ((had_get_ints & 8) && int_num - 0x34 + 0U <= 0x3d - 0x34 + 0U)  /* Microsoft Macro Assembler 1.10 masm.exe */ ||
      ((had_get_ints & 0x10) && (int_num == 0x02 || int_num == 0x1b || int_num == 0x00)) ||  /* JWasm 2.11a jwasmr.exe */
      int_num >= 0xc0 ||  /* Toolchain-private vectors (e.g. Intel iC-86 v4.5 IC86.EXE). */
      int_num == 0x06 ||  /* ASM32 1.1 assembler asm32.exe */
      0) {
    /* FYI kvikdos never sends Ctrl-<Break>. */
  } else {
    if (DEBUG || DEBUG_INTVEC || DIAG_ON(DIAG_BIT_INT)) {
      fprintf(g_diag_file, "debug: permissive set interrupt vector int:%02x to cs:%04x ip:%04x\n",
              int_num, (unsigned short)(value_seg_ofs >> 16), (unsigned short)value_seg_ofs);
    }
  }
  *p = value_seg_ofs;
  return 0;  /* Success. */
}