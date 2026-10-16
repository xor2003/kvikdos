#include "kvikdos.h"
#include "intrun.h"

/* VGA mode 13h (320x200, 256-color linear framebuffer at A000:0000).
 * Scope is deliberately narrow: the chunky mode only.  Planar and
 * unchained (mode-X) writes are interpreted linearly, and per-scanline
 * raster tricks vanish because whole frames are rendered, not beams.
 *
 * Emitters (EmuParams.video_emitter, --video=):
 *   ascii (default): GFX_CELLW x GFX_CELLH luminance grid, plain chars —
 *     legible in a captured stream to humans and console agents alike.
 *   ansi:            same grid as U+2580 half-block cells in 24-bit SGR
 *     color (top pixel = fg, bottom = bg); needs a truecolor terminal.
 *   off:             no drawing; the framebuffer and DAC still run.
 * --video-dump=FILE.png additionally writes a native-resolution PNG of
 * the framebuffer on SIGUSR1 and once at exit after graphics ran.
 *
 * Frames are gated to ~30 fps and emitted as a cell diff, like the text
 * renderer: only changed cells produce terminal output. */

#define GFX_FRAME_US 33333  /* ~30 fps emission gate. */

char vid_gfx = 0;
char vid_gfx_flush = 0;

static unsigned char vga_dac[0x300];  /* RAMDAC entries, 6-bit RGB triples. */
static unsigned dac_addr = 0;         /* Byte index 0..0x2ff into vga_dac. */
static unsigned char dac_stat = 0;    /* 0x3c7 read: 0 = write mode, 3 = read. */
static unsigned char dac_wlatch = 0;  /* Last 0x3c8 write (port read-back). */
static unsigned char pel_mask = 0xff;

static unsigned char shd_ansi[GFX_CELLW * GFX_CELLH * 6];  /* Emitted fg+bg RGB. */
static unsigned char shd_ascii[GFX_CELLW * GFX_CELLH];     /* Emitted ramp chars. */
static char gfx_repaint = 0;          /* Emit every cell, resize-aware. */
static unsigned gfx_tw = 0, gfx_th = 0;  /* Last terminal size used. */
static unsigned long gfx_last_us = 0;
static char gfx_seen = 0;             /* Mode 13h ran at least once. */
static volatile char gfx_dump_req = 0;
static char gfx_hooks = 0;

/* --- Default BIOS palette -------------------------------------------
 * Rebuild of the DAC contents the VGA BIOS loads on a mode 13h set:
 * entries 0-15 IRGB (index 6 = brown), 16-31 a gray ramp, 32-247 nine
 * 24-color hue cycles (three value groups x three saturations), 248-255
 * black.  Matches DOSBox 0.74's vga_palette (int10_modes.cpp); the
 * construction follows canidlogic/vgapal (MIT). */

static unsigned pal_n;

static void pal_add(int r, int g, int b) {
  const unsigned i = pal_n * 3;
  vga_dac[i] = (unsigned char)r;
  vga_dac[i + 1] = (unsigned char)g;
  vga_dac[i + 2] = (unsigned char)b;
  ++pal_n;
}

static int pal_run(int start, int ch, int lo, int melo, int me, int mehi, int hi) {
  int r = lo, g = lo, b = lo, i, up, v;
  if (start & 4) r = hi;
  if (start & 2) g = hi;
  if (start & 1) b = hi;
  pal_add(r, g, b);
  up = !(start & ch);
  for (i = 0; i < 3; ++i) {
    v = up ? (i == 0 ? melo : i == 1 ? me : mehi) : (i == 0 ? mehi : i == 1 ? me : melo);
    if (ch == 4) r = v;
    else if (ch == 2) g = v;
    else b = v;
    pal_add(r, g, b);
  }
  return start ^ ch;
}

static void pal_cycle(int lo, int melo, int me, int mehi, int hi) {
  int h = 1;
  h = pal_run(h, 4, lo, melo, me, mehi, hi);
  h = pal_run(h, 1, lo, melo, me, mehi, hi);
  h = pal_run(h, 2, lo, melo, me, mehi, hi);
  h = pal_run(h, 4, lo, melo, me, mehi, hi);
  h = pal_run(h, 1, lo, melo, me, mehi, hi);
  (void)pal_run(h, 2, lo, melo, me, mehi, hi);
}

static void dac_default(void) {
  static const unsigned char grays[16] = {
    0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63 };
  int i, h, l, r, g, b;
  pal_n = 0;
  for (i = 0; i < 16; ++i) {
    if (i & 8) { h = 63; l = 21; } else { h = 42; l = 0; }
    r = g = b = l;
    if (i & 4) r = h;
    if (i & 2) g = h;
    if (i & 1) b = h;
    if (i == 6) g = 21;  /* Brown, not dark yellow. */
    pal_add(r, g, b);
  }
  for (i = 0; i < 16; ++i) pal_add(grays[i], grays[i], grays[i]);
  pal_cycle(0, 16, 31, 47, 63);
  pal_cycle(31, 39, 47, 55, 63);
  pal_cycle(45, 49, 54, 58, 63);
  pal_cycle(0, 7, 14, 21, 28);
  pal_cycle(14, 17, 21, 24, 28);
  pal_cycle(20, 22, 24, 26, 28);
  pal_cycle(0, 4, 8, 12, 16);
  pal_cycle(8, 10, 12, 14, 16);
  pal_cycle(11, 12, 13, 15, 16);
  for (i = 0; i < 8; ++i) pal_add(0, 0, 0);
}

/* --- RAMDAC ports -----------------------------------------------------
 * 0x3c8 write selects the write index (byte-wise: idx*3, auto-increment
 * through R,G,B on 0x3c9 access), 0x3c7 write selects the read index and
 * reads back the DAC state (0 = write, 3 = read), 0x3c6 is the PEL mask.
 * Per msdos_player's vga_dac_regs model; loops cover rep insb/outsb. */

int vidgfx_port(unsigned port, int is_out, unsigned char *v) {
  switch (port) {
   case 0x3c8:
    if (is_out) { dac_addr = (unsigned)*v * 3; dac_stat = 0; dac_wlatch = *v; }
    else *v = dac_wlatch;
    return 1;
   case 0x3c7:
    if (is_out) { dac_addr = (unsigned)*v * 3; dac_stat = 3; }
    else *v = dac_stat;
    return 1;
   case 0x3c9:
    if (is_out) vga_dac[dac_addr % 0x300] = *v & 0x3f;  /* 6-bit RAMDAC. */
    else *v = vga_dac[dac_addr % 0x300];
    ++dac_addr;
    return 1;
   case 0x3c6:
    if (is_out) pel_mask = *v;
    else *v = pel_mask;
    return 1;
  }
  return 0;
}

/* --- BIOS pixel/scroll services --------------------------------------- */

void vidgfx_wr_px(void *mem, unsigned x, unsigned y, unsigned char c) {
  unsigned char *p;
  if (x >= GFX_W || y >= GFX_H) return;
  p = (unsigned char*)mem + GFX_BASE + y * GFX_W + x;
  *p = (c & 0x80) ? *p ^ c : c;  /* AL bit7: XOR onto screen. */
}

unsigned char vidgfx_rd_px(const void *mem, unsigned x, unsigned y) {
  if (x >= GFX_W || y >= GFX_H) return 0;
  return ((const unsigned char*)mem)[GFX_BASE + y * GFX_W + x];
}

/* int 10h AH=06/07 in graphics mode: the window is a text-cell rect on
 * the 40x25 grid (8 px cells), AL lines (0 = clear), BH = fill color. */
void vidgfx_scroll(void *mem, int top, int left, int bottom, int right, int n, int down, unsigned char fill) {
  unsigned char * const fb = (unsigned char*)mem + GFX_BASE;
  int pt = top * 8, pl = left * 8, pb = bottom * 8 + 7, pr = right * 8 + 7;
  int np, w, r;
  if (pt < 0) pt = 0;
  if (pl < 0) pl = 0;
  if (pr > GFX_W - 1) pr = GFX_W - 1;
  if (pb > GFX_H - 1) pb = GFX_H - 1;
  if (pt > pb || pl > pr) return;
  w = pr - pl + 1;
  np = (n <= 0 || n * 8 > pb - pt + 1) ? pb - pt + 1 : n * 8;
  if (down) {
    /* Bottom-up iteration reads each row before it is overwritten. */
    for (r = pb; r - np >= pt; --r)
      memcpy(fb + r * GFX_W + pl, fb + (r - np) * GFX_W + pl, (size_t)w);
    for (; r >= pt; --r) memset(fb + r * GFX_W + pl, fill, (size_t)w);
  } else {
    for (r = pt; r + np <= pb; ++r)
      memcpy(fb + r * GFX_W + pl, fb + (r + np) * GFX_W + pl, (size_t)w);
    for (; r <= pb; ++r) memset(fb + r * GFX_W + pl, fill, (size_t)w);
  }
}

/* int 10h AH=10h DAC services in graphics mode.  AL: 0x10 set entry BX to
 * DH/CH/CL; 0x12 load CX entries from ES:DX; 0x15 read entry BX into
 * DH/CH/CL; 0x17 store CX entries at ES:DX; 0x1b sum CX entries from BX
 * to gray; 0x03 keeps the shared blink toggle; the attribute-register
 * subfunctions are nops in mode 13h. */
void vidgfx_palette(void *mem) {
  const unsigned al = (unsigned char)regs.rax;
  const unsigned bx = *(unsigned short*)&regs.rbx;
  const unsigned cx = *(unsigned short*)&regs.rcx;
  if (al == 0x10) {
    vga_dac[bx * 3] = ((const unsigned char*)&regs.rdx)[1] & 0x3f;
    vga_dac[bx * 3 + 1] = ((const unsigned char*)&regs.rcx)[1] & 0x3f;
    vga_dac[bx * 3 + 2] = (unsigned char)regs.rcx & 0x3f;
  } else if (al == 0x15) {
    ((unsigned char*)&regs.rdx)[1] = vga_dac[bx * 3];
    ((unsigned char*)&regs.rcx)[1] = vga_dac[bx * 3 + 1];
    *(unsigned char*)&regs.rcx = vga_dac[bx * 3 + 2];
  } else if (al == 0x12 || al == 0x17) {
    const unsigned src = ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdx);
    unsigned i;
    for (i = 0; i < cx && bx + i < 256; ++i) {
      if (al == 0x12) {
        memcpy(vga_dac + (bx + i) * 3, (const unsigned char*)mem + src + i * 3, 3);
      } else {
        memcpy((unsigned char*)mem + src + i * 3, vga_dac + (bx + i) * 3, 3);
      }
    }
  } else if (al == 0x1b) {
    unsigned i;
    for (i = 0; i < cx && bx + i < 256; ++i) {
      unsigned char * const d = vga_dac + (bx + i) * 3;
      const unsigned char gy = (unsigned char)((d[0] * 30 + d[1] * 59 + d[2] * 11) / 100);
      d[0] = d[1] = d[2] = gy;
    }
  } else if (al == 0x03) {
    vid_blink = ((unsigned char)regs.rbx & 1) ? 1 : 0;
  }
}

/* --- PNG dump -----------------------------------------------------------
 * Minimal PNG writer: zlib stream made of stored (uncompressed) DEFLATE
 * blocks, one IDAT, CRC-32 per chunk.  No zlib dependency, C89-clean. */

static unsigned long crc_tab[256];
static char crc_ready = 0;
static unsigned long png_crc;

static void crc_make(void) {
  unsigned long c;
  unsigned i, k;
  for (i = 0; i < 256; ++i) {
    c = i;
    for (k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
    crc_tab[i] = c;
  }
  crc_ready = 1;
}

static void wbytes(FILE *f, const unsigned char *p, unsigned n, int do_crc) {
  unsigned i;
  for (i = 0; i < n; ++i) {
    fputc(p[i], f);
    if (do_crc) png_crc = crc_tab[(png_crc ^ p[i]) & 0xff] ^ (png_crc >> 8);
  }
}

static void wbe32(FILE *f, unsigned long v, int do_crc) {
  unsigned char b[4];
  b[0] = (unsigned char)(v >> 24);
  b[1] = (unsigned char)(v >> 16);
  b[2] = (unsigned char)(v >> 8);
  b[3] = (unsigned char)v;
  wbytes(f, b, 4, do_crc);
}

static void wchunk(FILE *f, const char *type, const unsigned char *data, unsigned len) {
  png_crc = 0xffffffffUL;
  wbe32(f, len, 0);
  wbytes(f, (const unsigned char*)type, 4, 1);
  if (data) wbytes(f, data, len, 1);
  wbe32(f, png_crc ^ 0xffffffffUL, 0);
}

void vidgfx_dump(void *mem) {
  static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  static const unsigned char ihdr[13] = {
    (GFX_W >> 8) & 0xff, GFX_W & 0xff, (GFX_H >> 8) & 0xff, GFX_H & 0xff,
    8, 2, 0, 0, 0 };
  const char * const path = emu_params ? emu_params->video_dump_filename : NULL;
  const unsigned char * const fb = (const unsigned char*)mem + GFX_BASE;
  const unsigned rawsz = GFX_H * (GFX_W * 3 + 1);
  unsigned char *raw, *z, *rp;
  unsigned long a, b, zlen, i;
  unsigned x, y, o, rem;
  FILE *f;
  if (!path) return;
  if (!crc_ready) crc_make();
  if (!(raw = (unsigned char*)malloc(rawsz)) ||
      !(z = (unsigned char*)malloc(rawsz + rawsz / 65535 * 5 + 16))) {
    free(raw);
    fprintf(stderr, "error: --video-dump: out of memory\n");
    return;
  }
  rp = raw;
  for (y = 0; y < GFX_H; ++y) {
    *rp++ = 0;  /* Filter type: none. */
    for (x = 0; x < GFX_W; ++x) {
      const unsigned char * const d = vga_dac + fb[y * GFX_W + x] * 3;
      *rp++ = (unsigned char)((d[0] << 2) | (d[0] >> 4));
      *rp++ = (unsigned char)((d[1] << 2) | (d[1] >> 4));
      *rp++ = (unsigned char)((d[2] << 2) | (d[2] >> 4));
    }
  }
  z[0] = 0x78; z[1] = 0x01;  /* zlib: deflate, no preset dictionary. */
  o = 2;
  rem = rawsz;
  i = 0;
  do {
    const unsigned len = rem > 65535 ? 65535 : rem;
    z[o++] = (unsigned char)(rem <= 65535);
    z[o++] = (unsigned char)len;
    z[o++] = (unsigned char)(len >> 8);
    z[o++] = (unsigned char)~len;
    z[o++] = (unsigned char)(~len >> 8);
    memcpy(z + o, raw + i, len);
    o += len;
    i += len;
    rem -= len;
  } while (rem);
  a = 1; b = 0;
  for (i = 0; i < rawsz; ++i) {  /* Adler-32 of the raw image. */
    a = (a + raw[i]) % 65521;
    b = (b + a) % 65521;
  }
  zlen = o;
  z[zlen++] = (unsigned char)(b >> 8);
  z[zlen++] = (unsigned char)b;
  z[zlen++] = (unsigned char)(a >> 8);
  z[zlen++] = (unsigned char)a;
  if ((f = fopen(path, "wb"))) {
    wbytes(f, sig, 8, 0);
    wchunk(f, "IHDR", ihdr, 13);
    wchunk(f, "IDAT", z, (unsigned)zlen);
    wchunk(f, "IEND", NULL, 0);
    fclose(f);
  } else {
    fprintf(stderr, "error: --video-dump: cannot write %s\n", path);
  }
  free(raw);
  free(z);
}

static void gfx_atexit_dump(void) {
  if (gfx_seen && emu_params && emu_params->video_dump_filename)
    vidgfx_dump(mem);
}

static void gfx_sig_usr1(int sig) {
  (void)sig;
  gfx_dump_req = 1;
}

/* --- Mode entry -------------------------------------------------------- */

void vidgfx_enter(void *mem, int keep_fb) {
  unsigned char * const b = (unsigned char*)mem;
  b[0x449] = 0x13;                                     /* Current mode. */
  *(unsigned short*)(b + 0x44a) = 40;                  /* Text columns. */
  *(unsigned short*)(b + 0x44c) = VID_PAGE_STRIDE;     /* Regen size. */
  *(unsigned short*)(b + 0x44e) = 0;                   /* Page start. */
  memset(b + 0x450, 0, 16);
  b[0x460] = 0x0d;  b[0x461] = 0x0e;
  b[0x462] = 0;
  *(unsigned short*)(b + 0x463) = 0x3d4;
  b[0x484] = 24;                                       /* Rows - 1. */
  *(unsigned short*)(b + 0x485) = 8;                   /* Char height. */
  dac_default();
  dac_addr = dac_stat = dac_wlatch = 0;
  pel_mask = 0xff;
  if (!keep_fb) memset(b + GFX_BASE, 0, GFX_W * GFX_H);
  if (!gfx_hooks) {
    struct sigaction sa;
    gfx_hooks = 1;
    atexit(gfx_atexit_dump);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gfx_sig_usr1;
    (void)sigaction(SIGUSR1, &sa, NULL);
  }
  gfx_seen = 1;
  gfx_repaint = 1;
  gfx_tw = gfx_th = 0;
  gfx_last_us = 0;
  vid_gfx_flush = 1;
  vid_enter();
  (void)!write(1, "\x1b[?25l", 6);  /* No text cursor in graphics mode. */
  vid_gfx = 1;
}

/* --- Emitters ------------------------------------------------------------
 * Both emit GFX_CELLW x GFX_CELLH cells clipped to (and centered in) the
 * terminal; with no tty (piped output) the full grid is emitted, which is
 * what an agent reading the capture wants.  Each cell needs at most a
 * position escape, an SGR pair and a 3-byte glyph: flush at margin 96. */

static void pal_rgb(unsigned char idx, unsigned char *rgb) {
  const unsigned char * const d = vga_dac + idx * 3;
  rgb[0] = (unsigned char)((d[0] << 2) | (d[0] >> 4));
  rgb[1] = (unsigned char)((d[1] << 2) | (d[1] >> 4));
  rgb[2] = (unsigned char)((d[2] << 2) | (d[2] >> 4));
}

/* Expand two palette indices to 8-bit RGB and store their average. */
static void avg_rgb(unsigned char a, unsigned char b, unsigned char *rgb) {
  const unsigned char * const x = vga_dac + a * 3;
  const unsigned char * const y = vga_dac + b * 3;
  rgb[0] = (unsigned char)((((x[0] << 2) | (x[0] >> 4)) + ((y[0] << 2) | (y[0] >> 4)) + 1) >> 1);
  rgb[1] = (unsigned char)((((x[1] << 2) | (x[1] >> 4)) + ((y[1] << 2) | (y[1] >> 4)) + 1) >> 1);
  rgb[2] = (unsigned char)((((x[2] << 2) | (x[2] >> 4)) + ((y[2] << 2) | (y[2] >> 4)) + 1) >> 1);
}

static void emit_ansi(const void *mem, int roff, int coff, unsigned tw, unsigned th, int have_ws) {
  char out[16384];
  char *o = out;
  const unsigned char * const fb = (const unsigned char*)mem + GFX_BASE;
  unsigned char rgb[6];
  unsigned long lfg = ~0UL, lbg = ~0UL;
  int r, c, expect = -1;
  for (r = 0; r < GFX_CELLH; ++r) {
    const unsigned char * const rowT = fb + r * 2 * GFX_W;
    const unsigned char * const rowB = rowT + GFX_W;
    for (c = 0; c < GFX_CELLW; ++c) {
      const int cell = r * GFX_CELLW + c;
      unsigned char * const sh = shd_ansi + cell * 6;
      unsigned long fgp, bgp;
      avg_rgb(rowT[c * 2], rowT[c * 2 + 1], rgb);       /* fg = top pixel. */
      avg_rgb(rowB[c * 2], rowB[c * 2 + 1], rgb + 3);   /* bg = bottom. */
      if (!gfx_repaint && memcmp(sh, rgb, 6) == 0) continue;
      if (have_ws && ((unsigned)(r + roff) >= th || (unsigned)(c + coff) >= tw)) continue;  /* Leave the shadow stale for a grow-repaint. */
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

static void emit_ascii(const void *mem, int roff, int coff, unsigned tw, unsigned th, int have_ws) {
  static const char ramp[] = " .:-=+*#%@";
  char out[16384];
  char *o = out;
  const unsigned char * const fb = (const unsigned char*)mem + GFX_BASE;
  unsigned char px[3];
  int r, c, rr, expect = -1;
  for (r = 0; r < GFX_CELLH; ++r) {
    for (c = 0; c < GFX_CELLW; ++c) {
      const int cell = r * GFX_CELLW + c;
      unsigned lum = 0;
      unsigned idx;
      char ch;
      for (rr = 0; rr < 4; ++rr) {  /* 2x4 source block per cell. */
        pal_rgb(fb[(r * 4 + rr) * GFX_W + c * 2], px);
        lum += (unsigned)px[0] * 77 + (unsigned)px[1] * 150 + (unsigned)px[2] * 29;
        pal_rgb(fb[(r * 4 + rr) * GFX_W + c * 2 + 1], px);
        lum += (unsigned)px[0] * 77 + (unsigned)px[1] * 150 + (unsigned)px[2] * 29;
      }
      idx = (lum >> 3 >> 8) * 10 / 256;  /* avg luma 0..255 -> ramp index. */
      ch = ramp[idx > 9 ? 9 : idx];
      if (!gfx_repaint && shd_ascii[cell] == (unsigned char)ch) continue;
      if (have_ws && ((unsigned)(r + roff) >= th || (unsigned)(c + coff) >= tw)) continue;
      shd_ascii[cell] = (unsigned char)ch;
      if (o > out + sizeof(out) - 96) { (void)!write(1, out, o - out); o = out; expect = -1; }
      if (cell != expect || c == 0) {
        o += sprintf(o, "\x1b[%d;%dH", r + roff + 1, c + coff + 1);
      }
      expect = cell + 1;
      *o++ = ch;
    }
  }
  if (o != out) { (void)!write(1, out, o - out); }
}

void vidgfx_render(void *mem) {
  struct timeval tv;
  struct winsize ws;
  unsigned long now;
  unsigned tw, th;
  int have_ws, roff, coff;
  if (gfx_dump_req) { gfx_dump_req = 0; vidgfx_dump(mem); }
  if (emu_params->video_emitter == VID_EMIT_OFF) return;
  gettimeofday(&tv, NULL);
  now = (unsigned long)(tv.tv_sec % 600) * 1000000UL + (unsigned long)tv.tv_usec;
  if (!vid_gfx_flush && now - gfx_last_us < GFX_FRAME_US) return;
  vid_gfx_flush = 0;
  gfx_last_us = now;
  have_ws = ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row;
  tw = have_ws ? ws.ws_col : 0;
  th = have_ws ? ws.ws_row : 0;
  if (tw != gfx_tw || th != gfx_th) {  /* Resize: repaint under new clip. */
    gfx_tw = tw;
    gfx_th = th;
    gfx_repaint = 1;
  }
  roff = have_ws && th > GFX_CELLH ? (int)(th - GFX_CELLH) / 2 : 0;
  coff = have_ws && tw > GFX_CELLW ? (int)(tw - GFX_CELLW) / 2 : 0;
  if (emu_params->video_emitter == VID_EMIT_ANSI)
    emit_ansi(mem, roff, coff, tw, th, have_ws);
  else
    emit_ascii(mem, roff, coff, tw, th, have_ws);
  gfx_repaint = 0;
}
