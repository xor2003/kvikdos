#include "kvikdos.h"
#include "intrun.h"

/* DOS int 21h services: exec group. Returns an IA_* action. */
int i21_exec(void) {
  if (ah == 0x4c) {
  /* Exit to DOS. */
            if (cleanup_fn[0] != '\0') unlink(get_linux_filename(cleanup_fn));
            return IA_EXIT;
  }   else if (ah == 0x31) {
  /* Terminate-and-stay-resident (TSR): DX = paragraphs to keep. */
            if (dpmi_host_active) {  /* The resident DPMI host stays; load the real target above it. */
              const unsigned host_psp = load_psp_para;
              char * const host_mcb = (char*)mem + (host_psp << 4) - 16;
              const unsigned host_old_size = MCB_SIZE_PARA(host_mcb);
              const char host_old_type = MCB_TYPE(host_mcb);
              const unsigned res_para0 = (unsigned short)regs.rdx;
              const unsigned res_para = res_para0 > host_old_size ? host_old_size : res_para0;
              const unsigned res_end0 = host_psp + res_para;  /* MCB para right after the shrunk host block. */
              unsigned res_end = res_end0;  /* Top of the resident region. */
              unsigned prev_size = res_para, target_psp_para;
              char *target_mcb;
              /* Extend res_end above any extra blocks the host still owns (AH=48).
                 The host block itself is skipped: after the shrink its end is res_end0. */
              { unsigned bp = PSP_PARA;
                for (;;) {
                  const char *m = (const char*)mem + (bp << 4) - 16;
                  unsigned end;
                  if (is_mcb_bad(mem, bp)) break;
                  end = bp + MCB_SIZE_PARA(m);
                  if (MCB_PID(m) != 0 && bp != host_psp && end > res_end) { res_end = end; prev_size = MCB_SIZE_PARA(m); }
                  if (MCB_TYPE(m) == 'Z') break;
                  bp += 1 + MCB_SIZE_PARA(m);
                }
              }
              if (res_end + 0x20 >= DOS_ALLOC_PARA_LIMIT) {  /* Not enough room for the target. */
                fprintf(stderr, "fatal: resident DPMI host leaves no conventional memory\n");
                return IA_FATAL;
              }
              /* Resize the host block to its resident footprint; it is no longer last. */
              MCB_TYPE(host_mcb) = 'M';
              MCB_SIZE_PARA(host_mcb) = res_para;
              if (res_end > res_end0 && host_old_type == 'M' && res_para < host_old_size) {
                /* Still-used blocks above keep the old chain alive; bridge the gap left
                   by the shrink with a free MCB (like the realloc-split does). */
                char * const fmcb = (char*)mem + (res_end0 << 4);
                char * const onext = (char*)mem + ((host_psp + host_old_size) << 4);
                const unsigned fsize = host_old_size - res_para - 1;
                memcpy(fmcb, default_program_mcb, 16);
                MCB_TYPE(fmcb) = 'M';
                MCB_PID(fmcb) = 0;
                MCB_PSIZE_PARA(fmcb) = res_para;
                MCB_SIZE_PARA(fmcb) = fsize;
                if (MCB_PID(onext) == 0) {  /* Merge with the old (free) next block. */
                  const char ntype = MCB_TYPE(onext);
                  char * const nn = onext + 16 + (MCB_SIZE_PARA(onext) << 4);
                  MCB_SIZE_PARA(fmcb) += 1 + MCB_SIZE_PARA(onext);
                  MCB_TYPE(fmcb) = ntype;
                  memset(onext, 0, 16);
                  if (ntype != 'Z') MCB_PSIZE_PARA(nn) = MCB_SIZE_PARA(fmcb);
                } else {
                  MCB_PSIZE_PARA(onext) = fsize;
                }
              }
              /* The target's MCB sits at res_end; its block (PSP) is the next para. */
              target_mcb = (char*)mem + (res_end << 4);
              memcpy(target_mcb, default_program_mcb, 16);
              MCB_TYPE(target_mcb) = 'Z';
              target_psp_para = res_end + 1;
              MCB_PID(target_mcb) = (unsigned short)target_psp_para;  /* Owner = the program's own PSP, per DOS convention. */
              MCB_PSIZE_PARA(target_mcb) = (unsigned short)prev_size;
              /* The target's image/.bss/stack must start zeroed (loader relies on it). */
              memset((char*)mem + (target_psp_para << 4), 0, DOS_MEM_LIMIT - (target_psp_para << 4));
              /* Switch to the real target and re-run the loader, preserving low memory. */
              load_prog = target_prog_buf;
              load_args = g_prog_args;
              load_args_str = g_prog_args_str;
              load_psp_para = target_psp_para;
              preserve_low = 1;
              dpmi_host_active = 0;
              if (DEBUG || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "debug: DPMI host resident to 0x%04x, target PSP at 0x%04x\n", res_end, target_psp_para);
              if ((img_fd = open_with_case_fallback(load_prog, O_RDONLY, 0666)) < 0) {
                fprintf(stderr, "fatal: cannot open DOS executable program: %s: %s\n", load_prog, strerror(errno));
                exit(252);
              }
              return IA_EXEC;
            }
            return IA_EXIT;  /* A lone program's TSR is just an exit for us. */
  }   else if (ah == 0x4b) {
  /* Load or execute program (exec). */
            const unsigned char al = (unsigned char)regs.rax;
            const char * const dos_filename = (char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rdx);  /* !! Security: check bounds. */
            if (al == 0 || al == 3) {  /* Microsoft Macro Assembler 6.00B driver masm.exe uses it with al == 3. */
              const char * const params = (char*)mem + ((unsigned)sregs.es.selector << 4) + (*(unsigned short*)&regs.rbx);  /* !! Security: check bounds. */
              const unsigned short load_para = al != 0 ? ((unsigned short*)params)[0] : 0;
              const unsigned short relocation_factor = al != 0 ? ((unsigned short*)params)[1] : 0;
              char * const psp = (al != 0 && load_para >= PSP_PARA + 0x10 && load_para < DOS_ALLOC_PARA_LIMIT) ? (char*)mem + ((unsigned)(load_para - 0x10) << 4) : NULL;
              const unsigned short env_para = al == 0 ? (((unsigned short*)params)[0] ? ((unsigned short*)params)[0] : ENV_PARA) : psp ? *(const unsigned short*)(psp + 0x2c) : 0;
              char * const env = ((al == 0 && env_para == ENV_PARA) || (env_para >= PSP_PARA + 0x10 && env_para < DOS_ALLOC_PARA_LIMIT)) ? (char*)mem + (env_para << 4) : NULL;
              const char *env_end = env ? env + (((PROGRAM_MCB_PARA - ENV_PARA < DOS_ALLOC_PARA_LIMIT - env_para) ? PROGRAM_MCB_PARA - ENV_PARA : DOS_ALLOC_PARA_LIMIT - env_para) << 4) : NULL;
              char * const args_raw = al == 0 ?  (char*)mem + (((unsigned short*)params)[2] << 4) + ((unsigned short*)params)[1]
                                    : psp ? psp + 0x80 : NULL;  /* DOS command tail in PSP format at [0x80]=len. */
              char args_buf[0x80];
              unsigned char args_size = 0;
              const char *safe_args = "";
              char is_args_ok = 0;
              const char is_dos_filename_high = sregs.ds.selector + (*(unsigned short*)&regs.rdx >> 4) >= PSP_PARA;  /* So that dos_filename won't overlap new_env below. */
              char *new_env;
              char new_prog_drive;
              int reason = 0;
              if (args_raw) {
                args_size = (unsigned char)args_raw[0];
                if (args_size < 0x7f && (args_raw[1 + args_size] == '\0' || args_raw[1 + args_size] == '\r' || args_raw[1 + args_size] == '\n')) {  /* PSP-style command tail. */
                  memcpy(args_buf, args_raw + 1, args_size);
                  args_buf[args_size] = '\0';
                  safe_args = args_buf;
                  is_args_ok = 1;
                } else {  /* Fallback: direct CR/NUL-terminated string pointer. */
                  const char *p = args_raw;
                  args_size = 0;
                  while (args_size < 0x7f && p[args_size] != '\0' && p[args_size] != '\r' && p[args_size] != '\n') ++args_size;
                  memcpy(args_buf, p, args_size);
                  args_buf[args_size] = '\0';
                  safe_args = args_buf;
                  is_args_ok = 1;
                }
              }
              if (!((al == 3 && is_dos_filename_high) || (al == 0 && env && is_dos_filename_high))) {
                fprintf(stderr, "fatal: bounds check failed (env_ok=%d args_ok=%d, fn_ok=%d) env when loading program=(%s) with g_prog_args=(%s)\n",
                        env != NULL, is_args_ok, is_dos_filename_high,
                        dos_filename, safe_args);
                return IA_FATAL_INT;
              }
              if (DEBUG || DEBUG_EXEC || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "debug: exec: al:%02x reason=%d program=(%s) g_prog_args=(%s)\n", al, reason, dos_filename, safe_args);
              /* Even with al == 0, the correct behavior would be resuming
               * execution of the parent proess (e.g. Borland C++ 2.0
               * compiler bcc.exe, Power C 2.2.0 pc.exe) after the child
               * process (e.g. TLINK 4.0 linker tlink.exe) has finished (and
               * then e.g. print the ``Available memory'' message and remove
               * the `turboc.$ln' file). However, kvikdos is not smart enough
               * for that, so it just does an exec() and forgets about the
               * parent process.
               * TODO(pts): Return to the parent on child exit (needs a real
               * process model with a saved parent context).
               */
              if (al == 3) {
                char ovl_dos[LINUX_PATH_SIZE];
                dir_state->dos_prog_abs = dos_prog_abs;  /* Allow loading overlay via mounted alias path. */
                g_prog_filename = get_linux_filename_r(dos_filename, dir_state, exec_fnbuf, NULL);
                dir_state->dos_prog_abs = NULL;  /* For security. */
                if (g_prog_filename[0] == '\0' && dos_prog_abs[0] &&
                    !strchr(dos_filename, ':') && !strchr(dos_filename, '\\') && !strchr(dos_filename, '/')) {  /* NOLINT(clang-analyzer-core.NonNullParamChecker): dos_filename is a guest pointer derived from mem, never NULL. */
                  const char *base = dos_prog_abs + strlen(dos_prog_abs);
                  for (; base != dos_prog_abs + 3 && base[-1] != '\\'; --base) {}
                  if (base > dos_prog_abs + 3) {
                    size_t dir_size = base - dos_prog_abs;
                    size_t fn_size = strlen(dos_filename);
                    if (dir_size + fn_size + 1 < sizeof(ovl_dos)) {
                      memcpy(ovl_dos, dos_prog_abs, dir_size);
                      memcpy(ovl_dos + dir_size, dos_filename, fn_size + 1);
                      dir_state->dos_prog_abs = dos_prog_abs;
                      g_prog_filename = get_linux_filename_r(ovl_dos, dir_state, exec_fnbuf, NULL);
                      dir_state->dos_prog_abs = NULL;
                    }
                  }
                }
                if (g_prog_filename[0] == '\0') {
                  *(unsigned short*)&regs.rax = 2;  /* File not found. */
                  return dos_error_21();
                }
                if ((img_fd = open_with_case_fallback(g_prog_filename, O_RDONLY, 0666)) < 0) {
                  *(unsigned short*)&regs.rax = get_dos_error_code(errno, 0x02);
                  return dos_error_21();
                }
                header_size = detect_dos_executable_program(img_fd, g_prog_filename, header);
                if (load_dos_overlay_program(img_fd, g_prog_filename, mem, header, header_size, load_para, relocation_factor) != 0) {
                  close(img_fd);
                  *(unsigned short*)&regs.rax = get_dos_error_code(errno, 0x1f);
                  return dos_error_21();
                }
                close(img_fd);
                *(unsigned short*)&regs.rax = 0;
                *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
                return IA_NEXT;
              }
              reason = 0;
              if (reason > 0) {
                if (al == 3) {
                  /* Program load probe (without execute): report failure to caller,
                   * don't terminate parent tool. LINK.EXE may probe optional helpers
                   * such as DOSXNT.EXE and continue without them.
                   */
                  *(unsigned short*)&regs.rax = 2;  /* File not found. */
                  return dos_error_21();
                }
                fprintf(stderr, "fatal: unsupported program to load: al:%02x reason=%d program=(%s) g_prog_args=(%s)\n", al, reason, dos_filename, safe_args);
                return IA_FATAL_INT;
              }
              if (al == 0) {
                const char *dos_exec_name = dos_filename;
                char dos_exec_buf[DOS_PATH_SIZE + 4];
                if ((dos_filename[0] & ~32) - 'A' + 0U < DRIVE_COUNT && dos_filename[1] == ':' &&
                    (dos_filename[2] == '\\' || dos_filename[2] == '/')) {
                  char requested_drive = dos_filename[0] & ~32;
                  if (!dir_state->linux_mount_dir[requested_drive - 'A']) {
                    char active_drive = dir_state->drive;
                    if ((active_drive - 'A' + 0U) < DRIVE_COUNT && dir_state->linux_mount_dir[active_drive - 'A']) {
                      size_t abs_tail_size = strlen(dos_filename + 2);
                      /* Some toolchains probe absolute paths on an unmapped drive.
                       * Retry same absolute tail on the active mounted drive.
                       */
                      if (abs_tail_size + 2 < sizeof(dos_exec_buf)) {
                        dos_exec_buf[0] = active_drive;
                        memcpy(dos_exec_buf + 1, dos_filename + 1, abs_tail_size + 1);
                        dos_exec_name = dos_exec_buf;
                      }
                    }
                  }
                }
                if (!strchr(dos_filename, ':') && !strchr(dos_filename, '\\') && !strchr(dos_filename, '/')) {
                  char path_copy[1024];
                  const char *dos_path_env = getenv_prefix_block_nocase("PATH=", env, env_end);
                  const char *resolved_prog;
                  char resolved_drive = '\0';
                  path_copy[0] = '\0';
                  if (dos_path_env) {
                    strncpy(path_copy, dos_path_env, sizeof(path_copy) - 1);
                    path_copy[sizeof(path_copy) - 1] = '\0';
                  }
                  resolved_prog = find_prog_on_path(dos_filename, dir_state, path_copy[0] ? path_copy : NULL, &resolved_drive);
                  if (resolved_prog && resolved_prog[0]) {
                    const char *resolved_dos = get_dos_abs_filename_r(resolved_prog, resolved_drive, dir_state, dos_exec_buf);
                    if (resolved_dos[0]) dos_exec_name = resolved_dos;
                  } else if (dos_prog_abs[0]) {
                    const char *base = dos_prog_abs + strlen(dos_prog_abs);
                    const char *tail = dos_filename;
                    size_t dir_size, tail_size;
                    for (; base > dos_prog_abs + 3 && base[-1] != '\\'; --base) {}
                    dir_size = (size_t)(base - dos_prog_abs);
                    tail_size = strlen(tail);
                    if (dir_size && dir_size + tail_size + 1 < sizeof(dos_exec_buf)) {
                      memcpy(dos_exec_buf, dos_prog_abs, dir_size);
                      memcpy(dos_exec_buf + dir_size, tail, tail_size + 1);
                      dos_exec_name = dos_exec_buf;
                    }
                  }
                }
                if (run_dos_child_subprocess(dos_exec_name, safe_args, env, env_end, dir_state, &last_exec_return_code) != 0) {
                  *(unsigned short*)&regs.rax = get_dos_error_code(errno, 0x1f);  /* General failure. */
                  return dos_error_21();
                }
                *(unsigned short*)&regs.rax = 0;
                *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
                return IA_NEXT;
              }
              if (reason == -1 && cleanup_fn[0] == '\0' && safe_args[0] == '@' && strlen(safe_args) <= sizeof(cleanup_fn)) {
                strcpy(cleanup_fn, safe_args + 1);  /* Example g_prog_args: "@turboc.$ln". */
                if (DEBUG || DIAG_ON(DIAG_BIT_FS)) fprintf(g_diag_file, "debug: will remove file at exit: %s\n", cleanup_fn);
              }
              dir_state->dos_prog_abs = dos_prog_abs;  /* For loading the overlay from g_prog_filename, even if not mounted. */
              g_prog_filename = get_linux_filename_r(dos_filename, dir_state, exec_fnbuf, NULL);
              dir_state->dos_prog_abs = NULL;  /* For security. */
              new_prog_drive = get_dos_filename_drive(dos_filename, dir_state);
              if (g_prog_filename[0] == '\0' || new_prog_drive == '\0') {
                fprintf(stderr, "fatal: bad program filename for loading: %s\n", dos_filename);
                return IA_FATAL_INT;
              }
              if ((img_fd = open_with_case_fallback(g_prog_filename, O_RDONLY, 0666)) < 0) {
                /*return dos_err_linux();*/  /* We don't know how to report the error properly here (it's not a normal int 0x21 call. */
                fprintf(stderr, "fatal: cannot open DOS executable program for loading: %s: %s\n", g_prog_filename, strerror(errno));
                return IA_FATAL_INT;
              }
              *(char*)env_end = '\0';  /* Hide counter for absolute program pathname. */
              memcpy(new_env = (char*)mem + (ENV_PARA << 4), env, env_end + 2 - env);
              strcpy(fnbuf2, safe_args);  /* Large enough to hold 0x7f bytes. */
              g_prog_args_str = fnbuf2;
              dos_prog_abs = get_dos_abs_filename_r(g_prog_filename, new_prog_drive, dir_state, dosfnbuf);
              if (DEBUG || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "debug: exec g_prog_filename=(%s) dos_prog_abs=(%s) dos_prog_drive=%c\n", g_prog_filename, dos_prog_abs, new_prog_drive);
              if (dos_prog_abs[0] == '\0') {
                fprintf(stderr, "fatal: error getting DOS absolute filename for exec on drive %c: %s\n", new_prog_drive, g_prog_filename);
                exit(252);
              }
              load_prog = g_prog_filename;  /* A fresh exec reloads at PSP_PARA; no resident host survives. */
              load_args = NULL;
              load_args_str = g_prog_args_str;
              load_psp_para = PSP_PARA;
              preserve_low = 0;
              dpmi_host_active = 0;
              return IA_EXEC;
            } else {
              /* Compatibility: some tools probe other EXEC modes. */
              *(unsigned short*)&regs.rax = 1;  /* Invalid function number. */
              return dos_error_21();
            }
          ;
  } 
  return IA_NEXT;
}
