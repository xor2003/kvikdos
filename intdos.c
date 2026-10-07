#include "kvikdos.h"
#include "intrun.h"

/* Shared DOS-error tail: records last_dos_error_code from AX, clamps values
 * above 0x12 to "invalid data", sets CF and logs. Returns IA_NEXT so the run
 * loop returns from the software interrupt. */
int dos_error_21(void) {
  last_dos_error_code = *(unsigned short*)&regs.rax;
  if (last_dos_error_code > 0x12) *(unsigned short*)&regs.rax = 0x0d;  /* Invalid data. Query via ah == 0x59. */
  *(unsigned short*)&regs.rflags |= 1 << 0;  /* CF=1. */
  if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: int 0x21 call error\n");
  return IA_NEXT;
}

/* Set AX to a DOS error code, then run the shared error tail. */
int dos_err_ax(unsigned short code) {
  *(unsigned short*)&regs.rax = code;
  return dos_error_21();
}

/* Map the host errno to a DOS error code (defaulting to general failure), then
 * run the shared error tail. */
int dos_err_linux(void) {
  *(unsigned short*)&regs.rax = get_dos_error_code(errno, 0x1f);  /* By default: General failure. */
  return dos_error_21();
}

/* Unknown / not-implemented int 21h functions: AL=0 with CF=1, as MS-DOS 2.0
 * and 6.22 do. Returns IA_NEXT so the interrupt returns cleanly. */
int dos_unknown21(void) {
  *(unsigned char*)&regs.rax = 0;
  *(unsigned short*)&regs.rflags |= 1 << 0;
  return IA_NEXT;
}

int int21_dispatch(void) {
  switch (ah) {

  case 0x4c:
 return i21_exec();

  case 0x31:
 return i21_exec();

  case 0x4b:
 return i21_exec();

  case 0x06:
 return i21_con();

  case 0x07:
  case 0x08:
 return i21_con();

  case 0x02:
 return i21_con();

  case 0x04:
 return i21_con();

  case 0x05:
 return i21_con();

  case 0x09:
 return i21_con();

  case 0x0b:
 return i21_con();

  case 0x0a:
 return i21_con();

  case 0x01:
 return i21_con();

  case 0x0c:
 return i21_con();

  case 0x30:
 return i21_misc();

  case 0x2c:
 return i21_misc();

  case 0x2a:
 return i21_misc();

  case 0x2b:
 return i21_misc();

  case 0x2d:
 return i21_misc();

  case 0x19:
 return i21_misc();

  case 0x47:
 return i21_misc();

  case 0x39:
 return i21_misc();

  case 0x3a:
 return i21_misc();

  case 0x41:
 return i21_misc();

  case 0x56:
 return i21_misc();

  case 0x5b:
 return i21_misc();

  case 0x25:
 return i21_misc();

  case 0x35:
 return i21_misc();

  case 0x34:
 return i21_misc();

  case 0x1f:
  case 0x32:
 return i21_misc();

  case 0x43:
 return i21_misc();

  case 0x33:
 return i21_misc();

  case 0x0e:
 return i21_misc();

  case 0x2f:
 return i21_misc();

  case 0x1a:
 return i21_misc();

  case 0x63:
 return i21_misc();

  case 0x38:
 return i21_misc();

  case 0x37:
 return i21_misc();

  case 0x36:
 return i21_misc();

  case 0x3b:
 return i21_misc();

  case 0x55:
 return i21_misc();

  case 0x65:
 return i21_misc();

  case 0x50:
 return i21_misc();

  case 0x51:
  case 0x62:
 return i21_misc();

  case 0x59:
 return i21_misc();

  case 0x60:
 return i21_misc();

  case 0x67:
 return i21_misc();

  case 0x52:
 return i21_misc();

  case 0x87:
 return i21_misc();

  case 0x5a:
 return i21_misc();

  case 0x71:
 return i21_misc();

  case 0x5c:
 return i21_misc();

  case 0x5d:
  case 0x5e:
  case 0x5f:
 return i21_misc();

  case 0x54:
 return i21_misc();

  case 0x2e:
 return i21_misc();

  case 0x4d:
 return i21_misc();

  case 0x66:
 return i21_misc();

  case 0x40:
 return i21_io();

  case 0x3f:
 return i21_io();

  case 0x57:
 return i21_io();

  case 0x3e:
 return i21_io();

  case 0x45:
 return i21_io();

  case 0x46:
 return i21_io();

  case 0x42:
 return i21_io();

  case 0x44:
 return i21_io();

  case 0x68:
  case 0x6a:
 return i21_io();

  case 0x3d:
  case 0x3c:
 return i21_open();

  case 0x6c:
 return i21_open();

  case 0x4a:
 return i21_mem();

  case 0x48:
 return i21_mem();

  case 0x49:
 return i21_mem();

  case 0x58:
 return i21_mem();

  case 0x0f:
  case 0x16:
 return i21_fcb();

  case 0x10:
  case 0x13:
 return i21_fcb();

  case 0x17:
 return i21_fcb();

  case 0x14:
  case 0x15:
 return i21_fcb();

  case 0x21:
  case 0x22:
 return i21_fcb();

  case 0x23:
 return i21_fcb();

  case 0x24:
 return i21_fcb();

  case 0x27:
  case 0x28:
 return i21_fcb();

  case 0x29:
 return i21_fcb();

  case 0x4e:
 return i21_find();

  case 0x4f:
 return i21_find();

  }
  return dos_unknown21();
}
