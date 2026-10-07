#include "kvikdos.h"
#include "intrun.h"

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

/* Consume one decoded event that is not a guest keycode (tty_decode()
 * returns KEV_*): modifier/lock transitions update the BDA shift byte so
 * int 16h AH=02h and direct 0x417 readers see them; mouse and key-release
 * events need no further work here. */
static void tty_nonkey(int key) {
  if (key == KEV_SHIFT && mem) ((unsigned char*)mem)[0x417] = (unsigned char)tty_mods;
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
        if (tcsetattr(fd, 0, &rt) == 0) { vid_tty_fd = fd; vid_raw_taken = 1; tty_state->raw_on = 1; vid_install_release(); }
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
        else {
          /* Publish the transient raw state so the signal/exit release
           * restores it if we die while blocked on a key. */
          applied = 1;
          tio.c_lflag = old_lflag;
          vid_saved_tio = tio;
          vid_tty_fd = tty_state->tty_in_fd;
          vid_raw_taken = 1;
          vid_install_release();
        }
      }
    }
    if (tty_state->pending_key >= 0) {
      *ax = (unsigned short)tty_state->pending_key;
      if (ah & 1) *flags &= ~(1 << 6);  /* Key-check: report it, keep it buffered. */
      else tty_state->pending_key = -1;  /* Read: consume it. */
    } else if (ah & 1) {  /* Check for a key without removing it. */
      int c = tty_getc(tty_state, 0);
      key = -1;
      while (c >= 0 && (key = tty_decode(tty_state, c)) < 0) {  /* Skip mouse/shift/release events. */
        tty_nonkey(key);
        c = tty_getc(tty_state, 0);
      }
      if (key < 0) {
        *flags |= (1 << 6);  /* ZF=1, no key. */
      } else {
        tty_state->pending_key = key;
        *ax = (unsigned short)key;
        *flags &= ~(1 << 6);  /* ZF=0, key available. */
      }
    } else {  /* Wait for and read a key. */
      int c, k;
      for (;;) {
        if ((c = tty_getc(tty_state, -1)) < 0) c = 26;  /* Ctrl-<Z>, simulate EOF. Most programs won't recognize it. */
        k = tty_decode(tty_state, c);
        if (k >= 0) break;
        tty_nonkey(k);
      }
      *ax = (unsigned short)k;
    }
    /* Since we set ECHO back here, a call with ah == 0x01 followed by a call
     * with ah == 0x00 will echo the character to the Linux terminal. */
    if (applied) {  /* Restore the saved line flags for the non-text-mode path. */
      struct termios tio;
      if (tcgetattr(tty_state->tty_in_fd, &tio) == 0) {
        tio.c_lflag = old_lflag;
        if (tcsetattr(tty_state->tty_in_fd, 0, &tio) != 0) tty_state->is_tty_in_error = 1;
      }
      vid_raw_taken = 0;
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
  if (mods >= 0) b[0x417] = (unsigned char)mods;  /* Full byte: mods now carries lock bits (caps/num/ins) too. */
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
    if (tcsetattr(fd, 0, &rt) == 0) { vid_tty_fd = fd; vid_raw_taken = 1; tty_state->raw_on = 1; vid_install_release(); }
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
  /* Publish the transient raw state so the signal/exit release restores it
   * if we die while blocked on a key. */
  tio.c_lflag = *old_lflag;
  vid_saved_tio = tio;
  vid_tty_fd = tty_state->tty_in_fd;
  vid_raw_taken = 1;
  vid_install_release();
  return 1;
}

static void tty_soft_raw_undo(TtyState *tty_state, int applied, tcflag_t old_lflag) {
  struct termios tio;
  if (!applied) return;
  if (tcgetattr(tty_state->tty_in_fd, &tio) == 0) {
    tio.c_lflag = old_lflag;
    if (tcsetattr(tty_state->tty_in_fd, 0, &tio) != 0) tty_state->is_tty_in_error = 1;
  }
  vid_raw_taken = 0;
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

void tty_raw_push(unsigned scan) {
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

/* Poll the tty and push every available decoded key.  The scancode side of
 * every event (real or synthesized make/break) is fed to the raw port-0x60
 * ring inside tty_decode; the BIOS word goes to the BDA buffer only when
 * bios_push — i.e. when no guest int 9 handler exists to do it itself (the
 * injected IRQ1 runs the guest ISR, which reads 0x60 and pushes to the BDA,
 * exactly like real hardware).  Returns the key count. */
int tty_drain(TtyState *tty_state, void *mem, int bios_push) {
  tcflag_t old_lflag = 0;
  int c, applied, n = 0;
  tty_ensure_raw(tty_state);
  applied = tty_soft_raw(tty_state, &old_lflag);
  while ((c = tty_getc(tty_state, 0)) >= 0) {
    const int key = tty_decode(tty_state, c);
    if (key < 0) {  /* Mouse/shift/release: no keycode for the buffer. */
      tty_nonkey(key);
      continue;
    }
    /* Guest int 9 hooked: the decoded word waits for our BIOS int 9 (the
     * ISR chains to it); otherwise push straight to the BDA buffer. */
    if (bios_push) kbd_push(mem, (unsigned)key, (int)tty_mods);
    else bk_push((unsigned)key, tty_mods);
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
  for (;;) {
    if ((c = tty_getc(tty_state, -1)) < 0) c = 26;  /* Ctrl-<Z>, simulate EOF. Most programs won't recognize it. */
    key = tty_decode(tty_state, c);
    if (key >= 0) break;
    tty_nonkey(key);
  }
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
