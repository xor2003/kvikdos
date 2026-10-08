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

/* Borland 32RTM module teardown decrements a refcount stored in a
 * read-only page and takes a guest page fault; its handler then prints a
 * "32loader runtime error" dump *after* the program's real output.  Under
 * KVM that #PF is handled entirely by the guest (kvikdos never sees it),
 * and the fault is a genuine 32RTM bug that also fires on real DOS, so the
 * dump is cosmetic -- the compile/link result and the exit code are already
 * decided before it runs.
 *
 * We suppress it, but only once the dump reports the teardown's exact
 * signature -- a page fault (Exception 0E) writing a present read-only page
 * (error code 00000007), identical across 32RTM versions -- so a genuinely
 * different protected-mode crash still prints.  The dump header is withheld
 * into rtm_td_buf until that signature is confirmed, then replayed if it is
 * a real crash. */
#define RTM_TD_SIG "32loader runtime error: Unhandled exception"
#define RTM_TD_SIGLEN (sizeof(RTM_TD_SIG) - 1)
#define RTM_TD_BUFSZ 512
#define RTM_TD_WSMAX 8           /* leading blank padding the dump can emit */

/* A candidate dump is blank padding followed by the signature. */
#define rtm_td_ws(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')

static unsigned char rtm_td;     /* 0 normal, 1 capturing a candidate dump, 2 suppressing its tail. */
static char rtm_td_buf[RTM_TD_BUFSZ];
static unsigned rtm_td_len;      /* bytes withheld in rtm_td_buf */
static unsigned rtm_td_sigoff;   /* leading blank bytes before the signature prefix */

/* Scan a hex value (0-9a-fA-F) at *pp up to the buffer end; returns the
 * value and advances *pp, or -1 if no hex digit was present. */
static long rtm_td_hex(const char **pp, const char *e) {
  long v = 0;
  int got = 0;
  for (; *pp < e; ++*pp) {
    unsigned c = (unsigned char)**pp, d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else break;
    v = v * 16 + d; got = 1;
  }
  return got ? v : -1;
}

/* Classify the captured dump header: 0 keep waiting, 1 benign teardown,
 * -1 a different (real) crash. */
static int rtm_td_classify(void) {
  const char *e = rtm_td_buf + rtm_td_len;
  const char *p = memmem(rtm_td_buf, rtm_td_len, "error code", 10);
  long ec;
  if (!p) return 0;                 /* error code line not captured yet */
  p += 10;
  while (p < e && (*p == ' ' || *p == '\t' || *p == '=')) ++p;
  if (p == e) return 0;             /* "error code" value not captured yet */
  ec = rtm_td_hex(&p, e);
  if (ec < 0) return -1;            /* malformed -- not the standard dump */
  if (p == e) return 0;             /* digits reach the buffer end: maybe more
                                     * are still to come, keep waiting. */
  /* error code bit0 = page present, bit1 = write, bit2 = user.  The
   * teardown fault is a user-mode write to a present read-only page: 7. */
  return (ec == 7) ? 1 : -1;
}

/* Emit raw guest bytes through the CON/teletype path.  Separated from the
 * teardown-dump filter in con_write so withheld dump bytes can be replayed
 * without re-entering the filter. */
static void con_emit(void *mem, const char *p, const char *end, int fd, int is_con) {
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

/* Write guest console output to a host fd with CON semantics, after running
 * the 32RTM teardown-dump filter.  The dump may be emitted one byte at a time
 * (int 21h AH=02), so the signature is matched as a stream: a chunk that ends
 * part-way through the signature has that suffix withheld into rtm_td_buf
 * until the next bytes confirm or refute it.
 * is_con: the DOS object is CON (int 21h con calls, int 29h, int 10h
 * teletype, handles 1/2).  Non-CON handles (AUX/PRN/files) write raw. */
void con_write(void *mem, const char *p, const char *end, int fd, int is_con) {
  if (!(is_con && fd == 1)) { con_emit(mem, p, end, fd, is_con); return; }
  while (p != end) {
    if (rtm_td == 2) return;                /* suppressing the dump's tail */
    if (rtm_td == 0) {
      if (rtm_td_len) {
        /* Continuing a withheld candidate = blank padding + signature prefix.
         * Consume bytes while they keep the candidate alive. */
        for (;;) {
          unsigned m = rtm_td_len - rtm_td_sigoff;   /* signature bytes matched */
          if (p == end) return;                       /* candidate spans chunks */
          if (m == 0) {                               /* still in the blank run */
            if (rtm_td_ws((unsigned char)*p)) {
              if (rtm_td_sigoff == RTM_TD_WSMAX) break;   /* too long for padding */
              rtm_td_buf[rtm_td_len++] = *p++; ++rtm_td_sigoff; continue;
            }
            if (*p != RTM_TD_SIG[0]) break;
            rtm_td_buf[rtm_td_len++] = *p++;              /* '3' of "32loader" */
            continue;
          }
          if (*p != RTM_TD_SIG[m]) break;
          rtm_td_buf[rtm_td_len++] = *p++;
          if (rtm_td_len - rtm_td_sigoff == RTM_TD_SIGLEN) { rtm_td = 1; break; }
        }
        if (rtm_td == 1) continue;                      /* signature: go capture */
        /* The withheld bytes were not a dump -- emit them and rescan. */
        con_emit(mem, rtm_td_buf, rtm_td_buf + rtm_td_len, fd, is_con);
        rtm_td_len = rtm_td_sigoff = 0;
        continue;
      }
      /* Scanning for a fresh signature or a boundary candidate. */
      {
        const char *sig = (const char*)memmem(p, (size_t)(end - p), RTM_TD_SIG, RTM_TD_SIGLEN);
        unsigned k;
        if (sig) {
          /* The dump leads with blank padding in the same write; fold it into
           * the capture so it is suppressed/replayed along with the dump. */
          const char *q = sig;
          while (q > p && rtm_td_ws((unsigned char)q[-1])) --q;
          con_emit(mem, p, q, fd, is_con);
          p = q; rtm_td = 1; rtm_td_len = rtm_td_sigoff = 0; continue;
        }
        /* Withhold a trailing blank-run + signature-prefix candidate. */
        k = (unsigned)(end - p) < RTM_TD_SIGLEN ? (unsigned)(end - p) : RTM_TD_SIGLEN;
        for (; k; --k) if (!memcmp(end - k, RTM_TD_SIG, k)) break;
        {
          const char *tail = end - k;                 /* signature-prefix start */
          while (tail > p && rtm_td_ws((unsigned char)tail[-1]) &&
                 (end - k - tail) < RTM_TD_WSMAX) --tail;
          if (!k && tail == end) { con_emit(mem, p, end, fd, is_con); return; }
          con_emit(mem, p, tail, fd, is_con);
          memcpy(rtm_td_buf, tail, (size_t)(end - tail));
          rtm_td_len = (unsigned)(end - tail);
          rtm_td_sigoff = rtm_td_len - k;             /* leading blank bytes */
          return;
        }
      }
    }
    /* rtm_td == 1: capturing the dump; withhold it until the reported fault
     * signature decides benign-teardown (drop) vs. real crash (replay). */
    {
      unsigned room = (unsigned)sizeof(rtm_td_buf) - rtm_td_len;
      unsigned n = (unsigned)(end - p) < room ? (unsigned)(end - p) : room;
      int cls;
      memcpy(rtm_td_buf + rtm_td_len, p, n); rtm_td_len += n; p += n;
      cls = rtm_td_classify();
      if (cls > 0) { rtm_td = 2; return; }  /* benign teardown: drop it all */
      if (cls < 0 || rtm_td_len == sizeof(rtm_td_buf)) {
        /* A real crash (or not the standard dump): replay what was withheld. */
        con_emit(mem, rtm_td_buf, rtm_td_buf + rtm_td_len, fd, is_con);
        rtm_td = 0; rtm_td_len = 0;
      } else return;                        /* undecided yet: keep withholding */
    }
  }
}

/* Reset the teardown-dump filter for a freshly loaded program.  Text still
 * withheld (a signature prefix or an undecided dump header) is flushed so it
 * is not lost; a confirmed-suppressed teardown is not. */
void con_teardown_reset(void *mem) {
  if (mem && rtm_td != 2 && rtm_td_len) con_emit(mem, rtm_td_buf, rtm_td_buf + rtm_td_len, 1, 1);
  rtm_td = 0; rtm_td_len = rtm_td_sigoff = 0;
}

/* Minimal stdout emitter shared by int 29h, int 10h teletype and the DOS
 * console-write calls: stdout_write_p..stdout_write_end bound the bytes. */
void emit_stdout(void) {
  con_write(mem, stdout_write_p, stdout_write_end, 1, 1);
}
