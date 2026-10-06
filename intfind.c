#include "kvikdos.h"
#include "intrun.h"

/* DOS int 21h services: find group. Returns an IA_* action. */
int i21_find(void) {
  if (ah == 0x4e) {
  /* Find first matching file (findfirst). */
            const unsigned short attrs = *(unsigned short*)&regs.rcx;
            const char * const pattern = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            const unsigned dta_linear = (dta_seg_ofs & 0xffff) + (dta_seg_ofs >> 16 << 4);
            const char *dos_pat_base;
            size_t dos_pat_dir_size;
            char dos_pat_prefix[DOS_PATH_SIZE];
            const char *linux_probe, *linux_dir;
            struct dirent *de;
            if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: findfirst pattern=(%s) attrs=0x%04x\n", pattern, attrs);
            if (!is_linear_byte_user_writable(dta_linear) || !is_linear_byte_user_writable(dta_linear + 0x2b - 1)) return dos_err_ax(0x57);
            if (attrs & 8) {  /* Volume label requested. */
             no_more_files:
              *(unsigned short*)&regs.rax = 0x12;  /* No more files. */
              return dos_error_21();
            }
            if (find_dirp) { closedir(find_dirp); find_dirp = NULL; }
            dos_pat_base = get_dos_basename(pattern);
            if (!is_dos_filename_83(dos_pat_base)) goto no_more_files;
            dos_pat_dir_size = dos_pat_base - pattern;
            if (dos_pat_dir_size >= sizeof(dos_pat_prefix) - 2) goto no_more_files;
            memcpy(dos_pat_prefix, pattern, dos_pat_dir_size);
            dos_pat_prefix[dos_pat_dir_size] = 'A';  /* Probe filename to resolve parent directory. */
            dos_pat_prefix[dos_pat_dir_size + 1] = '\0';
            linux_probe = get_linux_filename(dos_pat_prefix);
            if (linux_probe[0] == '\0') goto no_more_files;
            linux_dir = get_linux_basename(linux_probe);
            memcpy(find_linux_dir, linux_probe, linux_dir - linux_probe);
            find_linux_dir[linux_dir - linux_probe] = '\0';
            if (find_linux_dir[0] == '\0') strcpy(find_linux_dir, ".");
            find_dirp = opendir(find_linux_dir);
            if (!find_dirp) {
              if (errno == ENOENT) goto no_more_files;
              return dos_err_linux();
            }
            strncpy(find_dos_pattern, dos_pat_base, sizeof(find_dos_pattern) - 1);
            find_dos_pattern[sizeof(find_dos_pattern) - 1] = '\0';
            find_attrs = attrs;
            while ((de = readdir(find_dirp)) != NULL) {
              char fn[LINUX_PATH_SIZE];
              const char *fnb = de->d_name;
              char *dta;
              struct stat st;
              struct tm *tm;
              if (!is_dos_filename_83(fnb) || !dos_wildcard_match(find_dos_pattern, fnb)) continue;
              if (snprintf(fn, sizeof(fn), "%s/%s", find_linux_dir, fnb) <= 0 || strlen(fn) >= sizeof(fn)) continue;
              if (stat(fn, &st) != 0) continue;
              if (S_ISDIR(st.st_mode) && !(find_attrs & 0x10)) continue;
              dta = (char*)mem + dta_linear;
              memset(dta, '\0', 0x16);
              tm = localtime(&st.st_mtime);
              *(unsigned*)dta = FINDFIRST_MAGIC;
              *(unsigned short*)(dta + 0x16) = tm->tm_sec >> 1 | tm->tm_min << 5 | tm->tm_hour << 11;
              *(unsigned short*)(dta + 0x18) = tm->tm_mday | (tm->tm_mon + 1) << 5 | (tm->tm_year - 1980) << 9;
              *(unsigned*)(dta + 0x1a) = (sizeof(st.st_size) > 4 && st.st_size >> (32 * (sizeof(st.st_size) > 4))) ?
                  0xffffffffU : st.st_size + (size_t)0;
              { const char *p = fnb; char *q = dta + 0x1e, c;
                do { c = *p++; *q++ = upper_ascii(c); } while (c != '\0');
              }
              *(unsigned short*)&regs.rax = 0;
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              goto done_findnext;
            }
            closedir(find_dirp);
            find_dirp = NULL;
            goto no_more_files;
           done_findnext:;
            *(unsigned short*)&regs.rax = 0;  /* Undocumented, but necessary and used as a success indicator by the VAL 1995-05-27 linker val.exe. DOSBox also sets it. */
  }   else if (ah == 0x4f) {
  /* Find next matching file (findnext). */
            const unsigned dta_linear = (dta_seg_ofs & 0xffff) + (dta_seg_ofs >> 16 << 4);
            if (!is_linear_byte_user_writable(dta_linear) || !is_linear_byte_user_writable(dta_linear + 0x2b - 1)) return dos_err_ax(0x57);
            { char * const dta = (char*)mem + dta_linear;
              struct dirent *de;
              if (*(unsigned*)dta != FINDFIRST_MAGIC || !find_dirp) return dos_err_ax(0x57);
              while ((de = readdir(find_dirp)) != NULL) {
                char fn[LINUX_PATH_SIZE];
                const char *fnb = de->d_name;
                struct stat st;
                struct tm *tm;
                if (!is_dos_filename_83(fnb) || !dos_wildcard_match(find_dos_pattern, fnb)) continue;
                if (snprintf(fn, sizeof(fn), "%s/%s", find_linux_dir, fnb) <= 0 || strlen(fn) >= sizeof(fn)) continue;
                if (stat(fn, &st) != 0) continue;
                if (S_ISDIR(st.st_mode) && !(find_attrs & 0x10)) continue;
                memset(dta, '\0', 0x16);
                tm = localtime(&st.st_mtime);
                *(unsigned*)dta = FINDFIRST_MAGIC;
                *(unsigned short*)(dta + 0x16) = tm->tm_sec >> 1 | tm->tm_min << 5 | tm->tm_hour << 11;
                *(unsigned short*)(dta + 0x18) = tm->tm_mday | (tm->tm_mon + 1) << 5 | (tm->tm_year - 1980) << 9;
                *(unsigned*)(dta + 0x1a) = (sizeof(st.st_size) > 4 && st.st_size >> (32 * (sizeof(st.st_size) > 4))) ?
                    0xffffffffU : st.st_size + (size_t)0;
                { const char *p = fnb; char *q = dta + 0x1e, c;
                  do { c = *p++; *q++ = upper_ascii(c); } while (c != '\0');
                }
                *(unsigned short*)&regs.rax = 0;
                *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
                goto done_findnext2;
              }
              closedir(find_dirp);
              find_dirp = NULL;
              goto no_more_files;
             done_findnext2:;
            }
  } 
  return IA_NEXT;
}
