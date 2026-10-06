#include "kvikdos.h"
#include "intrun.h"



unsigned char run_dos_prog(struct EmuState *emu0, const char *prog_filename, const char *dpmi_host, const char *args_str, const char* const *args, DirState *dir_state0, TtyState *tty_state0, const EmuParams *emu_params0, const char* const *envp0, const char* const *extra_env, unsigned extra_env_count) {
  emu = emu0;
  dir_state = dir_state0;
  tty_state = tty_state0;
  emu_params = emu_params0;
  g_prog_filename = prog_filename;
  g_prog_args_str = args_str;
  g_prog_args = args;

  { struct SA { int StaticAssert_AllocParaLimits : DOS_ALLOC_PARA_LIMIT <= (DOS_MEM_LIMIT >> 4); }; }
  { struct SA { int StaticAssert_ShortSize : sizeof(short) == 2; }; }  /* Assumed by *(unsigned short*)... in many places. */
  { struct SA { int StaticAssert_IntSize : sizeof(int) == 4; }; }  /* Assumed by *(unsigned*)... in many places. */

  dpmi_host_active = (dpmi_host != NULL);
  preserve_low = 0;
  load_psp_para = PSP_PARA;
  if (dpmi_host_active) {  /* --dpmi=<dos-path>: resolve it like a DOS program name. */
    const char *r;
    if (dpmi_host[0] == '/' || access(dpmi_host, R_OK) == 0) {
      load_prog = dpmi_host;  /* Already a usable Linux path. */
    } else if (*(r = get_linux_filename_r(dpmi_host, dir_state, dpmi_linux_buf, NULL)) != '\0') {
      load_prog = r;
    } else {
      load_prog = dpmi_host;
    }
  } else {
    load_prog = prog_filename;
  }
  load_args = NULL;         /* A resident DPMI host takes no program args. */
  load_args_str = "";
  /* prog_filename points into the shared fnbuf, which DOS filename calls
   * (e.g. the host's device probes) overwrite while the host runs. Keep a
   * private copy so the target path survives the host->target reload.
   */
  strncpy(target_prog_buf, prog_filename ? prog_filename : "", sizeof(target_prog_buf) - 1);
  target_prog_buf[sizeof(target_prog_buf) - 1] = '\0';
  if (!dpmi_host_active) { load_args_str = args_str; load_args = args; }
  if (!load_prog) {
    img_fd = -1;
  } else if ((img_fd = open_with_case_fallback(load_prog, O_RDONLY, 0666)) < 0) {
    fprintf(stderr, "fatal: cannot open DOS executable program: %s: %s\n", load_prog, strerror(errno));
    exit(252);
  }
  dos_prog_abs = dir_state->dos_prog_abs;
  if (!dos_prog_abs) dos_prog_abs = "";
  dir_state->dos_prog_abs = NULL;  /* For security, use dos_prog_abs mapping only for read-only opens below. */

  video_write_step = 0;
  video_byte_written = 0;  /* Pacify uninitialized warnings. */
  stdout_write_p = NULL;  /* Pacify uninitialized warnings. */
  stdout_write_end = NULL;  /* Pacify uninitialized warnings. */
  dpmi_warned = 0;
  memset(xms_block_kb, 0, sizeof(xms_block_kb));
  memset(xms_block_sizes_kb, 0, sizeof(xms_block_sizes_kb));
  memset(xms_lock_counts, 0, sizeof(xms_lock_counts));
  umb_link_state = 0;
  memset(ems_pages_by_handle, 0, sizeof(ems_pages_by_handle));
  memset(ems_handle_base, 0, sizeof(ems_handle_base));
  ems_page_map[0] = ems_page_map[1] = ems_page_map[2] = ems_page_map[3] = 0;
  ems_phys_handle[0] = ems_phys_handle[1] = ems_phys_handle[2] = ems_phys_handle[3] = 0xffff;
  ems_total_pages = ems_free_pages = 256;  /* 4 MiB EMS in 16 KiB pages. */
  ems_pool_next = 0;
  port_0x92_a20 = 1;  /* A20 enabled by default; programs may toggle it. */
  port_0x70_index = 0;
  call_hlt_cs = call_hlt_ip = 0;
  cleanup_fn[0] = '\0';
  memset(fcb_lin_table, 0, sizeof(fcb_lin_table));
  { unsigned fi; for (fi = 0; fi < FCB_FILE_COUNT; ++fi) fcb_fd_table[fi] = -1; }
  current_psp_para = PSP_PARA;
  vid_tick = 0;
  vid_active = 0;  /* Each program starts in plain stdout mode. */
  vid_wrap_pend = 0;
  vid_cur_shape = -2;
  find_dirp = NULL;
  find_linux_dir[0] = '\0';
  find_dos_pattern[0] = '\0';
  find_attrs = 0;
  last_exec_return_code = 0;
  hlt_spin_count = 0;

 do_exec:
  header_size = detect_dos_executable_program(img_fd, load_prog, header);
  if (!preserve_low) {
    reset_emu(emu, emu_params);
    { const unsigned long xmem_kb = emu->xmem_size >> 10;
      const unsigned long i1588 = xmem_kb > 0xffffUL ? 0xffffUL : xmem_kb;
      const unsigned long reserve = i1588 > XMS_KERNEL_RESERVE_KB ? XMS_KERNEL_RESERVE_KB : 0;
      xms_free_kb = emu->xmem_size >= (64 << 10) ? xmem_kb - 64 - reserve : 0; }
    kvm_fds = emu->kvm_fds;
    mem = emu->mem;
    run = emu->kvm_run;
    /* Any read/write outside the regions above will trigger a KVM_EXIT_MMIO. */
    /* Fill magic interrupt table. */
    { unsigned u;
      for (u = 0; u < 0x100; ++u) { ((unsigned*)mem)[u] = MAGIC_INT_VALUE(u); }
      memset((char*)mem + (INT_HLT_PARA << 4), 0xf4, 0x100);  /* 256 hlt instructions, one for each int. TODO(pts): Is hlt+iret faster? */
      /* Far-callable XMS stub. It must live in the read-only first page at an
       * address no guest write can reach: 0x502 sits in the gap between the
       * BIOS data area (ends 0x500) and the int stubs (start 0x540), and is
       * outside every range the KVM_EXIT_MMIO write handler applies. The old
       * location 0x740 was inside the writable env area and got clobbered. */
      ((unsigned char*)mem)[0x502] = 0xcd;  /* int 0x43 */
      ((unsigned char*)mem)[0x503] = 0x43;
      ((unsigned char*)mem)[0x504] = 0xcb;  /* retf */
    }
    /* Populate the BIOS data area (0x400-0x500). It lives in the read-only
     * first page, so guest reads come straight from mem[]; guest writes to it
     * are applied via the KVM_EXIT_MMIO handler. Only the fields programs
     * actually probe are set.
     * https://stanislavs.org/helppc/bios_data_area.html
     */
    memset((char*)mem + 0x400, 0, 0x100);  /* Clear stale BDA on exec. */
    *(unsigned short*)((char*)mem + 0x410) = 0x21;  /* Equipment word: IPL from diskette, 80x25 color text. */
    *(unsigned short*)((char*)mem + 0x413) = DOS_MEM_LIMIT >> 10;  /* Base memory in KiB. */
    *(unsigned short*)((char*)mem + 0x41a) = 0x1e1e;  /* Keyboard buffer head=tail (empty). */
    *(unsigned short*)((char*)mem + 0x480) = 0x1e;   /* Keyboard buffer start offset. */
    *(unsigned short*)((char*)mem + 0x482) = 0x3e;   /* Keyboard buffer end offset. */
    ((char*)mem)[0x449] = 0x03;  /* Current video mode: 80x25 text. */
    *(unsigned short*)((char*)mem + 0x44a) = 80;     /* Columns. */
    *(unsigned short*)((char*)mem + 0x44c) = 0x1000; /* Video page size. */
    ((char*)mem)[0x460] = 0x0d;  /* Cursor end scan line. */
    ((char*)mem)[0x461] = 0x0e;  /* Cursor start scan line. */
    *(unsigned short*)((char*)mem + 0x463) = 0x3d4;  /* CRT controller base (color). */
    ((char*)mem)[0x484] = 24;    /* Rows - 1. */
    *(unsigned short*)((char*)mem + 0x485) = 16;     /* Character height. */
    ((char*)mem)[(INT_HLT_PARA << 4) - 1] = (char)0xcb;  /* `retf' opcode used by country case map. */
    memcpy((char*)mem + (PROGRAM_MCB_PARA << 4), default_program_mcb, 16);
    MCB_PID((char*)mem + (PROGRAM_MCB_PARA << 4)) = PSP_PARA;  /* Owner = the program's own PSP, per DOS convention. */
    /* DOS List-of-Lists (int 21h AH=52h) as an msdos_player-style dos_info_t. */
    init_dos_info(mem, emu->xmem_size);
    load_psp_para = PSP_PARA;
  }
  sregs = emu->initial_sregs;
  memset(&regs, '\0', sizeof(regs));

  /*memcpy(initial_sregs, &sregs, sizeof(sregs));*/  /* Not completely 0, but sregs.Xs.selector is 0. */
  sregs.fs.selector = sregs.gs.selector = ENV_PARA;  /* Random value after magic interrupt table. */

  current_psp_para = load_psp_para;
  { char *psp_args = load_dos_executable_program(img_fd, load_prog, mem, header, header_size, &regs, &sregs, &MCB_SIZE_PARA((char*)mem + ((load_psp_para - 1) << 4)), load_psp_para) + 0x80;
    if (load_args) {
      copy_args_to_dos_args(psp_args, load_args);
      load_args = NULL;  /* DOS exec() shouldn't copy them later. */
    } else {
      const unsigned size = strlen(load_args_str ? load_args_str : "");
      if (size > 0x7e) {  /* This shouldn't happen, that was checked before. */
        fprintf(stderr, "assert: exec command-line args too long\n");
        exit(252);
      }
      *psp_args++ = (char)size;
      memcpy(psp_args, load_args_str ? load_args_str : "", size);
      psp_args[size] = '\r';
    }
  }
  if (img_fd >= 0) close(img_fd);

  /* http://www.techhelpmanual.com/346-dos_environment.html */
  { char *env = (char*)mem + (ENV_PARA << 4), *env0 = env;
    char * const env_end = (char*)mem + ENV_LIMIT;
    char do_set_dos_path = 1;  /* This is smart, but an accasional chdir may ruin it: !(dos_prog_abs[0] == dir_state->drive && dos_prog_abs[1] == ':' && dos_prog_abs[2] == '\\' && strchr(dos_prog_abs + 3, '\\') == 0); */
    char do_clear_after_env = envp0 == NULL;
    if (do_clear_after_env) {
      while (*env++ != '\0') {
        if (DEBUG || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: reusing env var (%s)\n", env - 1);
        if (!(env = memchr(env, '\0', env_end - env))) {
          fprintf(stderr, "fatal: exec environment too large\n");
          exit(252);
        }
        ++env;
      }
    } else {
#if 0
      env = add_env(env, env_end, "PATH=D:\\foo;C:\\bar", 1);
      env = add_env(env, env_end, "heLLo=World!", 1);
#endif
      while (*envp0) {
        const char *host_var = *envp0++;
        const char *eq;
        if (!strchr(host_var, '=')) {
          if (DEBUG || DIAG_ON(DIAG_BIT_COMPAT)) fprintf(g_diag_file, "debug: skipping malformed host env var without '=': %s\n", host_var);
          continue;
        }
        eq = strchr(host_var, '=');
        if (eq && eq - host_var == 4 && strncmp(host_var, "PATH", 4) == 0) {
          const char *v = eq + 1;
          /* Don't import Unix PATH as DOS PATH. Keep DOS PATH auto-generated
           * from executable directory unless user provided a DOS-style PATH.
           */
          if (strchr(v, '/')) continue;
        }
        if (strncmp(host_var, "PATH=", 5) == 0) do_set_dos_path = 0;
        /* No attempt is made to deduplicate environment variables by name.
         * The user should supply unique names.
         */
        env = add_env(env, env_end, host_var, 1);
      }
      { unsigned ei;
        for (ei = 0; ei < extra_env_count; ++ei) {
          if (extra_env[ei]) env = add_env(env, env_end, extra_env[ei], 1);
        }
      }
      if (do_set_dos_path) {  /* Set %PATH% to the directory of dos_prog_abs. Set once. */
        size_t size;
        if (dos_prog_abs[0] == '\0') {
          size = 0;
        } else {
          const char *p = dos_prog_abs + strlen(dos_prog_abs);
          const char *p_base = dos_prog_abs + 3;
          for (; p != p_base && p[-1] != '\\'; --p) {}
          if (p != p_base) --p;
          if ((size_t)(env_end - env) < (size = p - dos_prog_abs) + 1 + 5) {
            fprintf(stderr, "fatal: DOS environment too long for PATH\n");
            exit(252);
          }
        }
        memcpy(env, "PATH=", 5);
        memcpy(env += 5, dos_prog_abs, size);
        env += size;
        *env++ = '\0';
      }
      envp0 = NULL;  /* DOS exec() won't copy them later. */
    }
    if (env == env0) env = add_env(env, env_end, "$=", 1);  /* Some programs such as pbc.exe would fail with an empty environment, so we create a fake variable. */
    env = add_env(env, env_end, "", 0);  /* Empty var marks end of env. */
    env = add_env(env, env_end, "\1", 0);  /* Number of subsequent variables (1). */
    if (dos_prog_abs[0] == '\0') dos_prog_abs = "C:\\KVIKPROG.COM";  /* Not the same as in default_program_mcb. */
    env = add_env(env, env_end, dos_prog_abs, 0);  /* Full program pathname. */
    if (do_clear_after_env) memset(env, '\0', env_end - env);
  }

/* We have to set both selector and base, otherwise it won't work. A `mov
 * ds, ax' instruction in the 16-bit KVM guest will set both. (FIX_SREG and
 * SET_SREG live in intrun.h.)
 */
  FIX_SREG(cs);
  FIX_SREG(ds);
  FIX_SREG(es);
  FIX_SREG(ss);
  FIX_SREG(fs);
  FIX_SREG(gs);

  if (emu_params->poke_word_count != 0) {
    unsigned i;
    for (i = 0; i < emu_params->poke_word_count; ++i) {
      unsigned linear = ((unsigned)emu_params->poke_word_segs[i] << 4) + emu_params->poke_word_ofs[i];
      if (linear + 2 > DOS_MEM_LIMIT) {
        fprintf(stderr, "fatal: --poke-word address out of DOS memory\n");
        exit(252);
      }
      *(unsigned short*)((char*)mem + linear) = emu_params->poke_word_values[i];
    }
  }

  if (emu_params->call_near_enabled || emu_params->call_far_enabled) {
    unsigned i;
    unsigned short new_sp;
    unsigned stack_linear;
    unsigned frame_words = emu_params->call_far_enabled ? 2 : 1;

    /* Build the exact stack a near CALL (or far CALLF for --call-far) would
     * have produced: [SP] is the return IP ([SP+2] the return CS for far
     * calls) and the following words are C/Pascal 16-bit arguments in source
     * order. Small-model game code expects DS/ES to be DGROUP after the C
     * runtime starts; callers can provide that selector with --call-ds. */
    if (emu_params->call_cs_enabled) {
      SET_SREG(cs, emu_params->call_cs);
    }
    if (emu_params->call_ss_enabled) {
      SET_SREG(ss, emu_params->call_ss);
      regs.rsp = emu_params->call_sp;
    }
    if (emu_params->call_ds_enabled) {
      SET_SREG(ds, emu_params->call_ds);
      SET_SREG(es, emu_params->call_ds);
    } else {
      SET_SREG(ds, sregs.ss.selector);
      SET_SREG(es, sregs.ss.selector);
    }
    if (emu_params->call_set_mask) {
      if (emu_params->call_set_mask & (1U << 0)) regs.rax = emu_params->call_set_regs[0];
      if (emu_params->call_set_mask & (1U << 1)) regs.rcx = emu_params->call_set_regs[1];
      if (emu_params->call_set_mask & (1U << 2)) regs.rdx = emu_params->call_set_regs[2];
      if (emu_params->call_set_mask & (1U << 3)) regs.rbx = emu_params->call_set_regs[3];
      if (emu_params->call_set_mask & (1U << 4)) regs.rsi = emu_params->call_set_regs[4];
      if (emu_params->call_set_mask & (1U << 5)) regs.rdi = emu_params->call_set_regs[5];
      if (emu_params->call_set_mask & (1U << 6)) regs.rbp = emu_params->call_set_regs[6];
    }
    call_hlt_cs = sregs.cs.selector;
    call_hlt_ip = (unsigned short)regs.rip;
    ((unsigned char*)mem)[sregs.cs.base + call_hlt_ip] = 0xf4;  /* HLT sentinel after RET. */
    new_sp = (unsigned short)((unsigned short)regs.rsp - (unsigned short)(2 * (emu_params->call_arg_count + frame_words)));
    stack_linear = sregs.ss.base + new_sp;
    if (stack_linear + 2 * (emu_params->call_arg_count + frame_words) > DOS_MEM_LIMIT) {
      fprintf(stderr, "fatal: --call-near/--call-far stack frame out of DOS memory\n");
      exit(252);
    }
    *(unsigned short*)((char*)mem + stack_linear) = call_hlt_ip;
    if (emu_params->call_far_enabled) {
      *(unsigned short*)((char*)mem + stack_linear + 2) = call_hlt_cs;
    }
    for (i = 0; i < emu_params->call_arg_count; ++i) {
      *(unsigned short*)((char*)mem + stack_linear + 2 * frame_words + i * 2) = emu_params->call_args[i];
    }
    regs.rsp = new_sp;
    if (emu_params->call_far_enabled) {
      SET_SREG(cs, emu_params->call_far_seg);
      regs.rip = emu_params->call_far_ip;
    } else {
      regs.rip = emu_params->call_near_ip;
    }
  }

  *(unsigned short*)&regs.rflags |= 1 << 1;  /* Reserved bit in EFLAGS. */
  /**(unsigned short*)&regs.rflags |= 1 << 9;*/  /* IF=1, enable interrupts. */

  had_get_ints = 0;  /* 1 << 0: int 0x00; 1 << 1: int 0x18; 1 << 2: int 0x06, 1 << 3: Get DOS version, 1 << 4: 0x34. */
  tasm30_bitset = 0;
  tick_count = 0;
  sphinx_cmm_flags = 0;
  ctrl_break_checking = 0;
  dir_state->linux_prog = load_prog;
  dta_seg_ofs = 0x80 | load_psp_para << 16;
  ongoing_set_int = 0;  /* No set_int operation ongoing. */
  last_dos_error_code = 0;
  port_0x40_tick = 0;
  is_stdout_write_cursor = 0;
  malloc_strategy = MS_BEST_FIT;  /* Doesn't matter which. */

  if (DEBUG || DIAG_ON(DIAG_BIT_VERBOSE)) dump_regs("debug", &regs, &sregs);

  /* !! Security: close all filehandles except for 0, 1, 2 and kvm_fds, so that read and write from DOS won't be able to touch them. */

 set_sregs_regs_and_continue:
  if (ioctl(kvm_fds.vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
    perror("fatal: KVM_SET_SREGS");
    exit(252);
  }
  if (ioctl(kvm_fds.vcpu_fd, KVM_SET_REGS, &regs) < 0) {
    perror("fatal: KVM_SET_REGS\n");
    exit(252);
  }

  /* !! Trap it if it tries to enter protected mode (cr0 |= 1). Is this possible? */
  for (;;) {
    int ret = ioctl(kvm_fds.vcpu_fd, KVM_RUN, 0);
    if (ret < 0) {
      fprintf(stderr, "KVM_RUN failed");
      exit(252);
    }
    if (ioctl(kvm_fds.vcpu_fd, KVM_GET_REGS, &regs) < 0) {
      perror("fatal: KVM_GET_REGS");
      exit(252);
    }
    if (ioctl(kvm_fds.vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
      perror("fatal: KVM_GET_REGS");
      exit(252);
    }
    if (DEBUG || DIAG_ON(DIAG_BIT_VERBOSE)) dump_regs("debug", &regs, &sregs);

    if (run->exit_reason != KVM_EXIT_HLT) hlt_spin_count = 0;
    if (++vid_tick >= 512) { vid_tick = 0; vid_render(mem); }  /* Periodic repaint of the guest text screen. */
    switch (run->exit_reason) {
     case KVM_EXIT_IO:
      if (io_dispatch() == IA_FATAL) goto fatal;
      break;
     case KVM_EXIT_DEBUG:
      break;
     case KVM_EXIT_SHUTDOWN:  /* How do we trigger it? */
      fprintf(stderr, "fatal: shutdown\n");
      exit(252);
     case KVM_EXIT_HLT:
      if (sregs.cs.selector == INT_HLT_PARA && (unsigned)((unsigned)regs.rip - 1) < 0x100) {  /* hlt caused by int through our magic interrupt table. */
        int_num = ((unsigned)regs.rip - 1) & 0xff;
        csip_ptr = (unsigned short*)((char*)mem + ((unsigned)sregs.ss.selector << 4) + (*(unsigned short*)&regs.rsp));  /* !! What if rsp wraps around 64 KiB boundary? Test it. Also calculate int_cs again. */
        int_ip = csip_ptr[0]; int_cs = csip_ptr[1];  /* Return address. */  /* !! Security: check bounds, also check that rsp <= 0xfffe. */
        ah = ((unsigned)regs.rax >> 8) & 0xff;
        if (DEBUG || DEBUG_INT || DIAG_ON(DIAG_BIT_INT)) fprintf(g_diag_file, "debug: int 0x%02x ah:%02x al:%02x cs:%04x ip:%04x\n", int_num, ah, (unsigned char)regs.rax, int_cs, int_ip);
        fflush(stdout);
        (void)ah;
        /* Documentation about DOS and BIOS int calls: https://stanislavs.org/helppc/idx_interrupt.html */
        switch (int_dispatch()) {
        case IA_NEXT:
          goto done_int_call;
        case IA_EXEC:
          goto do_exec;
        case IA_EXIT:
          return dos_exit();
        case IA_FATAL:
          goto fatal;
        case IA_FATAL_UIC:
          if (!emu_params->strict_mode) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned short*)&regs.rflags |= 1 << 0;
            goto done_int_call;
          }
          fprintf(stderr, "fatal: unsupported int 0x%02x ax:%04x\n", int_num, *(const unsigned short*)&regs.rax);
          goto fatal_int_fallback;
        case IA_FATAL_INT:
         fatal_int_fallback:
          if (!emu_params->strict_mode) {
            *(unsigned char*)&regs.rax = 0;
            *(unsigned short*)&regs.rflags |= 1 << 0;
            goto done_int_call;
          }
          fprintf(stderr, "fatal: unsupported int 0x%02x ah:%02x cs:%04x ip:%04x\n", int_num, ah, int_cs, int_ip);
          goto fatal;
        }
       done_int_call:
        /* Return from the interrupt. */
        SET_SREG(cs, int_cs);
        regs.rip = int_ip;
        if (csip_ptr[2] & (1 << 9)) *(unsigned short*)&regs.rflags |= (1 << 9);  /* Set IF back to 1 if it was 1. */
        *(unsigned short*)&regs.rsp += 6;  /* pop ip, pop cs, pop flags. */
        goto set_sregs_regs_and_continue;
      } else {  /* hlt instruction in user code. */
        if (emu_params->hlt_dump_filename) {
          const int fd = open(emu_params->hlt_dump_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
          if (fd >= 0) {
            if (write(fd, mem, GUEST_MEM_LIMIT) != GUEST_MEM_LIMIT) {
              fprintf(stderr, "error: error writing to --hlt-dump=... file: %s\n", emu_params->hlt_dump_filename);
            }
            close(fd);
          } else {
            fprintf(stderr, "error: error opening --hlt-dump=... file for writing: %s\n", emu_params->hlt_dump_filename);
          }
        }
        if ((emu_params->call_near_enabled || emu_params->call_far_enabled) &&
            sregs.cs.selector == call_hlt_cs &&
            (unsigned short)(regs.rip - 1) == call_hlt_ip) {
          maybe_dump_guest_mem(mem, GUEST_MEM_LIMIT);
          fprintf(stdout,
                  "kvikdos-call-result cs=%04x ip=%04x ax=%04x bx=%04x cx=%04x dx=%04x si=%04x di=%04x sp=%04x bp=%04x flags=%04x ds=%04x es=%04x ss=%04x\n",
                  sregs.cs.selector, (unsigned short)regs.rip,
                  (unsigned short)regs.rax, (unsigned short)regs.rbx,
                  (unsigned short)regs.rcx, (unsigned short)regs.rdx,
                  (unsigned short)regs.rsi, (unsigned short)regs.rdi,
                  (unsigned short)regs.rsp, (unsigned short)regs.rbp,
                  (unsigned short)regs.rflags, sregs.ds.selector,
                  sregs.es.selector, sregs.ss.selector);
          return (unsigned char)regs.rax;
        }
        if (sregs.cs.selector >= PSP_PARA && (emu_params->is_hlt_ok || !emu_params->strict_mode)) {
          /* The 8253 timer chip increments the counter in each 1 / 1193182s
           * causing IRQ0 at each 65536th increment. kvikdos doesn't implement
           * any of this, but now we wait that approximate amount for `hlt' to
           * wake up.
           */
          if (*(unsigned short*)&regs.rflags & (1 << 9)) {  /* IF == 1. */
            if (++hlt_spin_count >= 400) {
              fprintf(stderr, "error: guest stalled in HLT loop, aborting in permissive mode.\n");
              return 1;
            }
            usleep(54925);  /* 54925 =~= 1000000.0 / (1193182.0 / 65536). */
          } else {
            if (++hlt_spin_count >= 2000) {
              fprintf(stderr, "error: guest stalled in HLT loop (IF=0), aborting in permissive mode.\n");
              return 1;
            }
            usleep(1000);  /* Prevent busy loop if guest issues tight hlt with IF=0. */
          }
          break;
        } else {
          fprintf(stderr, "fatal: unexpected hlt\n");
          goto fatal;
        }
      }
     case KVM_EXIT_MMIO:
      if (mmio_dispatch() == IA_FATAL) goto fatal;
      break;
     case KVM_EXIT_INTERNAL_ERROR:
      fprintf(stderr, "fatal: KVM internal error suberror=%u\n", (unsigned)run->internal.suberror);
      /* We get this for an int call if we don't map
       * (KVM_SET_USER_MEMORY_REGION) or initialize the interrupt table
       * properly. However, we can't continue the emulation, because KVM_RUN
       * will return the same error again. !! Can we fix it?
       */
      /* if (run->internal.suberror == KVM_INTERNAL_ERROR_DELIVERY_EV && p[0] == (char)0xcd) {...} */
      goto fatal;
     default:
      fprintf(stderr, "fatal: unexpected KVM exit: reason=%u\n", run->exit_reason);
      goto fatal;
    }
  }
 fatal:
  dump_regs("fatal", &regs, &sregs);
#if 0  /* The Linux kernel does this at process exit. */
  close(kvm_fds.vcpu_fd);
  close(kvm_fds.vm_fd);
  close(kvm_fds.kvm_fd);
#endif
  exit(252);
  return 0;  /* Not reached. This is just to pacity owcc. */
}
