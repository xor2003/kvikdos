#include "kvikdos.h"

static unsigned char shd_ansi[GFX_CELLW * GFX_CELLH * 6];

/* Average a source rectangle so ANSI covers the entire framebuffer, even
 * when the terminal requires a smaller grid. Each cell has two color bands. */
static void sample_rgb(const unsigned char *fb, const unsigned char *dac, unsigned x0, unsigned x1,
                       unsigned y0, unsigned y1, unsigned char *rgb) {
  unsigned x, y, n = (x1 - x0) * (y1 - y0);
  unsigned sums[3] = { 0, 0, 0 };
  if (!n) { memset(rgb, 0, 3); return; }
  for (y = y0; y < y1; ++y) {
    for (x = x0; x < x1; ++x) {
      unsigned char px[3];
      const unsigned char * const d = dac + fb[y * GFX_W + x] * 3;
      px[0] = (unsigned char)((d[0] << 2) | (d[0] >> 4));
      px[1] = (unsigned char)((d[1] << 2) | (d[1] >> 4));
      px[2] = (unsigned char)((d[2] << 2) | (d[2] >> 4));
      sums[0] += px[0]; sums[1] += px[1]; sums[2] += px[2];
    }
  }
  rgb[0] = (unsigned char)(sums[0] / n);
  rgb[1] = (unsigned char)(sums[1] / n);
  rgb[2] = (unsigned char)(sums[2] / n);
}

void vidansi_emit(const void *mem, const unsigned char *dac, int repaint,
                  int roff, int coff, unsigned cols, unsigned rows) {
  char out[16384];
  char *o = out;
  const unsigned char * const fb = (const unsigned char*)mem + GFX_BASE;
  unsigned char rgb[6];
  unsigned long lfg = ~0UL, lbg = ~0UL;
  int r, c, expect = -1;
  if (!cols || cols > GFX_CELLW || !rows || rows > GFX_CELLH) return;
  for (r = 0; r < (int)rows; ++r) {
    const unsigned y0 = (unsigned)r * GFX_H / rows;
    const unsigned ym = ((unsigned)r * 2 + 1) * GFX_H / (rows * 2);
    const unsigned y1 = ((unsigned)r + 1) * GFX_H / rows;
    for (c = 0; c < (int)cols; ++c) {
      const int cell = r * GFX_CELLW + c;
      unsigned char * const sh = shd_ansi + cell * 6;
      unsigned long fgp, bgp;
      const unsigned x0 = (unsigned)c * GFX_W / cols;
      const unsigned x1 = ((unsigned)c + 1) * GFX_W / cols;
      sample_rgb(fb, dac, x0, x1, y0, ym, rgb);
      sample_rgb(fb, dac, x0, x1, ym, y1, rgb + 3);
      if (!repaint && memcmp(sh, rgb, 6) == 0) continue;
      memcpy(sh, rgb, 6);
      if (o > out + sizeof(out) - 96) { (void)!write(1, out, o - out); o = out; lfg = lbg = ~0UL; expect = -1; }
      if (cell != expect || c == 0) {
        o += sprintf(o, "\x1b[%d;%dH", r + roff + 1, c + coff + 1);
      }
      expect = cell + 1;
      fgp = (unsigned long)rgb[0] << 16 | (unsigned long)rgb[1] << 8 | rgb[2];
      bgp = (unsigned long)rgb[3] << 16 | (unsigned long)rgb[4] << 8 | rgb[5];
      if (fgp != lfg) {
        lfg = fgp;
        o += sprintf(o, "\x1b[38;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2]);
      }
      if (bgp != lbg) {
        lbg = bgp;
        o += sprintf(o, "\x1b[48;2;%d;%d;%dm", rgb[3], rgb[4], rgb[5]);
      }
      *o++ = (char)0xe2; *o++ = (char)0x96; *o++ = (char)0x80;  /* U+2580. */
    }
  }
  if (o != out) { memcpy(o, "\x1b[0m", 4); o += 4; (void)!write(1, out, o - out); }
}

