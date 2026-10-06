#include "kvikdos.h"
#include "intrun.h"

/* DOS int 21h services: fcb group. Returns an IA_* action. */
int i21_fcb(void) {
  if (ah == 0x0f || ah == 0x16) {
  /* Open (0x0f) / create (0x16) file via FCB. */
            /* Borrowed from ntvdm. FCB layout: 0:drive 1:8:name 9:3:ext 0xc:u16 curBlock
             * 0xe:u16 recSize 0x10:u32 fileSize 0x14:u16 date 0x16:u16 time
             * 0x20:u8 curRecord 0x21:u32 recNumber (low 3 bytes if recSize >= 64). */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            char * const fcb = (char*)mem + fcb_lin;
            char fcbname[16];
            int fd = -1;
            unsigned fi;
            struct stat st;
            *(unsigned char*)&regs.rax = 0xff;  /* Failure. */
            if (is_linear_byte_user_writable(fcb_lin) && is_linear_byte_user_writable(fcb_lin + 0x24)) {
              fcb_filename(fcb, dir_state->drive, fcbname, sizeof(fcbname));
              /* Close it if already open at this FCB address. */
              for (fi = 0; fi < FCB_FILE_COUNT; ++fi) {
                if (fcb_lin_table[fi] == fcb_lin) { close(fcb_fd_table[fi]); fcb_lin_table[fi] = 0; fcb_fd_table[fi] = -1; break; }
              }
              fd = open(get_linux_filename(fcbname), ah == 0x16 ? (O_RDWR | O_CREAT | O_TRUNC) : O_RDWR, 0644);
              if (fd < 0 && ah == 0x0f) fd = open(get_linux_filename(fcbname), O_RDONLY);
              if (fd >= 0) {
                for (fi = 0; fi < FCB_FILE_COUNT; ++fi) if (!fcb_lin_table[fi]) break;
                if (fi == FCB_FILE_COUNT) { close(fd); goto fcb_done; }
                fcb_lin_table[fi] = fcb_lin;
                fcb_fd_table[fi] = fd;
                if (!fcb[0]) fcb[0] = dir_state->drive - 'A' + 1;
                *(unsigned short*)(fcb + 0x0c) = 0;     /* curBlock. */
                *(unsigned short*)(fcb + 0x0e) = 0x80;  /* recSize = 128. */
                if (fstat(fd, &st) == 0) {
                  struct tm *tm = localtime(&st.st_mtime);
                  *(unsigned*)(fcb + 0x10) = (unsigned)st.st_size;
                  *(unsigned short*)(fcb + 0x14) = dos_fat_date(tm);
                  *(unsigned short*)(fcb + 0x16) = dos_fat_time(tm);
                } else {
                  *(unsigned*)(fcb + 0x10) = 0;
                  *(unsigned short*)(fcb + 0x14) = *(unsigned short*)(fcb + 0x16) = 0;
                }
                fcb[0x20] = 0;  /* curRecord. */
                /* recNumber deliberately not initialized: sequential-only apps may
                 * not allocate the full FCB (ntvdm comment, PLI.EXE). */
                *(unsigned char*)&regs.rax = 0;  /* Success. */
              }
            }
           fcb_done:;
  }   else if (ah == 0x10 || ah == 0x13) {
  /* Close (0x10) / delete (0x13) file via FCB. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            const char * const fcb = (const char*)mem + fcb_lin;
            unsigned fi;
            *(unsigned char*)&regs.rax = 0xff;  /* Failure. */
            for (fi = 0; fi < FCB_FILE_COUNT; ++fi) {
              if (fcb_lin_table[fi] == fcb_lin) { close(fcb_fd_table[fi]); fcb_lin_table[fi] = 0; fcb_fd_table[fi] = -1; *(unsigned char*)&regs.rax = 0; break; }
            }
            if (ah == 0x10) {
              /* AL already reflects whether the FCB was found open. */
            } else {  /* 0x13 delete: remove the file too. */
              char fcbname[16];
              fcb_filename(fcb, dir_state->drive, fcbname, sizeof(fcbname));
              *(unsigned char*)&regs.rax = unlink(get_linux_filename(fcbname)) == 0 ? 0 : 0xff;
            }
  }   else if (ah == 0x17) {
  /* Rename file via FCB. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            const char * const fcb = (const char*)mem + fcb_lin;
            char oldname[16], newname[16];
            *(unsigned char*)&regs.rax = 0xff;
            if (is_linear_byte_user_writable(fcb_lin) && is_linear_byte_user_writable(fcb_lin + 0x1c)) {
              fcb_filename(fcb, dir_state->drive, oldname, sizeof(oldname));
              fcb_filename(fcb + 0x10, dir_state->drive, newname, sizeof(newname));  /* New name at FCB+0x11; drive at +0x10. */
              newname[0] = oldname[0];  /* Rename stays on the same drive. */
              if (rename(get_linux_filename(oldname), get_linux_filename_r(newname, dir_state, fnbuf2, NULL)) == 0) *(unsigned char*)&regs.rax = 0;
            }
  }   else if (ah == 0x14 || ah == 0x15) {
  /* Sequential read (0x14) / write (0x15) via FCB. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            char * const fcb = (char*)mem + fcb_lin;
            const unsigned dta_linear = (dta_seg_ofs & 0xffff) + (dta_seg_ofs >> 16 << 4);
            unsigned fi;
            int fd = -1;
            *(unsigned char*)&regs.rax = 1;  /* EOF / error. */
            for (fi = 0; fi < FCB_FILE_COUNT; ++fi) if (fcb_lin_table[fi] == fcb_lin) { fd = fcb_fd_table[fi]; break; }
            if (fd >= 0) {
              const unsigned recsize = *(unsigned short*)(fcb + 0x0e) ? *(unsigned short*)(fcb + 0x0e) : 0x80;
              unsigned long seq_rec = ((unsigned long)*(unsigned short*)(fcb + 0x0c) << 7) + *(unsigned char*)(fcb + 0x20);
              const unsigned long fpos = seq_rec * recsize;
              if (ah == 0x14) {  /* Read. */
                memset((char*)mem + dta_linear, 0, recsize);
                if (lseek(fd, (off_t)fpos, SEEK_SET) >= 0) {
                  const int got = read(fd, (char*)mem + dta_linear, recsize);
                  if (got > 0) {
                    ++seq_rec;
                    *(unsigned short*)(fcb + 0x0c) = (unsigned short)(seq_rec >> 7);
                    fcb[0x20] = (char)(seq_rec & 0x7f);
                    *(unsigned char*)&regs.rax = got == (int)recsize ? 0 : 3;
                  }
                }
              } else {  /* Write. */
                if (lseek(fd, (off_t)fpos, SEEK_SET) >= 0) {
                  const int got = write(fd, (const char*)mem + dta_linear, recsize);
                  if (got == (int)recsize) {
                    ++seq_rec;
                    *(unsigned short*)(fcb + 0x0c) = (unsigned short)(seq_rec >> 7);
                    fcb[0x20] = (char)(seq_rec & 0x7f);
                    *(unsigned char*)&regs.rax = 0;
                  } else if (got >= 0) {
                    *(unsigned char*)&regs.rax = 1;  /* Disk full (partial write). */
                  }
                }
              }
            }
  }   else if (ah == 0x21 || ah == 0x22) {
  /* Random read (0x21) / write (0x22) via FCB. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            char * const fcb = (char*)mem + fcb_lin;
            const unsigned dta_linear = (dta_seg_ofs & 0xffff) + (dta_seg_ofs >> 16 << 4);
            unsigned fi;
            int fd = -1;
            *(unsigned char*)&regs.rax = 1;
            for (fi = 0; fi < FCB_FILE_COUNT; ++fi) if (fcb_lin_table[fi] == fcb_lin) { fd = fcb_fd_table[fi]; break; }
            if (fd >= 0) {
              const unsigned recsize = *(unsigned short*)(fcb + 0x0e) ? *(unsigned short*)(fcb + 0x0e) : 0x80;
              const unsigned long recnum = recsize >= 64 ? (*(unsigned*)(fcb + 0x21) & 0xffffff) : *(unsigned*)(fcb + 0x21);
              const unsigned long fpos = recnum * recsize;
              /* Set sequential position from random (Digital Research PL/I depends on this). */
              *(unsigned short*)(fcb + 0x0c) = (unsigned short)(recnum >> 7);
              fcb[0x20] = (char)(recnum & 0x7f);
              if (ah == 0x21) {  /* Read. */
                memset((char*)mem + dta_linear, 0, recsize);
                if (lseek(fd, (off_t)fpos, SEEK_SET) >= 0) {
                  const int got = read(fd, (char*)mem + dta_linear, recsize);
                  if (got > 0) *(unsigned char*)&regs.rax = got == (int)recsize ? 0 : 3;
                }
              } else {  /* Write. */
                if (lseek(fd, (off_t)fpos, SEEK_SET) >= 0) {
                  const int got = write(fd, (const char*)mem + dta_linear, recsize);
                  if (got == (int)recsize) *(unsigned char*)&regs.rax = 0;
                  else if (got >= 0) *(unsigned char*)&regs.rax = 1;  /* Disk full. */
                }
              }
            }
  }   else if (ah == 0x23) {
  /* Get file size via FCB. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            char * const fcb = (char*)mem + fcb_lin;
            char fcbname[16];
            struct stat st;
            *(unsigned char*)&regs.rax = 0xff;
            if (is_linear_byte_user_writable(fcb_lin) && is_linear_byte_user_writable(fcb_lin + 0x24)) {
              fcb_filename(fcb, dir_state->drive, fcbname, sizeof(fcbname));
              if (stat(get_linux_filename(fcbname), &st) == 0) {
                const unsigned recsize = *(unsigned short*)(fcb + 0x0e) ? *(unsigned short*)(fcb + 0x0e) : 0x80;
                const unsigned long nrec = ((unsigned long)st.st_size + recsize - 1) / recsize;
                if (recsize >= 64) *(unsigned*)(fcb + 0x21) = (*(unsigned*)(fcb + 0x21) & 0xff000000U) | (nrec & 0xffffff);
                else *(unsigned*)(fcb + 0x21) = nrec;
                *(unsigned*)(fcb + 0x10) = (unsigned)st.st_size;
                *(unsigned char*)&regs.rax = 0;
              }
            }
  }   else if (ah == 0x24) {
  /* Set random record field from sequential position. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            char * const fcb = (char*)mem + fcb_lin;
            if (is_linear_byte_user_writable(fcb_lin) && is_linear_byte_user_writable(fcb_lin + 0x24)) {
              const unsigned recsize = *(unsigned short*)(fcb + 0x0e);
              if (recsize) {
                const unsigned long seq_rec = ((unsigned long)*(unsigned short*)(fcb + 0x0c) << 7) + *(unsigned char*)(fcb + 0x20);
                const unsigned long recnum = seq_rec;
                if (recsize >= 64) *(unsigned*)(fcb + 0x21) = (*(unsigned*)(fcb + 0x21) & 0xff000000U) | (recnum & 0xffffff);
                else *(unsigned*)(fcb + 0x21) = recnum;
              }
            }
  }   else if (ah == 0x27 || ah == 0x28) {
  /* Random block read (0x27) / write (0x28) via FCB. */
            const unsigned fcb_lin = ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);
            char * const fcb = (char*)mem + fcb_lin;
            const unsigned dta_linear = (dta_seg_ofs & 0xffff) + (dta_seg_ofs >> 16 << 4);
            unsigned nrec_req = *(unsigned short*)&regs.rcx;
            unsigned fi;
            int fd = -1;
            *(unsigned short*)&regs.rcx = 0;
            *(unsigned char*)&regs.rax = 1;
            for (fi = 0; fi < FCB_FILE_COUNT; ++fi) if (fcb_lin_table[fi] == fcb_lin) { fd = fcb_fd_table[fi]; break; }
            if (fd >= 0 && nrec_req) {
              const unsigned recsize = *(unsigned short*)(fcb + 0x0e) ? *(unsigned short*)(fcb + 0x0e) : 0x80;
              const unsigned long recnum = recsize >= 64 ? (*(unsigned*)(fcb + 0x21) & 0xffffff) : *(unsigned*)(fcb + 0x21);
              const unsigned long fpos = recnum * recsize;
              unsigned long done_bytes = 0;
              if (lseek(fd, (off_t)fpos, SEEK_SET) >= 0) {
                const unsigned long want = (unsigned long)nrec_req * recsize;
                if (ah == 0x27) {  /* Block read: fill partial record area with ^Z like CP/M. */
                  memset((char*)mem + dta_linear, 0x1a, want < 0x8000 ? (unsigned)want : 0x8000);
                  { const int got = read(fd, (char*)mem + dta_linear, want < 0x8000 ? (unsigned)want : 0x8000);
                    if (got > 0) done_bytes = (unsigned long)got; }
                } else {  /* Block write. */
                  unsigned long remain = want;
                  while (remain) {
                    const int chunk = remain > 0x8000 ? 0x8000 : (int)remain;
                    const int got = write(fd, (const char*)mem + dta_linear + done_bytes, chunk);
                    if (got <= 0) break;
                    done_bytes += got;
                    remain -= got;
                  }
                }
              }
              { const unsigned nrec_done = (unsigned)((done_bytes + recsize - 1) / recsize);
                const unsigned long newrec = recnum + nrec_done;
                *(unsigned short*)&regs.rcx = (unsigned short)nrec_done;
                if (recsize >= 64) *(unsigned*)(fcb + 0x21) = (*(unsigned*)(fcb + 0x21) & 0xff000000U) | (newrec & 0xffffff);
                else *(unsigned*)(fcb + 0x21) = newrec;
                *(unsigned short*)(fcb + 0x0c) = (unsigned short)(newrec >> 7);
                fcb[0x20] = (char)(newrec & 0x7f);
                if (done_bytes) *(unsigned char*)&regs.rax = done_bytes == (unsigned long)nrec_req * recsize ? 0 : (ah == 0x27 ? 3 : 1);
              }
            } else if (nrec_req == 0) {
              *(unsigned char*)&regs.rax = 0;  /* Writing/reading 0 records succeeds. */
            }
  }   else if (ah == 0x29) {
  /* Parse filename for FCB. */
            const char * const p = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rsi);  /* !! Security: check bounds. */
            char *q = (char*)mem + ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rdi);  /* !! Security: check bounds. */
            const char *s = p;
            char *fn = q + 1;
            unsigned short new_si_ofs;
            char has_wild = 0;
            /* Permissive FCB parser for legacy MAKE/MSC tools. */
            while (*s == ' ' || *s == '\t') ++s;
            *q = '\0';  /* Drive: 0 (default). */
            memset(fn, ' ', 11);  /* Name(8)+Ext(3). */
            if (*s != '\0' && *s != '\r' && *s != '\n') {
              unsigned i = 0;
              while (*s != '\0' && *s != '\r' && *s != '\n' && *s != ' ' && *s != '\t' && *s != '.' && i < 8) {
                if (*s == '*' || *s == '?') has_wild = 1;
                fn[i++] = upper_ascii(*s++);
              }
              if (*s == '.') {
                unsigned j = 0;
                ++s;
                while (*s != '\0' && *s != '\r' && *s != '\n' && *s != ' ' && *s != '\t' && j < 3) {
                  if (*s == '*' || *s == '?') has_wild = 1;
                  fn[8 + j++] = upper_ascii(*s++);
                }
              }
              while (*s != '\0' && *s != '\r' && *s != '\n' && *s != ' ' && *s != '\t') ++s;
            }
            new_si_ofs = (unsigned short)(s - ((char*)mem + ((unsigned)sregs.ds.selector << 4)));
            *(unsigned short*)&regs.rsi = new_si_ofs;
            *(unsigned char*)&regs.rax = has_wild ? 1 : 0;  /* AL: wildcard present. */
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  } 
  return IA_NEXT;
}
