#include "kvikdos.h"
#include "intrun.h"

/* DOS CON device emulation for terminal output while no text mode is active
 * (vid_active == 0).  Real DOS CON writes every byte through the BIOS
 * teletype (int 10h AH=0eh): printable characters advance the cursor and
 * wrap at the screen width (BDA word at 0x44a, normally 80), a bare '\n' is
 * cooked to "\r\n" (vDos dev_con.h), tab expands to 8, and control or
 * high-half CP437 bytes render as screen glyphs.
 *
 * Without this the raw bytes land on the host terminal, which wraps at its
 * own width — so on a console wider than 80 columns DOS output bleeds to
 * the right of column 80.
 *
 * ANSI escape bytes pass through to the host terminal uncounted; a minimal
 * CSI parser keeps the tracked column in sync for cursor-movement finals.
 * Output to a non-tty fd (pipe/redirection) stays raw like a redirected
 * DOS handle. */

static unsigned con_col;          /* Tracked host-terminal column (0-based). */
static unsigned con_saved_col;    /* Column saved by CSI s. */
static unsigned char con_esc;     /* 0 text, 1 after ESC, 2 CSI, 3 OSC, 4 OSC+ESC. */
static int con_p1, con_p2, con_np;  /* CSI numeric params; -1 = absent. */

static unsigned con_ncols(const void *mem) {
  const unsigned n = *(const unsigned short*)((const char*)mem + 0x44a);
  return n >= 20 && n <= 250 ? n : VID_COLS;
}

/* One escape byte (already emitted); updates con_esc and *colp. */
static void con_esc_byte(unsigned char c, unsigned *colp, unsigned ncols) {
  switch (con_esc) {
  case 1:
    if (c == '[') { con_esc = 2; con_p1 = con_p2 = -1; con_np = 0; }
    else if (c == ']') con_esc = 3;
    else con_esc = 0;  /* Two-byte sequence (ESC c, ESC 7, ...) done. */
    break;
  case 2:  /* CSI param/intermediate bytes. */
    if (c >= '0' && c <= '9') {
      int *pp = con_np ? &con_p2 : &con_p1;
      *pp = (*pp < 0 ? 0 : *pp) * 10 + (c - '0');
    } else if (c == ';') {
      con_np = 1;
    } else if (c >= 0x40 && c <= 0x7e) {  /* Final byte. */
      const unsigned a = con_p1 < 0 ? 1 : (unsigned)con_p1;
      const unsigned b = con_p2 < 0 ? 1 : (unsigned)con_p2;
      switch (c) {
      case 'C': *colp += a; break;                          /* Cursor right. */
      case 'D': *colp = *colp > a ? *colp - a : 0; break;   /* Cursor left. */
      case 'E': case 'F': *colp = 0; break;                 /* Next/prev line -> col 0. */
      case 'G': case '`': *colp = a ? a - 1 : 0; break;     /* Horizontal absolute. */
      case 'H': case 'f': *colp = b ? b - 1 : 0; break;     /* Position: param2 = col. */
      case 's': con_saved_col = *colp; break;
      case 'u': *colp = con_saved_col; break;
      }
      if (*colp >= ncols) *colp = ncols - 1;
      con_esc = 0;
    }
    break;
  case 3:  /* OSC: ends on BEL or ST (ESC \). */
    if (c == '\a') con_esc = 0;
    else if (c == 0x1b) con_esc = 4;
    break;
  default: con_esc = 0; break;  /* case 4: swallow ST's terminator byte. */
  }
}

/* Write guest console output to a host fd with CON semantics.
 * is_con: the DOS object is CON (int 21h con calls, int 29h, int 10h
 * teletype, handles 1/2).  Non-CON handles (AUX/PRN/files) write raw. */
void con_write(void *mem, const char *p, const char *end, int fd, int is_con) {
  char buf[1024];
  unsigned bn = 0;
  const unsigned ncols = con_ncols(mem);
  unsigned short * const cursor = (unsigned short*)((char*)mem + 0x450);
  int syncbda;
  unsigned col;
  if (!is_con || !isatty(fd)) { (void)!write(fd, p, end - p); return; }
  if (vid_active) { vid_write_str(mem, p, end); vid_render(mem); return; }
  /* The BDA cursor column (0x450 low byte, valid while row byte is 0) is the
   * CON column when the program tracks it via int 10h cursor calls;
   * otherwise the emulator-side con_col carries the position. */
  syncbda = is_stdout_write_cursor && !((const char*)mem)[0x451];
  col = syncbda ? (*cursor & 0xff) : con_col;
  if (col >= ncols) col = 0;
  for (; p != end; ++p) {
    const unsigned char c = (unsigned char)*p;
    /* Worst iteration is '\t': up to 8 spaces each followed by "\r\n" = 24. */
    if (bn > sizeof(buf) - 32) { (void)!write(fd, buf, bn); bn = 0; }
    if (con_esc) { buf[bn++] = (char)c; con_esc_byte(c, &col, ncols); continue; }
    switch (c) {
    case 0x1b: buf[bn++] = (char)c; con_esc = 1; break;
    case '\r': buf[bn++] = '\r'; col = 0; break;
    case '\n': if (col) buf[bn++] = '\r'; buf[bn++] = '\n'; col = 0; break;
    case '\b': buf[bn++] = '\b'; if (col) --col; break;
    case '\a': buf[bn++] = '\a'; break;
    case '\t':
      do {
        buf[bn++] = ' ';
        if (++col >= ncols) { buf[bn++] = '\r'; buf[bn++] = '\n'; col = 0; }
      } while (col % 8);
      break;
    default:
      bn = (unsigned)(vid_cp437_utf8(buf + bn, c) - buf);  /* CP437 -> UTF-8 glyph. */
      if (++col >= ncols) { buf[bn++] = '\r'; buf[bn++] = '\n'; col = 0; }
      break;
    }
  }
  if (bn) (void)!write(fd, buf, bn);
  con_col = col;
  if (syncbda) *cursor = (unsigned short)col;  /* Row byte stays 0. */
}

/* Minimal stdout emitter shared by int 29h, int 10h teletype and the DOS
 * console-write calls: stdout_write_p..stdout_write_end bound the bytes. */
void emit_stdout(void) {
  con_write(mem, stdout_write_p, stdout_write_end, 1, 1);
}
