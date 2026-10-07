#include "kvikdos.h"
#include "intrun.h"

/* int 33h mouse driver, function surface borrowed from vDos (src/ints/
 * mouse.cpp): a pollable two-button text-mode mouse.  Host pointer events
 * arrive from ttydec.c (SGR/X10 reports decoded on the tty input stream) in
 * text-cell coordinates scaled to 8x8-pixel cells.  The driver also draws
 * its own cursor like the real one does: the cell under the pointer is
 * emitted with `(word & and_mask) ^ xor_mask` in vid_render() — the classic
 * color-inverse text cursor (default masks 0x77ff/0x7700). */

typedef struct MouseState {
  unsigned short x, y;          /* Pixel position (text cells are 8x8 px). */
  unsigned short minx, maxx, miny, maxy;
  short mickey_x, mickey_y;
  unsigned char buttons;        /* bit0 L, bit1 R, bit2 M. */
  short hidden;                 /* Show/hide counter; drawn when 0. */
  short oldhidden;
  char enabled;
  unsigned char page;
  unsigned short and_mask, xor_mask;
  unsigned short press_cnt[3], rel_cnt[3];
  unsigned short press_x[3], press_y[3], rel_x[3], rel_y[3];
  unsigned short sub_mask, sub_seg, sub_ofs;  /* User callback (AX=0x0c). */
  /* Event ring for the user callback.  Masks use the RBIL encoding:
   * bit0 move, 1/2 L press/release, 3/4 R, 5/6 M.  Delivery is serialized
   * by in_call: the far-call return lands on a trampoline hlt in the int
   * stub page, and only then is the next event injected. */
  unsigned char ev_mask[16];
  unsigned char ev_btns[16];
  unsigned short ev_x[16], ev_y[16];
  unsigned char ev_head, ev_cnt;
  unsigned char in_call;
  unsigned short resume_cs, resume_ip;
} MouseState;

static MouseState ms;

void mouse_reset(void) {
  memset(&ms, '\0', sizeof(ms));
  ms.maxx = VID_COLS * 8 - 1;
  ms.maxy = VID_ROWS * 8 - 1;
  ms.x = ms.maxx >> 1;
  ms.y = ms.maxy >> 1;
  ms.hidden = 1;       /* Real drivers start hidden until AX=1. */
  ms.enabled = 1;
  ms.and_mask = 0x77ff;
  ms.xor_mask = 0x7700;
}

/* Host-side event: px/py are pixel coords, evbtn the DOS button index
 * (0=L,1=R,2=M) or -1, evkind 0=motion 1=press 2=release. */
void mouse_host_event(int px, int py, int evbtn, int evkind) {
  unsigned short evmask = 0;
  if (px < ms.minx) px = ms.minx; else if (px > ms.maxx) px = ms.maxx;
  if (py < ms.miny) py = ms.miny; else if (py > ms.maxy) py = ms.maxy;
  if (px != ms.x || py != ms.y) evmask = 0x01;
  ms.mickey_x += (short)(px - ms.x);
  ms.mickey_y += (short)((py - ms.y) * 2);  /* vDos doubles text-mode Y mickeys. */
  ms.x = (unsigned short)px;
  ms.y = (unsigned short)py;
  if (evkind == 1 && (unsigned)evbtn < 3U) {
    ms.buttons |= (unsigned char)(1 << evbtn);
    ++ms.press_cnt[evbtn];
    ms.press_x[evbtn] = ms.x; ms.press_y[evbtn] = ms.y;
    evmask |= (unsigned short)(2 << (evbtn * 2));   /* 0x02/0x08/0x20. */
  } else if (evkind == 2) {
    int k;
    for (k = 0; k < 3; ++k) {
      if (evbtn >= 0 && k != evbtn) continue;  /* evbtn<0 = release-all (X10). */
      if (ms.buttons & (1 << k)) {
        ms.buttons &= (unsigned char)~(1 << k);
        ++ms.rel_cnt[k];
        ms.rel_x[k] = ms.x; ms.rel_y[k] = ms.y;
        evmask |= (unsigned short)(4 << (k * 2));  /* 0x04/0x10/0x40. */
      }
    }
  }
  if ((evmask & ms.sub_mask) && ms.ev_cnt < sizeof(ms.ev_mask)) {
    const unsigned tail = (ms.ev_head + ms.ev_cnt) & (sizeof(ms.ev_mask) - 1);
    ms.ev_mask[tail] = (unsigned char)evmask;
    ms.ev_btns[tail] = ms.buttons;
    ms.ev_x[tail] = ms.x; ms.ev_y[tail] = ms.y;
    ++ms.ev_cnt;
  }
}

/* Deliver a queued event to the guest's AX=0x0c handler.  The handler is
 * invoked with a far CALL (RETF return, per the MS/RBIL convention — see
 * QuickBASIC's handler, which ends `retf'): the pushed return address is a
 * hlt trampoline in the int stub page, so control returns to us when the
 * handler finishes.  Mutates the shared regs/sregs; the run loop pushes
 * them to the vCPU before the next hv_run, exactly like kbd_maybe_inject_irq. */
void mouse_maybe_call_handler(void) {
  unsigned short nsp;
  unsigned short *stk;
  if (ms.in_call || !ms.sub_mask || !ms.ev_cnt) return;
  if (!(regs.rflags & (1u << 9))) return;   /* IF=0: don't preempt the guest. */
  if (sregs.cr0 & 1) return;                /* PM handlers need a different frame. */
  if (pic_isr & 2) return;                  /* Don't nest inside an int 9 ISR. */
  nsp = (unsigned short)(regs.rsp - 4);
  stk = (unsigned short*)((char*)mem + ((sregs.ss.base + nsp) & 0xfffff));
  stk[0] = 0x100;            /* Trampoline offset in the int stub page. */
  stk[1] = INT_HLT_PARA;
  *(unsigned short*)&regs.rsp = nsp;
  ms.resume_cs = sregs.cs.selector;
  ms.resume_ip = (unsigned short)regs.rip;
  SET_SREG(cs, ms.sub_seg);
  *(unsigned short*)&regs.rip = ms.sub_ofs;
  *(unsigned short*)&regs.rax = ms.ev_mask[ms.ev_head];
  *(unsigned short*)&regs.rbx = ms.ev_btns[ms.ev_head];
  *(unsigned short*)&regs.rcx = ms.ev_x[ms.ev_head];
  *(unsigned short*)&regs.rdx = ms.ev_y[ms.ev_head];
  *(unsigned short*)&regs.rsi = (unsigned short)ms.mickey_x;
  *(unsigned short*)&regs.rdi = (unsigned short)ms.mickey_y;
  ms.mickey_x = ms.mickey_y = 0;
  ms.ev_head = (unsigned char)((ms.ev_head + 1) & (sizeof(ms.ev_mask) - 1));
  --ms.ev_cnt;
  ms.in_call = 1;
}

/* The handler's RETF landed on the trampoline hlt: resume the interrupted
 * guest code and free the delivery slot for the next queued event. */
void mouse_cbk_return(void) {
  SET_SREG(cs, ms.resume_cs);
  regs.rip = ms.resume_ip;
  ms.in_call = 0;
}

/* Screen-cell index of the software cursor for vid_render(), or -1 when the
 * driver cursor is hidden/disabled. */
int mouse_cursor_cell(void) {
  if (!ms.enabled || ms.hidden > 0) return -1;
  return (ms.y >> 3) * VID_COLS + (ms.x >> 3);
}

unsigned short mouse_cursor_and(void) { return ms.and_mask; }
unsigned short mouse_cursor_xor(void) { return ms.xor_mask; }

int int33_dispatch(void) {
  const unsigned short ax = *(unsigned short*)&regs.rax;
  if (getenv("KD_MOUSE_TRACE")) fprintf(stderr, "int33 ax=%04x bx=%04x cx=%04x dx=%04x es=%04x -> btns=%d x=%d y=%d ev=%u\n",
      ax, *(unsigned short*)&regs.rbx, *(unsigned short*)&regs.rcx, *(unsigned short*)&regs.rdx,
      sregs.es.selector, ms.buttons, ms.x, ms.y, ms.ev_cnt);
  switch (ax) {
   case 0x00:   /* Reset driver and read status. */
   case 0x21:   /* Software reset. */
    mouse_reset();
    *(unsigned short*)&regs.rax = 0xffff;   /* Driver installed. */
    *(unsigned short*)&regs.rbx = 2;        /* Two buttons. */
    break;
   case 0x01:   /* Show mouse. */
    if (ms.hidden > 0) --ms.hidden;
    break;
   case 0x02:   /* Hide mouse. */
    ++ms.hidden;
    break;
   case 0x03:   /* Return position and button status. */
    *(unsigned short*)&regs.rbx = ms.buttons;
    *(unsigned short*)&regs.rcx = ms.x & ~7;  /* Text-mode granularity (vDos granMask). */
    *(unsigned short*)&regs.rdx = ms.y;
    break;
   case 0x04:   /* Position mouse. */
    ms.x = *(unsigned short*)&regs.rcx;
    ms.y = *(unsigned short*)&regs.rdx;
    if (ms.x < ms.minx) ms.x = ms.minx; else if (ms.x > ms.maxx) ms.x = ms.maxx;
    if (ms.y < ms.miny) ms.y = ms.miny; else if (ms.y > ms.maxy) ms.y = ms.maxy;
    break;
   case 0x05:   /* Return button press data. */
   case 0x06: { /* Return button release data. */
    unsigned but = *(unsigned short*)&regs.rbx;
    if (but > 2) but = 2;
    *(unsigned short*)&regs.rax = ms.buttons;
    if (ax == 0x05) {
      *(unsigned short*)&regs.rcx = ms.press_x[but];
      *(unsigned short*)&regs.rdx = ms.press_y[but];
      *(unsigned short*)&regs.rbx = ms.press_cnt[but];
      ms.press_cnt[but] = 0;
    } else {
      *(unsigned short*)&regs.rcx = ms.rel_x[but];
      *(unsigned short*)&regs.rdx = ms.rel_y[but];
      *(unsigned short*)&regs.rbx = ms.rel_cnt[but];
      ms.rel_cnt[but] = 0;
    }
    break; }
   case 0x07:   /* Define horizontal cursor range. */
    ms.minx = *(unsigned short*)&regs.rcx;
    ms.maxx = *(unsigned short*)&regs.rdx;
    if (ms.x < ms.minx) ms.x = ms.minx; else if (ms.x > ms.maxx) ms.x = ms.maxx;
    break;
   case 0x08:   /* Define vertical cursor range. */
    ms.miny = *(unsigned short*)&regs.rcx;
    ms.maxy = *(unsigned short*)&regs.rdx;
    if (ms.y < ms.miny) ms.y = ms.miny; else if (ms.y > ms.maxy) ms.y = ms.maxy;
    break;
   case 0x0a:   /* Define text cursor (software cursor masks). */
    ms.and_mask = *(unsigned short*)&regs.rcx;
    ms.xor_mask = *(unsigned short*)&regs.rdx;
    break;
   case 0x0b:   /* Read motion counters (mickeys). */
    *(unsigned short*)&regs.rcx = (unsigned short)ms.mickey_x;
    *(unsigned short*)&regs.rdx = (unsigned short)ms.mickey_y;
    ms.mickey_x = ms.mickey_y = 0;
    break;
   case 0x0c:   /* Define event subroutine (far-called on queued events). */
    ms.sub_mask = *(unsigned short*)&regs.rcx;
    ms.sub_seg = sregs.es.selector;
    ms.sub_ofs = *(unsigned short*)&regs.rdx;
    break;
   case 0x14: { /* Exchange event subroutine. */
    const unsigned short om = ms.sub_mask, os = ms.sub_seg, oo = ms.sub_ofs;
    ms.sub_mask = *(unsigned short*)&regs.rcx;
    ms.sub_seg = sregs.es.selector;
    ms.sub_ofs = *(unsigned short*)&regs.rdx;
    *(unsigned short*)&regs.rcx = om;
    *(unsigned short*)&regs.rdx = oo;
    sregs.es.selector = os;
    break; }
   case 0x15:   /* Get driver state size. */
    *(unsigned short*)&regs.rbx = sizeof(ms);
    break;
   case 0x16:   /* Save driver state to ES:DX. */
   case 0x17: { /* Load driver state from ES:DX. */
    void * const p = guest_ptr(emu, ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdx));
    if (!p) break;
    if (ax == 0x16) memcpy(p, &ms, sizeof(ms));
    else memcpy(&ms, p, sizeof(ms));
    break; }
   case 0x1f:   /* Disable mouse driver. */
    *(unsigned short*)&regs.rbx = 0;
    sregs.es.selector = 0;
    ms.enabled = 0;
    ms.oldhidden = ms.hidden;
    ms.hidden = 1;
    break;
   case 0x20:   /* Enable mouse driver. */
    ms.enabled = 1;
    ms.hidden = ms.oldhidden;
    break;
   case 0x1e:   /* Get display page number. */
    *(unsigned short*)&regs.rbx = ms.page;
    break;
   case 0x23:   /* Get language. */
    *(unsigned short*)&regs.rbx = 0;  /* USA. */
    break;
   case 0x24:   /* Get software version and mouse type. */
    *(unsigned short*)&regs.rbx = 0x626;  /* Version 6.26, like vDos. */
    *(unsigned char*)&regs.rcx = 4;       /* CH: PS/2 type. */
    *(unsigned char*)((char*)&regs.rcx + 1) = 0;
    break;
   case 0x26:   /* Get maximum virtual coordinates. */
    *(unsigned short*)&regs.rbx = ms.enabled ? 0 : 0xffff;
    *(unsigned short*)&regs.rcx = ms.maxx;
    *(unsigned short*)&regs.rdx = ms.maxy;
    break;
   case 0x1d:   /* Set display page number. */
    ms.page = (unsigned char)regs.rbx;
    break;
   case 0x09:   /* Define GFX cursor: text mode only, accepted. */
   case 0x0d:   /* Light pen emulation on. */
   case 0x0e:   /* Light pen emulation off. */
   case 0x0f:   /* Define mickey/pixel rate. */
   case 0x10:   /* Define screen region for cursor updating. */
   case 0x12:   /* Set large graphics cursor block. */
   case 0x13:   /* Set double-speed threshold. */
   case 0x1a:   /* Set mouse sensitivity. */
   case 0x1c:   /* Set interrupt rate. */
   case 0x22:   /* Set language for messages. */
    break;
   case 0x1b:   /* Get mouse sensitivity (fixed values, per vDos). */
    *(unsigned short*)&regs.rbx = 50;
    *(unsigned short*)&regs.rcx = 50;
    *(unsigned short*)&regs.rdx = 50;
    break;
   default:
    if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "debug: unsupported mouse call ignored: ax=0x%04x\n", ax);
    break;
  }
  return IA_NEXT;
}
