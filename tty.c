#include "kvikdos.h"

static const unsigned char scancodes[128] = {
    0x3e, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x0e, 0x0f, 0x24, 0x25,
    0x26, 0x0c, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11,
    0x2d, 0x15, 0x2c, 0x01, 0x2b, 0x1b, 0x07, 0x0c, 0x39, 0x02, 0x28, 0x04,
    0x05, 0x06, 0x08, 0x28, 0x0a, 0x0b, 0x09, 0x0d, 0x33, 0x0c, 0x34, 0x35,
    0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x27, 0x27,
    0x33, 0x0d, 0x34, 0x35, 0x03, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22,
    0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f,
    0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x1a, 0x2b, 0x1b, 0x07, 0x0c,
    0x29, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25,
    0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11,
    0x2d, 0x15, 0x2c, 0x1a, 0x2b, 0x1b, 0x29, 0x35 };

static const unsigned short fake_keys[3] = {
    0x011b /* <Esc> */, 0x4400 /* <F10> */, 0x1c0d /* <Enter> */ };

int tty_getc(TtyState *tty_state, int ms) {
  struct pollfd pfd;
  unsigned char c;
  pfd.fd = (tty_state->tty_in_fd == -2) ? 0 : tty_state->tty_in_fd;
  pfd.events = POLLIN;
  /* Retry on EINTR: the 18.2 Hz SIGALRM tick interrupts blocking waits. */
  for (;;) {
    const int r = poll(&pfd, 1, ms);
    if (r > 0) break;
    if (r == 0 || errno != EINTR) return -1;
  }
  if (read(pfd.fd, &c, 1) < 1) return -1;
  return c;
}

static const unsigned short keymods[22][4] = {
  {0x4800, 0x4800, 0x9800, 0x8d00},  /* <Up> */
  {0x5000, 0x5000, 0xa000, 0x9100},  /* <Down> */
  {0x4d00, 0x4d00, 0x9d00, 0x7400},  /* <Right> */
  {0x4b00, 0x4b00, 0x9b00, 0x7300},  /* <Left> */
  {0x4700, 0x4700, 0x9700, 0x7700},  /* <Home> */
  {0x4f00, 0x4f00, 0x9f00, 0x7500},  /* <End> */
  {0x4900, 0x4900, 0x9900, 0x8400},  /* <PgUp> */
  {0x5100, 0x5100, 0xa100, 0x7600},  /* <PgDn> */
  {0x5200, 0x5200, 0xa200, 0x9200},  /* <Insert> */
  {0x5300, 0x5300, 0xa300, 0x9300},  /* <Delete> */
  {0x3b00, 0x5400, 0x6800, 0x5e00},  /* <F1> */
  {0x3c00, 0x5500, 0x6900, 0x5f00},  /* <F2> */
  {0x3d00, 0x5600, 0x6a00, 0x6000},  /* <F3> */
  {0x3e00, 0x5700, 0x6b00, 0x6100},  /* <F4> */
  {0x3f00, 0x5800, 0x6c00, 0x6200},  /* <F5> */
  {0x4000, 0x5900, 0x6d00, 0x6300},  /* <F6> */
  {0x4100, 0x5a00, 0x6e00, 0x6400},  /* <F7> */
  {0x4200, 0x5b00, 0x6f00, 0x6500},  /* <F8> */
  {0x4300, 0x5c00, 0x7000, 0x6600},  /* <F9> */
  {0x4400, 0x5d00, 0x7100, 0x6700},  /* <F10> */
  {0x8500, 0x8700, 0x8b00, 0x8900},  /* <F11> */
  {0x8600, 0x8800, 0x8c00, 0x8a00},  /* <F12> */
};

int apply_mod(int k, int mod) {
  int slot = 0;  /* Prefer alt, then ctrl, then shift, for single-mod combos. */
  if (mod & 2) slot = 2; else if (mod & 4) slot = 3; else if (mod & 1) slot = 1;
  if (keymods[k][slot]) return keymods[k][slot];
  return keymods[k][0];
}

static unsigned tty_mods = 0;  /* BDA 0x417-format modifier bits for the key being decoded. */

int decode_esc(const unsigned char *b, int n) {
  int num, mod, i;
  if (n < 1) return 0x011b;  /* Bare <Esc>. */
  if (b[0] == 'O') {  /* SS3 (application-keypad) sequence. */
    if (n < 2) return 0x011b;
    switch (b[1]) {
     case 'P': return 0x3b00; case 'Q': return 0x3c00; case 'R': return 0x3d00; case 'S': return 0x3e00;  /* <F1>..<F4> */
     case 'A': return 0x4800; case 'B': return 0x5000; case 'C': return 0x4d00; case 'D': return 0x4b00;  /* Arrow keys. */
     case 'H': return 0x4700; case 'F': return 0x4f00;  /* <Home>, <End>. */
    }
    return 0x011b;
  }
  if (b[0] != '[') {  /* <Esc><char> = <Alt><char>. */
    if (b[0] == 0x1b) return 0x011b;  /* <Esc><Esc>: report as <Esc>. */
    if (b[0] < 0x80) { tty_mods |= 8; return (scancodes[b[0]] << 8); }  /* <Alt><key>: AH = scancode, AL = 0. */
    return 0x011b;
  }
  /* Parse CSI params: <Esc>[ p1 ; p2 ... <final>. Modifier is the 2nd param. */
  num = 0; mod = 0;
  for (i = 1; i < n && b[i] >= '0' && b[i] <= '9'; ++i) num = num * 10 + (b[i] - '0');
  if (i < n && b[i] == ';') {  /* Modifier param follows. */
    ++i;
    for (; i < n && b[i] >= '0' && b[i] <= '9'; ++i) mod = mod * 10 + (b[i] - '0');
    if (mod > 0) --mod;  /* xterm: param is 1 + modifier bits. */
    /* xterm modifier bits: 1=Shift 2=Alt 4=Ctrl -> BDA 0x417 bits:
     * Shift (bits 0+1), Ctrl (bit2), Alt (bit3). */
    if (mod & 1) tty_mods |= 3;
    if (mod & 2) tty_mods |= 8;
    if (mod & 4) tty_mods |= 4;
  }
  if (i < n && b[i] >= 'A' && b[i] <= 'Z' && b[i] != '~') {  /* CSI [p1;mod]<letter>. */
    switch (b[i]) {
     case 'A': return apply_mod(K_UP, mod); case 'B': return apply_mod(K_DOWN, mod);
     case 'C': return apply_mod(K_RIGHT, mod); case 'D': return apply_mod(K_LEFT, mod);
     case 'H': return apply_mod(K_HOME, mod); case 'F': return apply_mod(K_END, mod);
     case 'Z': return 0x0f09;  /* <Shift><Tab>. */
    }
    return 0x011b;
  }
  if (i < n && b[i] == '~') {  /* CSI <num>[;mod]~ */
    switch (num) {
     case 1: case 7: return apply_mod(K_HOME, mod);
     case 4: case 8: return apply_mod(K_END, mod);
     case 2: return apply_mod(K_INS, mod);
     case 3: return apply_mod(K_DEL, mod);
     case 5: return apply_mod(K_PGUP, mod);
     case 6: return apply_mod(K_PGDN, mod);
     case 11: case 12: case 13: case 14: return apply_mod(K_F1 + num - 11, mod);  /* <F1>..<F4> */
     case 15: return apply_mod(K_F5, mod);
     case 17: case 18: case 19: case 20: case 21: return apply_mod(K_F6 + num - 17, mod);  /* <F6>..<F10> */
     case 23: return apply_mod(K_F11, mod); case 24: return apply_mod(K_F12, mod);
    }
  }
  return 0x011b;
}

int read_keycode(TtyState *tty_state, int c) {
  unsigned char b[8];
  int n = 0, t, done = 0;
  tty_mods = 0;
  if (c != 0x1b) return (c & ~0x7f ? 0x3f : scancodes[c]) << 8 | (c & 0xff);
  while (n < (int)sizeof(b) && !done) {
    t = tty_getc(tty_state, n ? 6 : 15);  /* The first tail byte gets a bit longer. */
    if (t < 0) break;
    b[n++] = (unsigned char)t;
    if (b[0] == 'O') done = n >= 2;
    else if (b[0] == '[') done = n >= 2 && ((b[n - 1] >= 'A' && b[n - 1] <= 'Z') || b[n - 1] == '~');
    else done = 1;  /* Unknown introducer. */
  }
  return decode_esc(b, n);
}

void process_key(TtyState *tty_state, unsigned char ah, unsigned short *ax, unsigned short *flags) {
  if (tty_state->tty_in_fd == -3) {  /* Fake keys. */
    *ax = *tty_state->next_fake_key;
    if (ah & 1) {
      *flags &= ~(1 << 6);  /* ZF=0, key available in buffer. */
    } else {
      if (++tty_state->next_fake_key == fake_keys + sizeof(fake_keys) / sizeof(fake_keys[0])) tty_state->next_fake_key = fake_keys;
    }
  } else {
    int fd, key, applied = 0;
    tcflag_t old_lflag = 0;
    if (tty_state->tty_in_fd == -1) {
      if ((tty_state->tty_in_fd = open("/dev/tty", O_RDWR)) < 0) {  /* Current controlling terminal. */
        tty_state->tty_in_fd = -2;
      } else {
        tty_state->tty_in_fd = ensure_fd_is_at_least(tty_state->tty_in_fd, 5);
      }
    }
    fd = (tty_state->tty_in_fd == -2) ? 0 : tty_state->tty_in_fd;
    if (vid_active && !tty_state->raw_on && !tty_state->is_tty_in_error) {
      /* While a text-mode program runs, keep the tty in raw mode so each
       * keypress is delivered at once (canonical mode would hold the input
       * until <Enter>), and restore it on exit via vid_term_release(). */
      if (tcgetattr(fd, &vid_saved_tio) == 0) {
        struct termios rt = vid_saved_tio;
        rt.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);  /* TODO(pts): Handle Ctrl-<C> and other signals. */
        rt.c_iflag &= ~(IXON | ICRNL);
        rt.c_cc[VMIN] = 1;
        rt.c_cc[VTIME] = 0;
        if (tcsetattr(fd, 0, &rt) == 0) { vid_tty_fd = fd; vid_raw_taken = 1; tty_state->raw_on = 1; }
        else tty_state->is_tty_in_error = 1;
      } else tty_state->is_tty_in_error = 1;
    }
    if (!tty_state->raw_on && !tty_state->is_tty_in_error) {  /* Per-call raw mode (non-text-mode path). */
      struct termios tio;
      if (tcgetattr(tty_state->tty_in_fd, &tio) != 0) {
        tty_state->is_tty_in_error = 1;
      } else {
        old_lflag = tio.c_lflag;
        tio.c_lflag &= ~(ICANON | ECHO);  /* As a side effect, ECHOCTL is also disabled, so Ctrl-<C> won't show up as ^C, but it will still send SIGINT. */
        if (tcsetattr(tty_state->tty_in_fd, 0, &tio) != 0) tty_state->is_tty_in_error = 1;
        else applied = 1;
      }
    }
    if (tty_state->pending_key >= 0) {
      *ax = (unsigned short)tty_state->pending_key;
      if (ah & 1) *flags &= ~(1 << 6);  /* Key-check: report it, keep it buffered. */
      else tty_state->pending_key = -1;  /* Read: consume it. */
    } else if (ah & 1) {  /* Check for a key without removing it. */
      int c = tty_getc(tty_state, 0);
      if (c < 0) {
        *flags |= (1 << 6);  /* ZF=1, no key. */
      } else {
        key = read_keycode(tty_state, c);
        tty_state->pending_key = key;
        *ax = (unsigned short)key;
        *flags &= ~(1 << 6);  /* ZF=0, key available. */
      }
    } else {  /* Wait for and read a key. */
      int c;
      if ((c = tty_getc(tty_state, -1)) < 0) c = 26;  /* Ctrl-<Z>, simulate EOF. Most programs won't recognize it. */
      *ax = (unsigned short)read_keycode(tty_state, c);
    }
    /* Since we set ECHO back here, a call with ah == 0x01 followed by a call
     * with ah == 0x00 will echo the character to the Linux terminal. */
    if (applied) {  /* Restore the saved line flags for the non-text-mode path. */
      struct termios tio;
      if (tcgetattr(tty_state->tty_in_fd, &tio) == 0) {
        tio.c_lflag = old_lflag;
        if (tcsetattr(tty_state->tty_in_fd, 0, &tio) != 0) tty_state->is_tty_in_error = 1;
      }
    }
  }
}

/* --- BIOS keyboard buffer in the BDA (0x400 range), per msdos_player ---
 * Head/tail offsets live at 0x41a/0x41c (into BDA), buffer bounds at
 * 0x480/0x482; entries are words (scan<<8|ascii) at 0x400+offset.  Guest
 * writes to the BDA land here too, so direct-buffer readers see the same
 * state the int 16h handlers consume. */
void kbd_push(void *mem, unsigned key, int mods) {
  unsigned char * const b = (unsigned char*)mem;
  unsigned beg = *(unsigned short*)(b + 0x480), end = *(unsigned short*)(b + 0x482);
  const unsigned head = *(unsigned short*)(b + 0x41a);
  const unsigned tail = *(unsigned short*)(b + 0x41c);
  unsigned next;
  if (!beg || !end) { beg = 0x1e; end = 0x3e; }  /* Standard 16-key buffer. */
  next = tail + 2;
  if (next >= end) next = beg;
  if (next == head) return;  /* Full: drop the key (real BIOS beeps). */
  *(unsigned short*)(b + 0x400 + tail) = (unsigned short)key;
  *(unsigned short*)(b + 0x41c) = (unsigned short)next;
  if (mods >= 0) b[0x417] = (unsigned char)((b[0x417] & 0xf0) | (mods & 0x0f));
}

int kbd_pop(void *mem) {
  unsigned char * const b = (unsigned char*)mem;
  unsigned beg = *(unsigned short*)(b + 0x480), end = *(unsigned short*)(b + 0x482);
  const unsigned head = *(unsigned short*)(b + 0x41a);
  const unsigned tail = *(unsigned short*)(b + 0x41c);
  unsigned key, next;
  if (!beg || !end) { beg = 0x1e; end = 0x3e; }
  if (head == tail) return -1;  /* Empty. */
  key = *(unsigned short*)(b + 0x400 + head);
  next = head + 2;
  if (next >= end) next = beg;
  *(unsigned short*)(b + 0x41a) = (unsigned short)next;
  return (int)key;
}

int kbd_peek(void *mem) {
  const unsigned char * const b = (const unsigned char*)mem;
  const unsigned head = *(const unsigned short*)(b + 0x41a);
  const unsigned tail = *(const unsigned short*)(b + 0x41c);
  if (head == tail) return -1;
  return (int)*(const unsigned short*)(b + 0x400 + head);
}

/* True when the BDA keyboard buffer has room for one more word. The BIOS
 * int 9 handler checks this before popping the staged keycode queue so a
 * burst of host keys can't overflow the 16-entry buffer — real hardware
 * never outruns the typist, but pasted/bursty host input can. */
int kbd_can_push(const void *mem) {
  const unsigned char * const b = (const unsigned char*)mem;
  unsigned beg = *(const unsigned short*)(b + 0x480), end = *(const unsigned short*)(b + 0x482);
  const unsigned head = *(const unsigned short*)(b + 0x41a);
  const unsigned tail = *(const unsigned short*)(b + 0x41c);
  unsigned next;
  if (!beg || !end) { beg = 0x1e; end = 0x3e; }
  next = tail + 2;
  if (next >= end) next = beg;
  return next != head;
}

/* Text-mode raw takeover: while a text-mode program runs, keep the tty in
 * raw mode so each keypress is delivered at once (canonical mode would hold
 * the input until <Enter>), and restore it on exit via vid_term_release(). */
void tty_ensure_raw(TtyState *tty_state) {
  int fd;
  if (!vid_active || tty_state->raw_on || tty_state->is_tty_in_error) return;
  if (tty_state->tty_in_fd == -1) {
    if ((tty_state->tty_in_fd = open("/dev/tty", O_RDWR)) < 0) {
      tty_state->tty_in_fd = -2;
    } else {
      tty_state->tty_in_fd = ensure_fd_is_at_least(tty_state->tty_in_fd, 5);
    }
  }
  fd = (tty_state->tty_in_fd == -2) ? 0 : tty_state->tty_in_fd;
  if (tcgetattr(fd, &vid_saved_tio) == 0) {
    struct termios rt = vid_saved_tio;
    rt.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);  /* TODO(pts): Handle Ctrl-<C> and other signals. */
    rt.c_iflag &= ~(IXON | ICRNL);
    rt.c_cc[VMIN] = 1;
    rt.c_cc[VTIME] = 0;
    if (tcsetattr(fd, 0, &rt) == 0) { vid_tty_fd = fd; vid_raw_taken = 1; tty_state->raw_on = 1; }
    else tty_state->is_tty_in_error = 1;
  } else tty_state->is_tty_in_error = 1;
}

/* Per-call noncanonical mode for the non-text-mode path (canonical input
 * would hold keystrokes until <Enter>).  Returns nonzero if applied. */
static int tty_soft_raw(TtyState *tty_state, tcflag_t *old_lflag) {
  struct termios tio;
  if (tty_state->raw_on || tty_state->is_tty_in_error) return 0;
  if (tty_state->tty_in_fd == -1) {
    if ((tty_state->tty_in_fd = open("/dev/tty", O_RDWR)) < 0) {
      tty_state->tty_in_fd = -2;
    } else {
      tty_state->tty_in_fd = ensure_fd_is_at_least(tty_state->tty_in_fd, 5);
    }
  }
  if (tcgetattr(tty_state->tty_in_fd, &tio) != 0) {
    tty_state->is_tty_in_error = 1;
    return 0;
  }
  *old_lflag = tio.c_lflag;
  tio.c_lflag &= ~(ICANON | ECHO);  /* As a side effect, ECHOCTL is also disabled, so Ctrl-<C> won't show up as ^C, but it will still send SIGINT. */
  if (tcsetattr(tty_state->tty_in_fd, 0, &tio) != 0) { tty_state->is_tty_in_error = 1; return 0; }
  return 1;
}

static void tty_soft_raw_undo(TtyState *tty_state, int applied, tcflag_t old_lflag) {
  struct termios tio;
  if (!applied) return;
  if (tcgetattr(tty_state->tty_in_fd, &tio) == 0) {
    tio.c_lflag = old_lflag;
    if (tcsetattr(tty_state->tty_in_fd, 0, &tio) != 0) tty_state->is_tty_in_error = 1;
  }
}

/* Raw make-scancode ring for programs that poll the keyboard controller
 * ports (0x64 status / 0x60 data) directly instead of using int 16h —
 * e.g. QuickBASIC's IDE idle loop.  Fed alongside the BDA buffer. */
static unsigned char raw_scans[64];
static unsigned raw_head = 0, raw_tail = 0;

/* BIOS keycode ring: decoded words (scan<<8|ascii) for our BIOS int 9
 * handler, with host-detected modifier bits in the high half.  Kept separate
 * from the raw scancode ring because a guest int 9 ISR that reads port 0x60
 * itself and then chains to the BIOS int 9 must not let the break byte eat
 * the decoded key (real hardware delivers make and break ~50-100 ms apart,
 * in separate IRQs). */
static unsigned bios_keys[64];
static unsigned bk_head = 0, bk_tail = 0;

static void raw_push(unsigned scan) {
  const unsigned n = (raw_tail + 1) & 63;
  if (n != raw_head) { raw_scans[raw_tail] = (unsigned char)scan; raw_tail = n; }
}
int tty_raw_pending(void) { return raw_head != raw_tail; }
int tty_raw_pop(void) {
  if (raw_head == raw_tail) return -1;
  { const int s = raw_scans[raw_head]; raw_head = (raw_head + 1) & 63;
    return s; }
}

static void bk_push(unsigned key, unsigned mods) {
  const unsigned n = (bk_tail + 1) & 63;
  if (n != bk_head) { bios_keys[bk_tail] = key | (mods << 16); bk_tail = n; }
}
int tty_bk_pending(void) { return bk_head != bk_tail; }
int tty_bk_pop(unsigned *key_out, unsigned *mods_out) {
  if (bk_head == bk_tail) return -1;
  { const unsigned k = bios_keys[bk_head];
    bk_head = (bk_head + 1) & 63;
    *key_out = k & 0xffff; *mods_out = k >> 16;
    return 0; }
}

/* Poll the tty and push every available decoded key.  The scancode always
 * goes to the raw (port-0x60) ring; the BIOS word goes to the BDA buffer
 * only when bios_push — i.e. when no guest int 9 handler exists to do it
 * itself (the injected IRQ1 runs the guest ISR, which reads 0x60 and pushes
 * to the BDA, exactly like real hardware).  Returns the key count. */
int tty_drain(TtyState *tty_state, void *mem, int bios_push) {
  tcflag_t old_lflag = 0;
  int c, applied, n = 0;
  tty_ensure_raw(tty_state);
  applied = tty_soft_raw(tty_state, &old_lflag);
  while ((c = tty_getc(tty_state, 0)) >= 0) {
    const unsigned key = (unsigned)read_keycode(tty_state, c);
    /* 8042 semantics: every keypress delivers a make scancode then a break
     * scancode (0x80|make) — guest int 9 ISRs (QuickBASIC's keyboard driver)
     * consume both, treating them as one keystroke. */
    raw_push(key >> 8);
    raw_push((key >> 8) | 0x80);
    /* Guest int 9 hooked: the decoded word waits for our BIOS int 9 (the
     * ISR chains to it); otherwise push straight to the BDA buffer. */
    if (bios_push) kbd_push(mem, key, (int)tty_mods);
    else bk_push(key, tty_mods);
    ++n;
  }
  tty_soft_raw_undo(tty_state, applied, old_lflag);
  return n;
}

/* Block until a key arrives; returns its BIOS keycode plus modifier bits. */
int tty_wait_key(TtyState *tty_state, int *mods_out) {
  tcflag_t old_lflag = 0;
  int c, key, applied;
  tty_ensure_raw(tty_state);
  applied = tty_soft_raw(tty_state, &old_lflag);
  if ((c = tty_getc(tty_state, -1)) < 0) c = 26;  /* Ctrl-<Z>, simulate EOF. Most programs won't recognize it. */
  key = read_keycode(tty_state, c);
  tty_soft_raw_undo(tty_state, applied, old_lflag);
  if (mods_out) *mods_out = (int)tty_mods;
  return key;
}

void init_tty_state(TtyState *tty_state, int tty_in_fd) {
  tty_state->tty_in_fd = tty_in_fd;
  tty_state->is_tty_in_error = 0;
  tty_state->next_fake_key = fake_keys;
  tty_state->pending_key = -1;
  tty_state->raw_on = 0;
}
