#include "kvikdos.h"

enum mz_subformat_t detect_mz_subformat(const char *path) {
  int fd;
  unsigned char mz[64];
  unsigned char sig4[4];
  unsigned char sig2[2];
  unsigned long off;
  ssize_t got;
  if (!path) return MZ_SUBFMT_NONE;
  fd = open(path, O_RDONLY);
  if (fd < 0) return MZ_SUBFMT_NONE;
  got = read(fd, mz, sizeof(mz));
  if (got < 0 || got < 0x40 || mz[0] != 'M' || mz[1] != 'Z') { close(fd); return MZ_SUBFMT_NONE; }
  off = (unsigned long)mz[0x3c] | ((unsigned long)mz[0x3d] << 8) | ((unsigned long)mz[0x3e] << 16) | ((unsigned long)mz[0x3f] << 24);
  if ((long)off < 0 || lseek(fd, (off_t)off, SEEK_SET) < 0) { close(fd); return MZ_SUBFMT_NONE; }
  got = read(fd, sig4, sizeof(sig4));
  if (got == 4 && sig4[0] == 'P' && sig4[1] == 'E' && sig4[2] == 0 && sig4[3] == 0) { close(fd); return MZ_SUBFMT_PE; }
  if (lseek(fd, (off_t)off, SEEK_SET) < 0) { close(fd); return MZ_SUBFMT_NONE; }
  got = read(fd, sig2, sizeof(sig2));
  close(fd);
  if (got == 2 && sig2[0] == 'N' && sig2[1] == 'E') return MZ_SUBFMT_NE;
  if (got == 2 && sig2[0] == 'L' && sig2[1] == 'E') return MZ_SUBFMT_LE;
  if (got == 2 && sig2[0] == 'L' && sig2[1] == 'X') return MZ_SUBFMT_LX;
  return MZ_SUBFMT_NONE;
}

int file_contains_text(const char *path, const char *needle) {
  int fd;
  struct stat st;
  char *buf;
  size_t size, nlen;
  ssize_t got;
  int found = 0;
  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); return 0; }
  size = st.st_size > (1 << 20) ? (1 << 20) : (size_t)st.st_size;  /* Scan up to 1 MiB. */
  nlen = strlen(needle);
  if (nlen == 0 || nlen > size) { close(fd); return 0; }
  buf = (char*)malloc(size);
  if (!buf) { close(fd); return 0; }
  got = read(fd, buf, size);
  close(fd);
  if (got > 0 && (size_t)got >= nlen && memmem(buf, (size_t)got, needle, nlen) != NULL) found = 1;
  free(buf);
  return found;
}

int is_probable_borland_dual_mode_ne(const char *path) {
  return file_contains_text(path, "DPMI error (") &&
         (file_contains_text(path, "TLINK") || file_contains_text(path, "RTM"));
}

int is_probable_windows_message_stub(const char *path) {
  int fd;
  unsigned char h[64];
  unsigned char *buf = NULL;
  size_t stub_size, max_scan;
  ssize_t got;
  int is_stub = 0;
  unsigned long e_lfanew, e_cparhdr;

  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  got = read(fd, h, sizeof(h));
  if (got < (ssize_t)sizeof(h) || h[0] != 'M' || h[1] != 'Z') goto done;

  e_lfanew = (unsigned long)h[0x3c] | ((unsigned long)h[0x3d] << 8) |
             ((unsigned long)h[0x3e] << 16) | ((unsigned long)h[0x3f] << 24);
  e_cparhdr = (unsigned long)h[0x08] | ((unsigned long)h[0x09] << 8);
  if (e_lfanew < 0x40) goto done;
  if (e_lfanew <= (e_cparhdr << 4)) goto done;
  stub_size = (size_t)(e_lfanew - (e_cparhdr << 4));
  if (stub_size == 0) goto done;

  /* Most message stubs are tiny; anything larger is likely meaningful DOS. */
  if (stub_size > 1024) goto done;

  max_scan = stub_size > 4096 ? 4096 : stub_size;
  buf = (unsigned char*)malloc(max_scan);
  if (!buf) goto done;
  if (lseek(fd, (off_t)(e_cparhdr << 4), SEEK_SET) < 0) goto done;
  got = read(fd, buf, max_scan);
  if (got <= 0) goto done;

  if (memmem(buf, (size_t)got, "This program", 12) ||
      memmem(buf, (size_t)got, "requires Microsoft", 18) ||
      memmem(buf, (size_t)got, "cannot be run in DOS mode", 25)) {
    is_stub = 1;
  }

 done:
  if (buf) free(buf);
  close(fd);
  return is_stub;
}

int is_probable_dos_extender_program(const char *path) {
  /* DOS-extender signatures: a bound MZ/NE/LE/PE file containing any of
   * these is meant to run under DOS with a protected-mode extender, not
   * under Windows/Wine.
   */
  static const char * const sigs[] = {
    "DOSX16", "DOSX32", "dosxnt", "DOSXNT", "MS32KRNL",
    "Phar Lap", "PHARLAP", "TNT", "TNTDOS", "RUN286",
    "DOS4GW", "DOS/4GW", "DOS4G", "4GWPRO",
    "DOS/32", "DOS32A", "DOS/16M", "DOS16M", "D16M",
    "PMODE", "PMODETSR", "PMODE/W",
    "CauseWay", "CAUSEWAY", "WDOSX", "PROVM", "X32VM", "ZPM",
    "go32", "GO32", "CWSDPMI", "HDPMI",
    "DOS-Extender", "DOSEXTENDER", "DOS Extender", "__DOSEXT16_MODE",
    "DPMI, VCPI", "DPMI16BI", "32RTM", "RTM.EXE", "POWERPACK",
    "DPMI host", "DPMI loader", "requires DPMI", "386|DOS",
    "protected-mode application",
  };
  int fd;
  unsigned char *buf = NULL;
  ssize_t got;
  unsigned i;
  int is_ext = 0;
  const size_t scan_size = 1U << 20;  /* Scan first 1 MiB. */
  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  buf = (unsigned char*)malloc(scan_size);
  if (!buf) goto done;
  got = read(fd, buf, scan_size);
  if (got <= 0) goto done;
  for (i = 0; !is_ext && i < sizeof(sigs) / sizeof(sigs[0]); ++i) {
    if (memmem(buf, (size_t)got, sigs[i], strlen(sigs[i]))) is_ext = 1;
  }
 done:
  if (buf) free(buf);
  close(fd);
  return is_ext;
}
