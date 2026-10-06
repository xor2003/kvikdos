#include "kvikdos.h"

static unsigned char vid_shadow[VID_BUFSZ];  /* Shadow of the rendered framebuffer. */

static int vid_last_attr = -1;   /* Last SGR attribute emitted; -1 = none. */

char vid_active = 0;      /* Nonzero: render 0xb8000 to the terminal. */

char vid_wrap_pend = 0;   /* Nonzero: last cell of a row was written; the wrap is deferred until the next char (avoids a spurious scroll when the bottom-right cell is filled). */

static char vid_altscreen = 0;   /* Nonzero: the terminal alternate screen is up. */

int vid_tty_fd = -1;      /* Host fd holding raw mode, -1 if not taken. */

char vid_raw_taken = 0;   /* Nonzero: we put the tty in raw mode. */

struct termios vid_saved_tio;  /* Saved lflag/tc state to restore at exit. */

static char vid_release_registered = 0;

int vid_cur_shape = -2;   /* Last emitted DECSCUSR style (0..7) or -1 hidden; -2 = unknown. */

static const unsigned short cp437_high[128] = {
  0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
  0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
  0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
  0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
  0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
  0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
  0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
  0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
  0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
  0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
  0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
  0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
  0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
  0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x0020 };

unsigned char cga_to_ansi(unsigned char c) {
  return (unsigned char)((c & 2) | ((c & 1) << 2) | ((c & 4) >> 2));
}

char *vid_utf8(char *o, unsigned cp) {
  if (cp < 0x80) {
    *o++ = (char)cp;
  } else if (cp < 0x800) {
    *o++ = (char)(0xc0 | (cp >> 6));
    *o++ = (char)(0x80 | (cp & 0x3f));
  } else {
    *o++ = (char)(0xe0 | (cp >> 12));
    *o++ = (char)(0x80 | ((cp >> 6) & 0x3f));
    *o++ = (char)(0x80 | (cp & 0x3f));
  }
  return o;
}

void vid_term_release(void) {  /* Registered via atexit(). */
  if (vid_raw_taken && vid_tty_fd >= 0) tcsetattr(vid_tty_fd, 0, &vid_saved_tio);
  vid_raw_taken = 0;
  if (vid_altscreen) {
    static const char leave_seq[] = "\x1b[0m\x1b[?12l\x1b[?25h\x1b[0 q\x1b[?1049l";
    (void)!write(1, leave_seq, sizeof(leave_seq) - 1);
    vid_altscreen = 0;
  }
}

void vid_enter(void) {  /* Activate text mode (alt screen + fresh repaint). */
  static const char enter_seq[] = "\x1b[?1049h\x1b[2J\x1b[H\x1b[?12h";  /* ?12h enables a blinking cursor; vid_render() places it at the DOS cursor. */
  if (!vid_release_registered) { vid_release_registered = 1; atexit(vid_term_release); }
  if (!vid_altscreen) { (void)!write(1, enter_seq, sizeof(enter_seq) - 1); vid_altscreen = 1; }
  memset(vid_shadow, 0xff, sizeof(vid_shadow));  /* Force a full repaint. */
  vid_last_attr = -1;
  vid_cur_shape = -2;
  vid_active = 1;
}

void vid_render(void *mem) {
  const unsigned char *v = (const unsigned char*)mem + VID_BASE;
  char out[16384];
  char *o = out;
  int i, row, col, at, la, expect;
  unsigned cur;
  if (!vid_active) {  /* Auto-detect once the guest draws a real screen. */
    int filled = 0;
    for (i = 0; i < VID_COLS * VID_ROWS * 2; i += 2) {
      const unsigned char ch = v[i];
      if (ch != 0 && ch != ' ' && ++filled >= 80) break;
    }
    if (filled < 80) return;
    vid_enter();
  }
  la = vid_last_attr;
  expect = -1;  /* Next cell index that needs no cursor-position escape. */
  for (i = 0; i < VID_COLS * VID_ROWS; ++i) {
    const unsigned char ch = v[i << 1], at8 = v[(i << 1) + 1];
    if (ch == vid_shadow[i << 1] && at8 == vid_shadow[(i << 1) + 1]) continue;
    vid_shadow[i << 1] = ch; vid_shadow[(i << 1) + 1] = at8;
    if (o > out + sizeof(out) - 64) { (void)!write(1, out, o - out); o = out; la = -1; expect = -1; }
    if (i != expect) {  /* Not contiguous with the previous cell: move the cursor. */
      row = i / VID_COLS + 1; col = i % VID_COLS + 1;
      o += sprintf(o, "\x1b[%d;%dH", row, col);
    }
    expect = i + 1;
    at = at8;
    if (at != la) {
      /* CGA attr: bits 0-2 fg, bit3 fg bright, bits 4-6 bg, bit7 bg bright. */
      const int fg = cga_to_ansi(at & 7), bg = cga_to_ansi((at >> 4) & 7);
      la = at;
      o += sprintf(o, "\x1b[0;%d;%dm", ((at & 8) ? 90 : 30) + fg, ((at & 0x80) ? 100 : 40) + bg);
    }
    if (ch >= 0x80) o = vid_utf8(o, cp437_high[ch - 0x80]);
    else if (ch >= 0x20 && ch < 0x7f) *o++ = (char)ch;
    else *o++ = ' ';
  }
  vid_last_attr = la;
  /* Cursor shape/visibility from the BDA register (set by int 10h AH=01). */
  {
    unsigned cx = *(const unsigned short*)((const unsigned char*)mem + 0x460);
    int shape;
    if (cx & 0x2000) shape = -1;  /* CH bit5: cursor hidden. */
    else {
      unsigned start = (cx >> 8) & 0x1f, end = cx & 0x1f;
      /* Scan lines 0..7 in a text cell; DOS default underline is 6..7. */
      shape = (start <= 1 && end >= 6) ? 1 : (start >= 5 ? 3 : 5);  /* block / underline / bar */
    }
    if (shape != vid_cur_shape) {
      vid_cur_shape = shape;
      if (shape < 0) o += sprintf(o, "\x1b[?25l");              /* Hide. */
      else o += sprintf(o, "\x1b[?25h\x1b[%d q", shape);          /* Show + blinking style. */
    }
  }
  cur = *(const unsigned short*)((const unsigned char*)mem + 0x450);  /* BDA cursor, page 0. */
  row = ((cur >> 8) & 0xff) + 1; col = (cur & 0xff) + 1;
  if (row < 1) row = 1; else if (row > VID_ROWS) row = VID_ROWS;
  if (col < 1) col = 1; else if (col > VID_COLS) col = VID_COLS;
  o += sprintf(o, "\x1b[%d;%dH", row, col);
  if (o != out) { (void)!write(1, out, o - out); }
}

void vid_fill(void *mem, int top, int left, int bottom, int right, unsigned char ch, unsigned char attr) {
  unsigned char *v = (unsigned char*)mem + VID_BASE;
  int r, c;
  if (right >= VID_COLS) right = VID_COLS - 1;
  if (bottom >= VID_ROWS) bottom = VID_ROWS - 1;
  if (top > bottom || left > right) return;
  for (r = top; r <= bottom; ++r) for (c = left; c <= right; ++c) {
    v[(r * VID_COLS + c) << 1] = ch;
    v[((r * VID_COLS + c) << 1) + 1] = attr;
  }
}

void vid_scroll(void *mem, unsigned char al, unsigned char bh, unsigned short cx, unsigned short dx, int down) {
  unsigned char *v = (unsigned char*)mem + VID_BASE;
  int top = (cx >> 8) & 0xff, left = cx & 0xff, bottom = (dx >> 8) & 0xff, right = dx & 0xff;
  int n, r, c, src;
  if (right >= VID_COLS) right = VID_COLS - 1;
  if (bottom >= VID_ROWS) bottom = VID_ROWS - 1;
  if (top > bottom || left > right) return;
  n = al ? al : bottom - top + 1;  /* al == 0 clears the whole window. */
  if (down) {
    for (r = bottom; r >= top; --r) for (c = left; c <= right; ++c) {
      src = r - n;
      v[(r * VID_COLS + c) << 1] = src >= top ? v[(src * VID_COLS + c) << 1] : ' ';
      v[((r * VID_COLS + c) << 1) + 1] = src >= top ? v[((src * VID_COLS + c) << 1) + 1] : bh;
    }
  } else {
    for (r = top; r <= bottom; ++r) for (c = left; c <= right; ++c) {
      src = r + n;
      v[(r * VID_COLS + c) << 1] = src <= bottom ? v[(src * VID_COLS + c) << 1] : ' ';
      v[((r * VID_COLS + c) << 1) + 1] = src <= bottom ? v[((src * VID_COLS + c) << 1) + 1] : bh;
    }
  }
}

void vid_putc(void *mem, unsigned char ch) {
  unsigned char *v = (unsigned char*)mem + VID_BASE;
  unsigned short *cur = (unsigned short*)((unsigned char*)mem + 0x450);
  int row = (*cur >> 8) & 0xff, col = *cur & 0xff;
  if (col >= VID_COLS) col = VID_COLS - 1;   /* Clamp a stray cursor column to the 80-column width. */
  if (row >= VID_ROWS) row = VID_ROWS - 1;
  switch (ch) {
   case '\r': col = 0; vid_wrap_pend = 0; break;
   case '\n': ++row; vid_wrap_pend = 0; break;
   case '\b': if (col) --col; vid_wrap_pend = 0; break;
   case '\t': col = (col + 8) & ~7; vid_wrap_pend = 0; if (col >= VID_COLS) { col = 0; ++row; } break;
   case 7: break;  /* BEL. */
   default:
     if (vid_wrap_pend) { vid_wrap_pend = 0; col = 0; ++row; }  /* Commit a deferred end-of-line wrap. */
     v[(row * VID_COLS + col) << 1] = ch;  /* Keep the existing attribute byte. */
     if (++col >= VID_COLS) { vid_wrap_pend = 1; col = VID_COLS - 1; }  /* Stay on the last cell; wrap only when the next char arrives. */
  }
  if (row >= VID_ROWS) {
    vid_scroll(mem, 1, 7, 0, (unsigned short)(VID_ROWS - 1) << 8 | (VID_COLS - 1), 0);
    row = VID_ROWS - 1;
    vid_wrap_pend = 0;
  }
  *cur = (unsigned short)((row << 8) | col);
}

void vid_write_str(void *mem, const char *p, const char *end) {
  while (p != end) vid_putc(mem, (unsigned char)*p++);
}
