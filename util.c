#include "kvikdos.h"

char *xstrdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = (char*)malloc(n);
  if (!p) return NULL;
  memcpy(p, s, n);
  return p;
}

void copy_cstr0(char *dst, size_t dst_size, const char *src) {
  size_t n;
  if (dst_size == 0) return;
  n = strlen(src);
  if (n >= dst_size) n = dst_size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

const char *skip_dot_slash(const char *p) {
  while (p[0] == '.' && p[1] == '/') {  /* Skip ./ at the beginning. */
    for (p += 2; p[0] == '/'; ++p) {}
  }
  return p;
}

void remove_duplicate_slashes(char *p) {
  const char *q;
  char c;
  for (q = p; (c = *q) != '\0' && c != '/'; ++q) {}
  if (*q == '\0') return;  /* Avoid writing read-only memory for --kvm-check. */
  p += q - p;  /* Using `-' to prevent warning on const pointer. */
  while ((c = *q) != '\0') {
    *p++ = c;
    ++q;
    if (c == '/') {
      for (; *q == '/'; ++q) {}
    }
  }
  *p = '\0';
}

char is_same_ascii_nocase(const char *a, const char *b, unsigned size) {
  while (size-- != 0) {
    const unsigned char pa = *a++;
    const unsigned char pb = *b++;
    if (!(pa == pb || ((pa | 32) - 'a' + 0U <= 'z' - 'a' + 0U && (pa ^ 32) == pb))) return 0;
  }
  return 1;
}

void *my_memmem(const void *haystack, size_t haystacklen,
                       const void *needle, size_t needlelen) {
  /* `last' is the last position where a full needle can still start. The
   * first-byte memchr is bounded to it — passing the raw remainder would go
   * negative once a candidate sits past `last' (size_t wraparound = out of
   * bounds scan; musl SIGBUSes, glibc reads garbage). */
  const char *c = (const char*)haystack, *last;
  char first;
  if (needlelen == 0) return (void*)haystack;
  first = *(const char*)needle;
  if (haystacklen < needlelen) return NULL;
  if (needlelen == 1) return memchr(haystack, first, haystacklen);
  last = c + (haystacklen - needlelen);
  while ((c = (const char*)memchr(c, first, (size_t)(last - c + 1))) != NULL) {
    if (memcmp(c, needle, needlelen) == 0) return (void*)c;
    ++c;
  }
  return NULL;
}

const char *get_linux_ext(const char *p) {
  const char *ext0, *ext;
  if (!p) return "";
  ext0 = ext = p + strlen(p);
  for (; ext != p && ext[-1] != '/' && ext[-1] != '.'; --ext) {}
  return ext == p || ext[-1] == '/' ? ext0 : ext;
}

int ensure_fd_is_at_least(int fd, int min_fd) {
  if (fd >= 0 && fd + 0U < min_fd + 0U) {
    int fd2 = dup(fd), fd3;
    if (fd2 < 0) { perror("dup"); exit(252); }
    if (fd2 + 0U < min_fd + 0U) fd2 = ensure_fd_is_at_least(fd2, min_fd);
    if ((fd3 = open("/dev/null", O_RDWR)) >= 0) {
      if (fd3 == fd) {  /* Usually doesn't happen. */
        return fd2;  /* Keep /dev/null open as fd (see below). */
      } else if (dup2(fd3, fd) == fd) {
        close(fd3);
        return fd2;  /* Keep /dev/null open as fd (for which fd < min_fd), for faster operation subsequent many open() + close() calls with ensure_fd_is_at_least(). */
      } else {
        close(fd3);
      }
    }
    close(fd);
    fd = fd2;
  }
  return fd;
}

char get_dos_filename_drive(const char *p, const DirState *dir_state) {
  if (p[0] != '\0' && p[1] == ':') {
    const char drive_idx = (p[0] & ~32) - 'A';
    if ((unsigned char)drive_idx >= DRIVE_COUNT || !dir_state->linux_mount_dir[(int)drive_idx]) return '\0';  /* Bad drive. */
    return drive_idx + 'A';
  }
  return dir_state->drive;
}

void fcb_filename(const char *fcb, char dos_default_drive, char *out, unsigned out_size) {
  unsigned i;
  char *o = out, * const oend = out + (out_size ? out_size - 1 : 0);
  if (o == oend) { *o = '\0'; return; }
  *o++ = fcb[0] ? 'A' + fcb[0] - 1 : dos_default_drive;
  if (o == oend) { *o = '\0'; return; }
  *o++ = ':';
  for (i = 0; i < 8 && o < oend; ++i) {
    const char c = fcb[1 + i];
    if (c == ' ' || c == '\0') break;
    *o++ = c;
  }
  for (i = 0; i < 3 && o < oend; ++i) {
    const char c = fcb[9 + i];
    if (c == ' ' || c == '\0') break;
    if (o + 1 >= oend) break;
    if (i == 0) *o++ = '.';
    *o++ = c;
  }
  *o = '\0';
}

unsigned short dos_fat_date(const struct tm *tm) {
  return (unsigned short)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
}

unsigned short dos_fat_time(const struct tm *tm) {
  return (unsigned short)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec >> 1));
}

const char *get_dos_basename(const char *fn) {
  const char *fnp;
  if (fn[0] != '\0' && fn[1] == ':') fn += 2;
  /* Turbo C++ 1.01 compiler calls findfirst (int 0x21 ah == 0x4e (find
   * first), which calls get_dos_basename(...). We want it to succeed if the
   * specified fn contains '/' as directory separator, not only for '\\'.
   */
  for (fnp = fn + strlen(fn); fnp != fn && fnp[-1] != '\\' && fnp[-1] != '/'; --fnp) {}
  return fnp;
}

const char *get_linux_basename(const char *fn) {
  const char *fnp;
  for (fnp = fn + strlen(fn); fnp != fn && fnp[-1] != '/'; --fnp) {}
  return fnp;
}

char upper_ascii(char c) {
  return (c - 'a' + 0U <= 'z' - 'a' + 0U) ? c - 32 : c;
}

char has_dos_ext_nocase(const char *fn, const char *ext) {
  const char *dot = strrchr(fn, '.');
  size_t n;
  if (!dot) return 0;
  n = strlen(ext);
  if (strlen(dot) != n) return 0;
  return is_same_ascii_nocase(dot, ext, n);
}

time_t dos_datetime_to_time(unsigned short dos_date, unsigned short dos_time) {
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_sec = (dos_time & 31) << 1;
  tm.tm_min = (dos_time >> 5) & 63;
  tm.tm_hour = (dos_time >> 11) & 31;
  tm.tm_mday = dos_date & 31;
  tm.tm_mon = ((dos_date >> 5) & 15) - 1;
  tm.tm_year = ((dos_date >> 9) & 127) + 80;  /* Since 1900. */
  tm.tm_isdst = -1;
  if (tm.tm_mon < 0) tm.tm_mon = 0;
  if (tm.tm_mday < 1) tm.tm_mday = 1;
  return mktime(&tm);
}

char dos_wildcard_match(const char *pattern, const char *name) {
  while (*pattern == '*') ++pattern;
  if (*pattern == '\0') return 1;
  for (;; ++name) {
    const char pc = upper_ascii(*pattern);
    const char nc = upper_ascii(*name);
    if (pc == '*') {
      do ++pattern; while (*pattern == '*');
      if (*pattern == '\0') return 1;
      for (;; ++name) {
        if (*name == '\0') return 0;
        if (dos_wildcard_match(pattern, name)) return 1;
      }
    }
    if (pc == '\0') return nc == '\0';
    if (nc == '\0') return 0;
    if (pc != '?' && pc != nc) return 0;
    ++pattern;
  }
}

char is_dos_filename_83(const char *fn) {
  unsigned u;
  if (fn[0] != '\0' && fn[1] == ':') fn += 2;
  for (u = 0; u <= 8 && *fn != '.' && *fn != '\0'; ++fn, ++u) {}
  if (u > 8) return 0;
  if (*fn == '.') {
    for (u = 0, ++fn; u <= 3 && *fn != '\0'; ++fn, ++u) {}
    if (u > 3) return 0;
  }
  return 1;
}
