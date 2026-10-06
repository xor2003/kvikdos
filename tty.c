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
  if (poll(&pfd, 1, ms) <= 0) return -1;
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
    if (b[0] < 0x80) return (scancodes[b[0]] << 8);  /* <Alt><key>: AH = scancode, AL = 0. */
    return 0x011b;
  }
  /* Parse CSI params: <Esc>[ p1 ; p2 ... <final>. Modifier is the 2nd param. */
  num = 0; mod = 0;
  for (i = 1; i < n && b[i] >= '0' && b[i] <= '9'; ++i) num = num * 10 + (b[i] - '0');
  if (i < n && b[i] == ';') {  /* Modifier param follows. */
    ++i;
    for (; i < n && b[i] >= '0' && b[i] <= '9'; ++i) mod = mod * 10 + (b[i] - '0');
    if (mod > 0) --mod;  /* xterm: param is 1 + modifier bits. */
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

void init_tty_state(TtyState *tty_state, int tty_in_fd) {
  tty_state->tty_in_fd = tty_in_fd;
  tty_state->is_tty_in_error = 0;
  tty_state->next_fake_key = fake_keys;
  tty_state->pending_key = -1;
  tty_state->raw_on = 0;
}
