#include "kvikdos.h"

/* Host-terminal key event decoding, split out of tty.c: legacy byte and
 * escape-sequence input plus kitty-keyboard CSI-u events and xterm SGR/X10
 * mouse reports.  tty_decode() returns one of:
 *   >=0        BIOS keycode word (scan<<8|ascii), a key press
 *   KEV_NONE   consumed event with no guest keycode (mouse, key release)
 *   KEV_SHIFT  modifier/lock state changed; tty_mods holds the new BDA
 *              0x417 bits — callers update the BDA or the staged queue.
 * Side effects: the raw port-0x60 scancode ring is fed here (real make on
 * press, real break on release under kitty; a synthesized make+break pair
 * for legacy encodings — 8042 semantics per the guest int 9 model), and
 * mouse_host_event() gets pointer updates. */

static const unsigned char scancodes[128] = {
    0x3e, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x0e, 0x0f, 0x24, 0x25,
    0x26, 0x1c, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11,
    0x2d, 0x15, 0x2c, 0x01, 0x2b, 0x1b, 0x07, 0x0c, 0x39, 0x02, 0x28, 0x04,
    0x05, 0x06, 0x08, 0x28, 0x0a, 0x0b, 0x09, 0x0d, 0x33, 0x0c, 0x34, 0x35,
    0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x27, 0x27,
    0x33, 0x0d, 0x34, 0x35, 0x03, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22,
    0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f,
    0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x1a, 0x2b, 0x1b, 0x07, 0x0c,
    0x29, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25,
    0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11,
    0x2d, 0x15, 0x2c, 0x1a, 0x2b, 0x1b, 0x29, 0x35 };

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

static int apply_mod(int k, int mod) {
  int slot = 0;  /* Prefer alt, then ctrl, then shift, for single-mod combos. */
  if (mod & 2) slot = 2; else if (mod & 4) slot = 3; else if (mod & 1) slot = 1;
  if (keymods[k][slot]) return keymods[k][slot];
  return keymods[k][0];
}

unsigned tty_mods = 0;  /* BDA 0x417-format bits for the last decoded event. */

static int tty_ins = 0;  /* Insert-mode toggle (BDA 0x417 bit7), flipped on each Ins press. */

static unsigned rdnum(const unsigned char *b, int n, int *pi) {
  unsigned v = 0;
  while (*pi < n && b[*pi] >= '0' && b[*pi] <= '9') v = v * 10 + ((unsigned)b[(*pi)++] - '0');
  return v;
}

static void raw_pair(unsigned key) {  /* Synthesized make+break for legacy encodings. */
  tty_raw_push(key >> 8);
  tty_raw_push((key >> 8) | 0x80);
}

/* E0-prefixed make/break: on a real AT keyboard the dedicated editing and
 * navigation keys (the six-pack plus arrows, keypad / and keypad Enter)
 * carry an E0 prefix that distinguishes them from their numpad twins —
 * guest int 9 handlers rely on it (plain 0x53 is numpad-Del, E0 0x53 is
 * the editing-cluster Delete). */
static void raw_make_e(unsigned scan, int e0) {
  if (e0) tty_raw_push(0xe0);
  tty_raw_push(scan);
}
static void raw_break_e(unsigned scan, int e0) {
  if (e0) tty_raw_push(0xe0);
  tty_raw_push(scan | 0x80);
}
static void raw_pair_e(unsigned key, int e0) {
  raw_make_e(key >> 8, e0);
  raw_break_e(key >> 8, e0);
}

/* US-layout shifted chars, used only when the terminal reports neither the
 * shifted alternate key (kitty flag 4) nor the text (flag 16). */
static unsigned shift_map(unsigned key) {
  switch (key) {
   case '1': return '!'; case '2': return '@'; case '3': return '#';
   case '4': return '$'; case '5': return '%'; case '6': return '^';
   case '7': return '&'; case '8': return '*'; case '9': return '(';
   case '0': return ')'; case '-': return '_'; case '=': return '+';
   case '[': return '{'; case ']': return '}'; case '\\': return '|';
   case ';': return ':'; case '\'': return '"'; case '`': return '~';
   case ',': return '<'; case '.': return '>'; case '/': return '?';
  }
  return key;
}

/* Kitty functional key codes (PUA) to AT scancodes.  -1 = unmapped. */
static int kitty_scan(unsigned key) {
  static const unsigned char kp_scans[28] = {  /* 57399 KP_0 .. 57426 KP_DELETE */
      0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49,
      0x53, 0x35, 0x37, 0x4a, 0x4e, 0x1c, 0x0d, 0x00,
      0x4b, 0x4d, 0x48, 0x50, 0x49, 0x51, 0x47, 0x4f, 0x52, 0x53 };
  if (key - 57399U <= 27U) return kp_scans[key - 57399];
  if (key == 57427) return 0x4c;  /* KP_BEGIN */
  if (key - 57364U <= 9U) return key - 57364 + 0x3b;  /* F1..F10 */
  switch (key) {
   case 57344: return 0x01;  /* ESCAPE */
   case 57345: return 0x1c;  /* ENTER */
   case 57346: return 0x0f;  /* TAB */
   case 57347: return 0x0e;  /* BACKSPACE */
   case 57348: return 0x52;  /* INSERT */
   case 57349: return 0x53;  /* DELETE */
   case 57350: return 0x4b;  /* LEFT */
   case 57351: return 0x4d;  /* RIGHT */
   case 57352: return 0x48;  /* UP */
   case 57353: return 0x50;  /* DOWN */
   case 57354: return 0x49;  /* PAGE_UP */
   case 57355: return 0x51;  /* PAGE_DOWN */
   case 57356: return 0x47;  /* HOME */
   case 57357: return 0x4f;  /* END */
   case 57358: return 0x3a;  /* CAPS_LOCK */
   case 57359: return 0x46;  /* SCROLL_LOCK */
   case 57360: return 0x45;  /* NUM_LOCK */
   case 57374: return 0x57;  /* F11 */
   case 57375: return 0x58;  /* F12 */
   case 57441: return 0x2a;  /* LEFT_SHIFT */
   case 57442: case 57448: return 0x1d;  /* CTRL */
   case 57443: case 57449: case 57453: return 0x38;  /* ALT (+ ISO Level3). */
   case 57447: return 0x36;  /* RIGHT_SHIFT */
  }
  return -1;
}

/* Nonzero when the key's hardware scancode carries an E0 prefix: the
 * dedicated editing/navigation keys (57348 INSERT .. 57357 END), keypad /
 * and keypad Enter. */
static int kitty_e0(unsigned key) {
  return (key - 57348U <= 9U) || key == 57410U || key == 57414U;
}

/* kitty key -> keymods[] index for the BIOS keycode word (with shift/ctrl/
 * alt variants), or -1 when not an editing/navigation/F key.  Keypad
 * digits count as navigation keys only when the effective Num Lock state
 * (kitty bit 128, inverted by shift like the real BIOS) is off. */
static int kitty_km_idx(unsigned key, unsigned mods) {
  static const signed char kp_nav[11] = {8, 5, 1, 7, 3, -1, 2, 4, 0, 6, 9};  /* KP_0..KP_9, KP_PERIOD */
  switch (key) {
   case 57352: case 57419: return 0;  /* UP */
   case 57353: case 57420: return 1;  /* DOWN */
   case 57351: case 57418: return 2;  /* RIGHT */
   case 57350: case 57417: return 3;  /* LEFT */
   case 57356: case 57423: return 4;  /* HOME */
   case 57357: case 57424: return 5;  /* END */
   case 57354: case 57421: return 6;  /* PAGE_UP */
   case 57355: case 57422: return 7;  /* PAGE_DOWN */
   case 57348: case 57425: return 8;  /* INSERT */
   case 57349: case 57426: return 9;  /* DELETE */
  }
  if (key - 57399U <= 10U) {  /* KP_0..KP_9 + KP_DECIMAL: digit vs nav. */
    int nav = !(mods & 128);
    if (mods & 1) nav = !nav;  /* Shift temporarily inverts Num Lock. */
    return nav ? kp_nav[key - 57399] : -1;
  }
  if (key - 57364U <= 11U) return (int)(key - 57364) + 10;  /* F1..F12 */
  return -1;
}

/* Default ASCII for keypad codes (numlock-on view); the terminal's own text
 * field overrides when present. */
static unsigned kp_ascii(unsigned key) {
  if (key - 57399U <= 9U) return '0' + (key - 57399);  /* KP_0..KP_9 */
  switch (key) {
   case 57409: case 57426: return '.';
   case 57410: return '/'; case 57411: return '*';
   case 57412: return '-'; case 57413: return '+';
   case 57414: return 0x0d; case 57415: return '=';
   case 57417: return '4'; case 57418: return '6';
   case 57419: return '8'; case 57420: return '2';
   case 57421: return '9'; case 57422: return '3';
   case 57423: return '7'; case 57424: return '1';
   case 57425: return '0'; case 57427: return '5';
  }
  return 0;
}

/* CSI key[:alts];mod[:ev][;text]u — kitty keyboard protocol.  Key-release
 * events produce only a break scancode; modifier-only presses produce no
 * keycode, just a shift-state event (what bare Alt needs to reach a guest
 * int 9 handler, e.g. QuickBASIC menu activation). */
static int kitty_event(const unsigned char *b, int n) {
  int i = 1, mods, scan;
  unsigned key, modf, ev = 1, text = 0, shifted = 0;
  key = rdnum(b, n, &i);
  if (i < n && b[i] == ':') { ++i; shifted = rdnum(b, n, &i);
    if (i < n && b[i] == ':') { ++i; (void)rdnum(b, n, &i); } }  /* base-layout key: unused. */
  modf = 1;
  if (i < n && b[i] == ';') {
    ++i; modf = rdnum(b, n, &i); if (!modf) modf = 1;
    if (i < n && b[i] == ':') { ++i; ev = rdnum(b, n, &i); if (!ev) ev = 1; }
    if (i < n && b[i] == ';') { ++i; text = rdnum(b, n, &i); }  /* First text codepoint only. */
  }
  if (i >= n || b[i] != 'u' || !key) return KEV_NONE;
  mods = (int)modf - 1;
  /* The modifier field is the post-event snapshot (kitty: bits 1=shift 2=alt
   * 4=ctrl 64=caps 128=num).  BDA 0x417: bits0-1 shift, 2 ctrl, 3 alt,
   * 5 num, 6 caps, 7 ins. */
  tty_mods = (unsigned)tty_ins | ((mods & 1) ? 3U : 0U) | ((mods & 2) ? 8U : 0U) |
      ((mods & 4) ? 4U : 0U) | ((mods & 64) ? 0x40U : 0U) | ((mods & 128) ? 0x20U : 0U);
  if (key - 57441U <= 57454U - 57441U || (key >= 57358 && key <= 57360)) {
    /* Modifier/lock-only key: no BIOS keycode, but the make/break scancode
     * and the 0x417 change are exactly what a guest int 9 ISR watches for. */
    scan = kitty_scan(key);
    if (scan >= 0) tty_raw_push((unsigned)(scan | (ev == 3 ? 0x80 : 0)));
    return KEV_SHIFT;
  }
  scan = key < 128 ? (key == 127 ? 0x0e : scancodes[key]) : kitty_scan(key);
  if (scan < 0) return KEV_NONE;
  {
    const int e0 = kitty_e0(key);
    if (ev == 3) {  /* Release: break scancode only — the BIOS never buffers releases. */
      raw_break_e((unsigned)scan, e0);
      return KEV_NONE;
    }
    {
      unsigned word;
      const int km = kitty_km_idx(key, (unsigned)mods);
      if (km >= 0) {
        word = (unsigned)apply_mod(km, mods);  /* Editing/nav/F keys, incl. shifted/ctrl variants. */
      } else if (key == 57344) {
        word = 0x011b;
      } else if (key == 57345) {
        word = (mods & 4) ? 0x1c0a : 0x1c0d;   /* Enter (Ctrl = LF). */
      } else if (key == 57346) {
        word = (mods & 1) ? 0x0f00 : 0x0f09;   /* Tab / Shift-Tab. */
      } else if (key == 57347) {
        word = 0x0e08;                          /* Backspace. */
      } else {
        unsigned ascii;
        if (text && text < 128) ascii = text;
        else if (shifted && shifted < 128 && (mods & 1)) ascii = shifted;
        else if (key < 128) {
          ascii = key;
          if (key == 13) ascii = (mods & 4) ? 0x0a : 0x0d;         /* Ctrl-Enter = LF per BIOS. */
          else if (key == 127) ascii = 8;
          else if (key == 32 && (mods & 4)) ascii = 0;             /* Ctrl-Space = NUL. */
          else if (mods & 2) ascii = 0;                            /* Alt: BIOS gives scan<<8|0. */
          else if (mods & 4) ascii = key & 0x1f;                   /* Ctrl: control byte. */
          else if (mods & 1) ascii = key >= 'a' && key <= 'z' ? key - 32 : shift_map(key);
        } else {
          ascii = kp_ascii(key);
        }
        word = ((unsigned)scan << 8) | ascii;
      }
      if (key == 57348 && ev != 2) tty_ins ^= 0x80;  /* Ins press toggles BDA bit7. */
      raw_make_e((unsigned)scan, e0);
      return (int)word;
    }
  }
}

/* xterm button number -> DOS button index (0=L, 1=R, 2=M). */
static const unsigned char xt2dos_btn[4] = {0, 2, 1, 0};

/* ESC[<btn;x;yM  (press/motion) / ESC[<btn;x;ym  (release) — SGR mouse. */
static int sgr_mouse(const unsigned char *b, int n) {
  int i = 2;
  unsigned btn, x, y;
  char fin;
  btn = rdnum(b, n, &i);
  if (i >= n || b[i] != ';') return KEV_NONE;
  ++i; x = rdnum(b, n, &i);
  if (i >= n || b[i] != ';') return KEV_NONE;
  ++i; y = rdnum(b, n, &i);
  fin = i < n ? (char)b[i] : 0;
  if (fin != 'M' && fin != 'm') return KEV_NONE;
  if (btn & 64) {  /* Wheel: position still counts, no button semantics. */
    mouse_host_event((int)(x - 1) * 8, (int)(y - 1) * 8, -1, 0);
  } else if (fin == 'm') {
    mouse_host_event((int)(x - 1) * 8, (int)(y - 1) * 8, xt2dos_btn[btn & 3], 2);
  } else if (btn & 32) {  /* Motion (drag or hover). */
    mouse_host_event((int)(x - 1) * 8, (int)(y - 1) * 8, -1, 0);
  } else if ((btn & 3) != 3) {
    mouse_host_event((int)(x - 1) * 8, (int)(y - 1) * 8, xt2dos_btn[btn & 3], 1);
  }
  return KEV_NONE;
}

/* ESC[M btn x y — X10 mouse (3 raw bytes follow, coords offset by 32). */
static int x10_mouse(TtyState *tty_state) {
  int i, v[3], b0, x, y;
  for (i = 0; i < 3; ++i) {
    const int t = tty_getc(tty_state, 25);
    if (t < 0) return KEV_NONE;
    v[i] = t;
  }
  b0 = v[0] - 32; x = v[1] - 32; y = v[2] - 32;
  if (b0 & 64) { mouse_host_event((x - 1) * 8, (y - 1) * 8, -1, 0); }
  else if ((b0 & 3) == 3) { mouse_host_event((x - 1) * 8, (y - 1) * 8, -1, 2); }  /* Release-all. */
  else if (!(b0 & 32)) { mouse_host_event((x - 1) * 8, (y - 1) * 8, xt2dos_btn[b0 & 3], 1); }
  else { mouse_host_event((x - 1) * 8, (y - 1) * 8, -1, 0); }
  return KEV_NONE;
}

/* Legacy CSI / SS3 body (no kitty 'u' final): CSI num[;mod[:ev]]<final>. */
static int decode_csi(const unsigned char *b, int n) {
  int i = 1, num, mod = 0, ev = 0, word = 0x011b, e0 = 0;
  num = (int)rdnum(b, n, &i);
  if (i < n && b[i] == ';') {  /* Modifier param, optional :event-type subfield. */
    ++i; mod = (int)rdnum(b, n, &i);
    if (mod > 0) --mod;  /* xterm: param is 1 + modifier bits. */
    if (i < n && b[i] == ':') { ++i; ev = (int)rdnum(b, n, &i); }
    /* xterm modifier bits: 1=Shift 2=Alt 4=Ctrl -> BDA 0x417 bits:
     * Shift (bits 0+1), Ctrl (bit2), Alt (bit3). */
    if (mod & 1) tty_mods |= 3;
    if (mod & 2) tty_mods |= 8;
    if (mod & 4) tty_mods |= 4;
  }
  if (i < n && b[i] >= 'A' && b[i] <= 'Z' && b[i] != '~') {  /* CSI [p1;mod]<letter>. */
    switch (b[i]) {
     case 'A': word = apply_mod(K_UP, mod); e0 = 1; break;
     case 'B': word = apply_mod(K_DOWN, mod); e0 = 1; break;
     case 'C': word = apply_mod(K_RIGHT, mod); e0 = 1; break;
     case 'D': word = apply_mod(K_LEFT, mod); e0 = 1; break;
     case 'H': word = apply_mod(K_HOME, mod); e0 = 1; break;
     case 'F': word = apply_mod(K_END, mod); e0 = 1; break;
     case 'Z': word = 0x0f09; break;  /* <Shift><Tab>. */
     case 'P': word = apply_mod(K_F1, mod); break;
     case 'Q': word = apply_mod(K_F2, mod); break;
     case 'R': word = apply_mod(K_F3, mod); break;
     case 'S': word = apply_mod(K_F4, mod); break;
    }
  } else if (i < n && b[i] == '~') {  /* CSI <num>[;mod[:ev]]~ */
    switch (num) {
     case 1: case 7: word = apply_mod(K_HOME, mod); e0 = 1; break;
     case 4: case 8: word = apply_mod(K_END, mod); e0 = 1; break;
     case 2: word = apply_mod(K_INS, mod); e0 = 1; break;
     case 3: word = apply_mod(K_DEL, mod); e0 = 1; break;
     case 5: word = apply_mod(K_PGUP, mod); e0 = 1; break;
     case 6: word = apply_mod(K_PGDN, mod); e0 = 1; break;
     case 11: case 12: case 13: case 14: word = apply_mod(K_F1 + num - 11, mod); break;  /* <F1>..<F4> */
     case 15: word = apply_mod(K_F5, mod); break;
     case 17: case 18: case 19: case 20: case 21: word = apply_mod(K_F6 + num - 17, mod); break;  /* <F6>..<F10> */
     case 23: word = apply_mod(K_F11, mod); break;
     case 24: word = apply_mod(K_F12, mod); break;
    }
  }
  if (ev == 3) {  /* Release (kitty event type): break scancode only. */
    raw_break_e((unsigned)(word >> 8), e0);
    return KEV_NONE;
  }
  if (num == 2 && ev != 2 && i < n && b[i] == '~') tty_ins ^= 0x80;  /* Ins press toggles BDA bit7. */
  if (ev) raw_make_e((unsigned)(word >> 8), e0);  /* kitty press/repeat: real make, break on release. */
  else raw_pair_e((unsigned)word, e0);  /* Legacy: synthesized make+break pair. */
  return word;
}

int tty_decode(TtyState *tty_state, int c) {
  unsigned char b[24];
  int n = 0, t, word;
  tty_mods = (unsigned)tty_ins;
  if (c != 0x1b) {
    const unsigned w =
        c == 0x7f ? 0x0e08u  /* <Backspace>: terminals send DEL, not BS. */
                  : (((unsigned)(c & ~0x7f ? 0x3f : scancodes[c]) << 8) | (unsigned)(c & 0xff));
    raw_pair(w);
    return (int)w;
  }
  for (;;) {  /* Read the escape tail: SS3 is 2 bytes; CSI ends at a 0x40..0x7e final byte. */
    if (n == (int)sizeof(b)) break;
    t = tty_getc(tty_state, n ? 6 : 15);  /* The first tail byte gets a bit longer. */
    if (t < 0) break;
    b[n++] = (unsigned char)t;
    if (b[0] == 'O') { if (n >= 2) break; }
    else if (b[0] == '[') { if (n >= 2 && b[n - 1] >= 0x40) break; }
    else break;  /* ESC+<char> = <Alt><char>: a single tail byte. */
  }
  if (n == 0 || b[0] == 0x1b) { raw_pair(0x011b); return 0x011b; }  /* Bare <Esc>. */
  if (b[0] == 'O') {  /* SS3 (application-keypad) sequence. */
    switch (n >= 2 ? b[1] : 0) {
     case 'P': word = 0x3b00; break; case 'Q': word = 0x3c00; break;
     case 'R': word = 0x3d00; break; case 'S': word = 0x3e00; break;  /* <F1>..<F4> */
     case 'A': raw_pair_e(0x4800, 1); return 0x4800;  /* <Up> */
     case 'B': raw_pair_e(0x5000, 1); return 0x5000;  /* <Down> */
     case 'C': raw_pair_e(0x4d00, 1); return 0x4d00;  /* <Right> */
     case 'D': raw_pair_e(0x4b00, 1); return 0x4b00;  /* <Left> */
     case 'H': raw_pair_e(0x4700, 1); return 0x4700;  /* <Home> */
     case 'F': raw_pair_e(0x4f00, 1); return 0x4f00;  /* <End> */
     default: word = 0x011b; break;
    }
    raw_pair((unsigned)word);
    return word;
  }
  if (b[0] != '[') {  /* <Esc><char> = <Alt><char>. */
    if (b[0] < 0x80) {
      tty_mods |= 8;
      word = scancodes[b[0]] << 8;  /* <Alt><key>: AH = scancode, AL = 0. */
      raw_pair((unsigned)word);
      return word;
    }
    raw_pair(0x011b);
    return 0x011b;
  }
  /* CSI body. */
  if (n >= 2 && b[1] == '<') return sgr_mouse(b, n);
  if (n == 2 && b[1] == 'M') return x10_mouse(tty_state);
  if (b[n - 1] == 'u') return kitty_event(b, n);
  return decode_csi(b, n);
}
