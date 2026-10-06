#include "kvikdos.h"
#include "intrun.h"

/* Country/region information block returned by the int 21h AH=38h/AH=65h
 * services. Exactly 0x18 bytes when packed; asserted below. */
static const struct {
  unsigned short date_format;
  char currency[5];
  char thousands_separator[2];
  char decimal_separator[2];
  char date_separator[2];
  char time_separator[2];
  char currency_format, digits_after_decimal, time_format;
  unsigned short casemap_callback_ofs;  /* Points to a `retf' opcode. Natural alignment of 2 bytes. */
  unsigned short casemap_callback_seg;  /* Natural alignment of 2 bytes. */
  char data_separator[2];
} country_info = { 0, "$", ",", ".", "-", ":", 0, 2, 0, 0xf, INT_HLT_PARA - 1, "," };

typedef char CountryInfoAssert[(sizeof(country_info) == 0x18) ? 1 : -1];

/* DOS int 21h services: misc group. Returns an IA_* action. */
int i21_misc(void) {
  if (ah == 0x30) {
  /* Get DOS version number. */
            const unsigned char al = (unsigned char)regs.rax;
            if (DEBUG || DEBUG_INTVEC || DIAG_ON(DIAG_BIT_INT)) fprintf(g_diag_file, "debug: get DOS version\n");
            had_get_ints |= 8;
            tasm30_bitset |= 1;
            *(unsigned short*)&regs.rax = 5 | 0 << 8;  /* 5.0. */
            *(unsigned short*)&regs.rbx = al == 1 ? 0x1000 :  /* DOS in HMA. */
                0xff00;  /* MS-DOS with high 8 bits of OEM serial number in BL. */
            *(unsigned short*)&regs.rcx = 0;  /* Low 16 bits of OEM serial number in CX. */
  }   else if (ah == 0x2c) {
  /* Get time. */
            time_t ts = time(0);
            struct tm *tm = localtime(&ts);
            struct timeval tv;
            const unsigned char hundredths = gettimeofday(&tv, NULL) == 0 ? (unsigned char)(tv.tv_usec / 10000) : 0;
            *(unsigned short*)&regs.rcx = tm->tm_hour << 8 | tm->tm_min;
            *(unsigned short*)&regs.rdx = tm->tm_sec << 8 | hundredths;
            tasm30_bitset |= 0x40;
  }   else if (ah == 0x2a) {
  /* Get date. */
            time_t ts = time(0);
            struct tm *tm = localtime(&ts);
            *(unsigned char*)&regs.rax = tm->tm_wday;
            *(unsigned short*)&regs.rcx = tm->tm_year + 1900;
            *(unsigned short*)&regs.rdx = (tm->tm_mon + 1) << 8 | tm->tm_mday;
            tasm30_bitset |= 0x20;
  }   else if (ah == 0x2b) {
  /* Set date. We don't set the host clock, just validate. */
            /* CX=year, DH=month, DL=day. AL=0 on success, 0xff on invalid.
             * This doubles as the DESQview presence probe (AX=2b01, CX='DE',
             * DX='SQ'): reporting 0xff means "not DESQview", which extenders
             * (e.g. Phar Lap) need to see.
             */
            const unsigned y = *(unsigned short*)&regs.rcx, mo = ((unsigned char*)&regs.rdx)[1], d = *(unsigned char*)&regs.rdx;
            *(unsigned char*)&regs.rax = (y >= 1980 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) ? 0 : 0xff;
  }   else if (ah == 0x2d) {
  /* Set time. We don't set the host clock, just validate. */
            const unsigned h = ((unsigned char*)&regs.rcx)[1], mi = *(unsigned char*)&regs.rcx, s = ((unsigned char*)&regs.rdx)[1];
            *(unsigned char*)&regs.rax = (h < 24 && mi < 60 && s < 60) ? 0 : 0xff;
  }   else if (ah == 0x19) {
  /* Get current drive. */
            *(unsigned char*)&regs.rax = dir_state->drive - 'A';
  }   else if (ah == 0x47) {
  /* Get current directory. */
            char *p, *p0, *pend;
            const char *s;
            /* Input: DL: 0 = current drive, 1: A: */
            if (*(unsigned char*)&regs.rdx != 0) { 
              *(unsigned short*)&regs.rax = 0xf;  /* Invalid drive specified. */
              return dos_error_21();
            }
            p0 = p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + *(unsigned short*)&regs.rsi;
            pend = p + 63;
            s = dir_state->current_dir[dir_state->drive - 'A'];
            for (; *s != '\0' && p != pend; ++s, ++p) {
              *p = *s;
            }
            if (p != p0 && p[-1] == '/') --p;  /* Remove trailing '/'. */
            *p = '\0';  /* Silently truncate to 64 bytes. */
            if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: get current directory on drive %c: (%s)\n", dir_state->drive, p0);
            *(unsigned short*)&regs.rax = 0x100;  /* DOSBox 0.74-4 also does this. */
  }   else if (ah == 0x39) {
  /* Create subdirectory (mkdir). */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const int result = mkdir(get_linux_filename(p), 0755);
            if (result < 0) return dos_err_linux();
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x3a) {
  /* Remove subdirectory (rmdir). */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const int result = rmdir(get_linux_filename(p));
            if (result < 0) return dos_err_linux();
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x41) {
  /* Delete file. */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const int fd = unlink(get_linux_filename(p));
            if (fd < 0) return dos_err_linux();
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x56) {
  /* Rename file. */
            const char * const p_old = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const char * const p_new = (char*)mem + ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdi);  /* !! Security: check bounds. */
            int fd = rename(get_linux_filename(p_old), get_linux_filename_r(p_new, dir_state, fnbuf2, NULL));
            if (fd < 0) return dos_err_linux();
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x5b) {
  /* Create new file (fails if exists). */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const int fd = open(get_linux_filename(p), O_RDWR | O_CREAT | O_EXCL, 0644);
            int dos_fd;
            if (fd < 0) return dos_err_linux();
            dos_fd = fd < 5 ? ensure_fd_is_at_least(fd, 5) : fd;
            dos_fd = map_fd_open(dos_fd);
            if ((dos_fd + 0U) >> 16) {
              *(unsigned short*)&regs.rax = 4;  /* Too many open files. */
              return dos_error_21();
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            *(unsigned short*)&regs.rax = dos_fd;
  }   else if (ah == 0x25) {
  /* Set interrupt vector. */
            if (set_int((unsigned char)regs.rax, *(unsigned short*)&regs.rdx | sregs.ds.selector << 16, mem, had_get_ints, &tasm30_bitset)) return IA_FATAL;
  }   else if (ah == 0x35) {
  /* Get interrupt vector. */
            const unsigned char get_int_num = (unsigned char)regs.rax;
            if (DEBUG || DEBUG_INTVEC || DIAG_ON(DIAG_BIT_INT)) fprintf(g_diag_file, "debug: get interrupt vector int:%02x\n", get_int_num);
            if (get_int_num == 0) had_get_ints |= 1;  /* Turbo Pascal 7.0 programs start with this. */
            if (get_int_num == 0x18) { had_get_ints |= 2; tasm30_bitset |= 0x10; }  /* TASM 3.0, TASM 3.2, Borland C++ 2.0 compiler bcc.exe for memory allocation. */
            if (get_int_num == 0x06) had_get_ints |= 4;  /* TLINK 4.0. */
            if ((had_get_ints & 8) && get_int_num == 0x34) had_get_ints |= 0x10;  /* JWasm 2.11a jwasmr.exe */
            /* !!! TODO(pts): Make the default permissive in general, and enable these protections only on a flag. */
            if ((had_get_ints & 1) ||
                get_int_num - 0x22 + 0U <= 0x24 - 0x22 + 0U ||  /* Microsoft BASIC Professional Development System 7.10 linker pblink.exe gets interrupt vector 0x24. */
                get_int_num == 0x18 ||  /* TASM 3.2, used for memory allocation. */
                get_int_num == 0x06 ||  /* TLINK 4.0. */
                get_int_num == 0x67 ||  /* WLINK 7.0. */
                ((had_get_ints & 0x10) && (get_int_num - 0x34 + 0U <= 0x3d - 0x34 + 0U || get_int_num == 0x02 || get_int_num == 0x1b)) ||  /* JWasm 2.11a jwasmr.exe */
                ((had_get_ints & 2) && (get_int_num == 0x1b || get_int_num == 0x3f)) ||  /* Borland Turbo C++ 1.01 compiler tcc.exe, Borland C++ 2.0 complier bcc.exe */
               0) {
              const unsigned short *pp = (const unsigned short*)((char*)mem + (get_int_num << 2));
              if (DEBUG || DIAG_ON(DIAG_BIT_INT)) fprintf(g_diag_file, "debug: get interrupt vector int:%02x is cs:%04x ip:%04x\n", get_int_num, pp[1], pp[0]);
              (*(unsigned short*)&regs.rbx) = pp[0];
              SET_SREG(es, pp[1]);
            } else {
              const unsigned short *pp = (const unsigned short*)((char*)mem + (get_int_num << 2));
              if (DEBUG || DIAG_ON(DIAG_BIT_INT)) fprintf(g_diag_file, "debug: permissive get interrupt vector int:%02x is cs:%04x ip:%04x\n", get_int_num, pp[1], pp[0]);
              (*(unsigned short*)&regs.rbx) = pp[0];
              SET_SREG(es, pp[1]);
            }
  }   else if (ah == 0x34) {
  /* Get InDOS flag address. */
            ((char*)mem)[0x041a] = 0;  /* InDOS = 0 (DOS idle). */
            SET_SREG(es, 0);
            *(unsigned short*)&regs.rbx = 0x041a;
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x1f || ah == 0x32) {
  /* DPB probes. */
            return dos_unknown21();
  }   else if (ah == 0x43) {
  /* Get/set file attributes. */
            const unsigned char al = (unsigned char)regs.rax;
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const char *fn;
            if (al > 1) return dos_err_ax(0x57);
            fn = get_linux_filename(p);
            if (al == 0) {  /* Get. */
              struct stat st;
              if (stat(fn, &st) != 0) return dos_err_linux();
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              *(unsigned short*)&regs.rax = (st.st_mode & 0200) ? 0 : 1;  /* readonly */
            } else {  /* Set. */
              struct stat st;
              mode_t mode;
              unsigned short attr = *(unsigned short*)&regs.rcx;
              if (stat(fn, &st) != 0) return dos_err_linux();
              mode = st.st_mode;
              /* Accept DOS attribute bits: RO(1), H(2), S(4), A(0x20). */
              if (attr & 1) mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
              else mode |= S_IWUSR;
              if (chmod(fn, mode) != 0) return dos_err_linux();
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            }
  }   else if (ah == 0x33) {
  /* Get/set system values. */
            const unsigned char al = (unsigned char)regs.rax;
            const unsigned char dl = (unsigned char)regs.rdx;
            if (al == 0) {
              *(unsigned char*)&regs.rdx = ctrl_break_checking;  /* 0 or 1. */
            } else if (al == 1) {
              ctrl_break_checking = (dl > 0);
            } else if (al == 2) {
              const unsigned char old = ctrl_break_checking;
              ctrl_break_checking = (dl > 0);
              *(unsigned char*)&regs.rdx = old;
            } else if (al == 5) {
              *(unsigned char*)&regs.rdx = 'C' - 'A' + 1;  /* Boot drive. */
            } else if (al == 6) {
              *(unsigned short*)&regs.rbx = 0x500;  /* DOS 5.0. */
              *(unsigned short*)&regs.rdx = 0x100;  /* DL contains DOS revision number 0. */
            } else {
              if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "debug: unsupported get/set system values subcall: al=%02x dl=%02x\n", al, dl);
              return dos_unknown21();
            }
  }   else if (ah == 0x0e) {
  /* Select disk. */
            const unsigned char dl = (unsigned char)regs.rdx;
            if (dl < DRIVE_COUNT && dir_state->linux_mount_dir[dl]) dir_state->drive = dl + 'A';
            *(unsigned char*)&regs.rax = 26;  /* 26 drives: 'A' .. 'Z'. */
  }   else if (ah == 0x2f) {
  /* Get disk transfer address (DTA). */
            SET_SREG(es, dta_seg_ofs >> 16);
            *(unsigned short*)&regs.rbx = dta_seg_ofs;
  }   else if (ah == 0x1a) {
  /* Set disk transfer address (DTA). */
            dta_seg_ofs = *(unsigned short*)&regs.rdx | sregs.ds.selector << 16;
  }   else if (ah == 0x63) {
  /* Get lead byte table. Multibyte support in MS-DOS 2.25. */
            return dos_unknown21();
  }   else if (ah == 0x38) {
  /* Get/set country dependent information. */
            const unsigned char al = (unsigned char)regs.rax;
            char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            if (al == 0x00) {  /* Get. */
              tasm30_bitset |= 2;
              memcpy(p, &country_info, 0x18);
              *(unsigned short*)&regs.rax = *(unsigned short*)&regs.rbx = 1;
            } else {
              if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "debug: unsupported country subcall: al=%02x\n", al);
              return dos_unknown21();
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x37) {
  /* Get/set switch character (for command-line flags). */
            const unsigned char al = (unsigned char)regs.rax;
            if (al == 0x00) {  /* Get. */
              *(unsigned char*)&regs.rax = 0;  /* Success. */
              *(unsigned char*)&regs.rdx = '/';
            } else if (al == 0x02) {  /* Get device prefix flag. */
              *(unsigned char*)&regs.rdx = 0xff;  /* Device prefix /dev/... not needed. */
            } else {
              if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "debug: unsupported switch-character subcall: al=%02x\n", al);
              return dos_unknown21();
            }
  }   else if (ah == 0x36) {
  /* Get disk free space. */
            /* Borrowed from ntvdm: report believable constants for lots of free space. */
            const unsigned dl = *(unsigned char*)&regs.rdx;
            const unsigned drv = dl ? dl - 1 : (unsigned)(dir_state->drive - 'A');
            if (drv >= DRIVE_COUNT || !dir_state->linux_mount_dir[drv]) {
              *(unsigned short*)&regs.rax = 0xffff;  /* Invalid drive. */
            } else {
              *(unsigned short*)&regs.rax = 8;       /* Sectors per cluster. */
              *(unsigned short*)&regs.rbx = 0x6fff;  /* Available clusters: ~224 MiB free. */
              *(unsigned short*)&regs.rcx = 512;     /* Bytes per sector. */
              *(unsigned short*)&regs.rdx = 0x7fff;  /* Total clusters: ~256 MiB. */
            }
  }   else if (ah == 0x3b) {
  /* Change current directory (chdir). */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            char abs_path[DOS_PATH_SIZE + 4];
            struct stat st;
            if (*(const unsigned char*)p == 0x1a) {  /* Turbo Prolog 1.1 sends ^Z as drive letter. */
              *(unsigned short*)&regs.rax = 3;  /* Path not found. */
              return dos_error_21();
            }
            dos_normalize_abspath(p, dir_state, abs_path, sizeof(abs_path));
            if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: chdir normalized (%s) -> (%s)\n", p, abs_path);
            if (abs_path[0] == '\0') {
              *(unsigned short*)&regs.rax = 3;  /* Path not found. */
              return dos_error_21();
            }
            {
              const char * const linux_dir = get_linux_filename_r(abs_path, dir_state, fnbuf, NULL);
              if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: chdir linux_dir (%s)\n", linux_dir);
              if (abs_path[3] == '\0') {  /* Drive root "D:\". */
                if (!dir_state->linux_mount_dir[abs_path[0] - 'A']) {
                  *(unsigned short*)&regs.rax = 3;  /* Path not found. */
                  return dos_error_21();
                }
              } else if (*linux_dir == '\0' || stat(linux_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
                *(unsigned short*)&regs.rax = 3;  /* Path not found. */
                return dos_error_21();
              }
            }
            /* abs_path is "D:\" or "D:\A\B"; current_dir[] stores "A\B\" or "". */
            {
              const char *s = abs_path + 3;
              char *t = dir_state->current_dir[abs_path[0] - 'A'];
              char * const tend = t + sizeof(dir_state->current_dir[0]) - 1;
              while (*s && t < tend) *t++ = *s++;
              if (t != dir_state->current_dir[abs_path[0] - 'A'] && t < tend && t[-1] != '\\') *t++ = '\\';
              *t = '\0';
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x55) {
  /* Create new PSP (undocumented DOS 2.x+). */
            /* Borrowed from ntvdm: DX is the caller-provided segment for the new
             * PSP, SI is the value for the top-of-memory field. The new PSP
             * becomes the current process. Used by Turbo Pascal 5.5. */
            const unsigned new_psp_para = *(unsigned short*)&regs.rdx;
            char * const psp = (char*)mem + (new_psp_para << 4);
            if (!is_linear_byte_user_writable(new_psp_para << 4) || !is_linear_byte_user_writable((new_psp_para << 4) + 0xff)) return dos_err_ax(0x57);
            memcpy(psp, (const char*)mem + (current_psp_para << 4), 0x100);  /* Inherit JFT and vectors. */
            *(unsigned short*)(psp + 0) = 0x20cd;  /* `int 0x20' opcode. */
            *(unsigned short*)(psp + 2) = *(unsigned short*)&regs.rsi;  /* Top of memory. */
            *(unsigned*)(psp + 0x0a) = *(unsigned*)((char*)mem + (0x22 << 2));  /* int 0x22 vector copy. */
            *(unsigned*)(psp + 0x0e) = *(unsigned*)((char*)mem + (0x23 << 2));  /* int 0x23 vector copy. */
            *(unsigned*)(psp + 0x12) = *(unsigned*)((char*)mem + (0x24 << 2));  /* int 0x24 vector copy. */
            *(unsigned short*)(psp + 0x16) = (unsigned short)current_psp_para;  /* Parent PSP. */
            *(unsigned short*)(psp + 6) = 0xffff;  /* .COM bytes available in segment. */
            current_psp_para = new_psp_para;
  }   else if (ah == 0x65) {
  /* Get extended country information (DOS 3.3+). */
            const unsigned char al = (unsigned char)regs.rax;
            char * const p = (char*)mem + ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdi);  /* !! Security: check bounds. */
            if (al == 1) {  /* General country info. */
              p[0] = 1;  /* Info ID. */
              *(unsigned short*)(p + 1) = 0x26;  /* Buffer size. */
              *(unsigned short*)(p + 3) = 1;     /* Country: USA. */
              *(unsigned short*)(p + 5) = 437;   /* Code page. */
              memcpy(p + 7, &country_info, 0x18);
              *(unsigned short*)&regs.rcx = 7 + 0x18;  /* Bytes written. */
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else if (al == 2) {  /* Uppercase table. */
              unsigned i;
              p[0] = 2;  /* Info ID. */
              *(unsigned short*)(p + 1) = 32;  /* Table size. */
              for (i = 0; i < 32; ++i) p[3 + i] = (char)(0x80 + i);  /* Identity. */
              p[3 + 0x01] = (char)0x9a;  /* ü -> Ü */
              p[3 + 0x04] = (char)0x8e;  /* ä -> Ä */
              p[3 + 0x06] = (char)0x8f;  /* å -> Å */
              p[3 + 0x07] = (char)0x80;  /* ç -> Ç */
              p[3 + 0x0e] = (char)0x92;  /* æ -> Æ */
              p[3 + 0x14] = (char)0x99;  /* ö -> Ö */
              p[3 + 0x1b] = (char)0x9a;  /* ü -> Ü */
              *(unsigned short*)&regs.rcx = 3 + 32;
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else {
              *(unsigned short*)&regs.rax = 2;  /* File not found (bad info id). */
              return dos_error_21();
            }
  }   else if (ah == 0x51 || ah == 0x62) {
  /* Get process ID (PSP) (0x51). Get PSP (0x62). */
            *(unsigned short*)&regs.rbx = (unsigned short)current_psp_para;
  }   else if (ah == 0x59) {
  /* Get extended error information. */
            *(unsigned short*)&regs.rax = last_dos_error_code;
            if (last_dos_error_code == 0)  {  /* No error. */
              *(unsigned short*)&regs.rbx = 0xd << 8  /* error class: unknown */ | 6  /* ignore */;
              *(unsigned short*)&regs.rcx = *(unsigned char*)&regs.rcx | 1 << 8;  /* CH: Locus: unknown. */
            } else {
              *(unsigned short*)&regs.rbx = 6 << 8  /* error class: system failure */ | 4  /* abort with cleanup */;
              *(unsigned short*)&regs.rcx = *(unsigned char*)&regs.rcx | 2 << 8;  /* CH: Locus: block device. */
            }
  }   else if (ah == 0x60) {
  /* Get fully qualified filename. */
            const char * const fn = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rsi);  /* !! Security: check bounds. */
            char * const path_out = (char*)mem + ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdi);  /* 128 bytes of buffer. */  /* !! Security: check bounds. */
            get_dos_abspath_r(fn, dir_state, path_out, 128);
            if (*path_out == '\0') {
              *(unsigned short*)&regs.rax = 0x100;  /* We set AH to some arbitrary error code. */
              return dos_error_21();
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x67) {
  /* Set handle count. */
            /* https://stanislavs.org/helppc/int_21-67.html Says that only the first 20 handles are copied to the child process. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x52) {
  /* Get pointer to INVARS (DOS list-of-lists). */
            /* Microsoft Macro Assembler 6.00B driver masm.exe. */
            /* Phar Lap TELLME walks the MCB chain from [ES:BX-2]. The first-MCB
               word must sit at a nonzero offset inside the returned segment
               (BX must be > 0, else ES:BX-2 wraps to ES:0xfffe): per
               dos_info_t, ES:BX points at first_dpb with first_mcb at BX-2.
               ES=INVARS_LIN>>4-1 (0x50), BX=0x10 puts first_mcb at 0x50e. */
            (*(unsigned short*)&regs.rbx) = (INVARS_LIN & 0xf) + 0x10;
            SET_SREG(es, (INVARS_LIN >> 4) - 1);
            if (DEBUG || DIAG_ON(DIAG_BIT_VERBOSE)) { unsigned u; const char *mm; fprintf(g_diag_file, "debug: invars es:bx=%04x:%04x first_mcb=%04x\n", (INVARS_LIN >> 4) - 1, (INVARS_LIN & 0xf) + 0x10, *(unsigned short*)((char*)mem + INVARS_LIN - 2)); u = PSP_PARA; for (;;) { mm = (const char*)mem + (u << 4) - 16; fprintf(g_diag_file, "debug: chain mcb@%04x type=%c pid=%04x size=%04x psize=%04x bad=%d\n", u - 1, MCB_TYPE(mm) ? MCB_TYPE(mm) : '?', MCB_PID(mm), MCB_SIZE_PARA(mm), MCB_PSIZE_PARA(mm), is_mcb_bad(mem, u)); if (MCB_TYPE(mm) == 'Z' || (MCB_TYPE(mm) != 'M' && MCB_TYPE(mm) != 'Z')) break; u += 1 + MCB_SIZE_PARA(mm); if (u > DOS_ALLOC_PARA_LIMIT) break; } }
  }   else if (ah == 0x87) {
  /* Used by older Microsoft toolchains. */
            return dos_unknown21();  /* Report unsupported without fatal abort. */
  }   else if (ah == 0x5a) {
  /* Create temporary file. */
            const char *tmpl = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            char dos_prefix[DOS_PATH_SIZE + 4], dos_name[DOS_PATH_SIZE + 4];
            size_t tn = 0, pfx_len;
            const char *last_slash = NULL, *scan;
            int fd = -1;
            unsigned i;
            for (; tmpl[tn] != '\0' && tn + 1 < sizeof(dos_prefix); ++tn) {}
            if (tn == 0) {
              strcpy(dos_prefix, "TMP");
            } else {
              memcpy(dos_prefix, tmpl, tn);
              dos_prefix[tn] = '\0';
            }
            for (scan = dos_prefix; *scan; ++scan) if (*scan == '\\' || *scan == '/' || *scan == ':') last_slash = scan;
            pfx_len = last_slash ? (size_t)(last_slash + 1 - dos_prefix) : 0;
            if (pfx_len >= sizeof(dos_name)) pfx_len = 0;
            memcpy(dos_name, dos_prefix, pfx_len);
            dos_name[pfx_len] = '\0';
            for (i = 0; i != 0x10000; ++i) {
              char *w = dos_name + pfx_len;
              unsigned v = i;
              static const char hex[] = "0123456789ABCDEF";
              *w++ = 'K'; *w++ = 'V';
              *w++ = hex[(v >> 12) & 15];
              *w++ = hex[(v >> 8) & 15];
              *w++ = hex[(v >> 4) & 15];
              *w++ = hex[v & 15];
              *w++ = '.';
              *w++ = 'T'; *w++ = 'M'; *w++ = 'P';
              *w = '\0';
              if (*get_linux_filename_r(dos_name, dir_state, fnbuf, NULL) == '\0') {
                *(unsigned short*)&regs.rax = 3;  /* Path not found. */
                return dos_error_21();
              }
              fd = open(fnbuf, O_RDWR | O_CREAT | O_EXCL, 0644);
              if (fd >= 0) break;
              if (errno != EEXIST) {
                *(unsigned short*)&regs.rax = get_dos_error_code(errno, 0x1f);
                return dos_error_21();
              }
            }
            if (fd < 0) {
              *(unsigned short*)&regs.rax = 0x50;  /* File exists. */
              return dos_error_21();
            }
            if (fd < 5) fd = ensure_fd_is_at_least(fd, 5);
            fd = map_fd_open(fd);
            if ((fd + 0U) >> 16) {
              *(unsigned short*)&regs.rax = 4;  /* Too many open files. */
              return dos_error_21();
            }
            strcpy((char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx), dos_name);
            *(unsigned short*)&regs.rax = fd;
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x71) {
  const unsigned char al = (unsigned char)regs.rax;
  if (al == 0x0d || al - 0x39 + 0U <= 0x4f - 0x39 + 0U || al == 0x56 || al == 0x60 || al == 0x6c || al - 0xa0 + 0U <= 0xaa - 0xa0 + 0U) {  /* http://mirror.cs.msu.ru/oldlinux.org/Linux.old/docs/interrupts/int-html/int-21.htm */
     /* ax == 0x716c. Open or create with long file name (starting from Windows 95). http://mirror.cs.msu.ru/oldlinux.org/Linux.old/docs/interrupts/int-html/rb-3209.htm */
    return dos_unknown21();  /* flat assembler 1.73.24 fasmlite.exe relies on this. */
  } else {
    return IA_FATAL_INT;
  }
  }   else if (ah == 0x5c) {
  /* Record lock/unlock. */
            const unsigned char al = (unsigned char)regs.rax;
            const int fd = get_linux_fd(*(unsigned short*)&regs.rbx, &kvm_fds);
            struct flock fl;
            if (fd < 0) return dos_err_ax(6);
            memset(&fl, 0, sizeof(fl));
            fl.l_type = (al == 0) ? F_WRLCK : (al == 1) ? F_UNLCK : (short)-1;
            if (fl.l_type == (short)-1) return dos_unknown21();
            fl.l_whence = SEEK_SET;
            fl.l_start = ((long)(*(unsigned short*)&regs.rcx) << 16) | *(unsigned short*)&regs.rdx;
            fl.l_len = ((long)(*(unsigned short*)&regs.rsi) << 16) | *(unsigned short*)&regs.rdi;
            if (fcntl(fd, F_SETLK, &fl) == 0) {
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else {
              *(unsigned short*)&regs.rax = 0x21;  /* Lock violation. */
              return dos_error_21();
            }
  }   else if (ah == 0x5d || ah == 0x5e || ah == 0x5f) {
  /* Share/network redirector services. */
            return dos_unknown21();
  }   else if (ah == 0x54) {
  /* Get verify flag. */
            *(unsigned char*)&regs.rax = 0;  /* Verify off. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x2e) {
  /* Set verify flag. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x4d) {
  /* Get return code from child process. */
            *(unsigned short*)&regs.rax = ((unsigned short)last_exec_return_code) << 8;
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x66) {
  /* Get/set global code page. */
            const unsigned char al = (unsigned char)regs.rax;
            if (al == 1) {
              *(unsigned short*)&regs.rbx = *(unsigned short*)&regs.rdx = 437;  /* CP-437: https://en.wikipedia.org/wiki/Code_page_437 */
            } else {
              return IA_FATAL_INT;
            }
  } 
  return IA_NEXT;
}
