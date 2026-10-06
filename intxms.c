#include "kvikdos.h"
#include "intrun.h"

/* Largest contiguous free KiB in [64, pool_kb) excluding the reserved band
 * [res_lo,res_hi): free space is [64,res_lo) U [res_hi,pool_kb) minus allocs. */
static unsigned long xms_largest_free_kb(unsigned long pool_kb, unsigned long res_lo, unsigned long res_hi) {
  unsigned long largest = 0, i, j;
  for (i = 0; i <= XMS_HANDLE_COUNT; ++i) {  /* i==0: tail gap; i==COUNT: gap ending at res_lo; else gap ending at block i. */
    unsigned long gap_end = i == 0 ? pool_kb : (i == XMS_HANDLE_COUNT ? res_lo : xms_block_kb[i]);
    unsigned long gap_start = 64;  /* Above the HMA reserve. */
    if (i && i < XMS_HANDLE_COUNT && !xms_block_sizes_kb[i]) continue;
    for (j = 1; j < XMS_HANDLE_COUNT; ++j) {
      if (xms_block_sizes_kb[j]) {
        unsigned long be = xms_block_kb[j] + xms_block_sizes_kb[j];
        if (be > gap_start && be <= gap_end) gap_start = be;
      }
    }
    if (res_hi > gap_start && res_hi <= gap_end) gap_start = res_hi;  /* Reserved band bounds the gap. */
    if (gap_end > gap_start && gap_end - gap_start > largest) largest = gap_end - gap_start;
  }
  return largest;
}

int int43_dispatch(void) {
/* XMS entry point pseudo interrupt (from INT 2F AX=4310 ES:BX). */
        /* EMBs are KiB-granule offsets into xmem; offset 0..63 is reserved for the HMA. */
        const unsigned long xms_pool_kb = emu->xmem_size >> 10;
        /* Reserved band just below the top of the int15/88-reported extended
         * memory. DOS extenders such as Phar Lap 386|DOS-Extender park their
         * resident kernel and page tables there (they size memory only via the
         * <=64 MiB-int15/88 call) and assume XMS never hands out that phys.
         * Excluding it from the pool avoids a phys collision that otherwise
         * gets the kernel code pages reclaimed (zero-filled) -> fatal 10025. */
        const unsigned long i1588_kb = xms_pool_kb > 0xffffUL ? 0xffffUL : xms_pool_kb;
        const unsigned long res_hi = i1588_kb;
        const unsigned long res_lo = res_hi > XMS_KERNEL_RESERVE_KB ? res_hi - XMS_KERNEL_RESERVE_KB : res_hi;
        if (ah == 0x00) {  /* Get XMS version. */
          *(unsigned short*)&regs.rax = 0x0300;  /* XMS version 3.00. */
          *(unsigned short*)&regs.rbx = 0x0001;  /* Internal revision. */
          *(unsigned short*)&regs.rdx = 1;  /* HMA exists. */
        } else if (ah == 0x01 || ah == 0x02) {  /* Request/release HMA: always available (real memory). */
          *(unsigned short*)&regs.rax = 1;
          *(unsigned char*)&regs.rbx = 0;
        } else if (ah == 0x03 || ah == 0x05) {  /* Global/local A20 enable. */
          port_0x92_a20 = 1;
          *(unsigned short*)&regs.rax = 1;
          *(unsigned char*)&regs.rbx = 0;
        } else if (ah == 0x04 || ah == 0x06) {  /* Global/local A20 disable. */
          port_0x92_a20 = 0;
          *(unsigned short*)&regs.rax = 1;
          *(unsigned char*)&regs.rbx = 0;
        } else if (ah == 0x07) {  /* Query A20. */
          *(unsigned short*)&regs.rax = port_0x92_a20;
          *(unsigned char*)&regs.rbx = 0;
        } else if (ah == 0x08) {  /* Query free extended memory: AX=largest block, DX=total free. */
          unsigned long largest = xms_largest_free_kb(xms_pool_kb, res_lo, res_hi);
          *(unsigned short*)&regs.rax = largest > 0xffffUL ? 0xffff : (unsigned short)largest;
          *(unsigned short*)&regs.rdx = xms_free_kb > 0xffffUL ? 0xffff : (unsigned short)xms_free_kb;
          *(unsigned char*)&regs.rbx = 0;
          if (DEBUG || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "xms: query free -> largest=%uKB total=%uKB\n", (unsigned)*(unsigned short*)&regs.rax, (unsigned)*(unsigned short*)&regs.rdx);
        } else if (ah == 0x88) {  /* Query ANY free extended memory (XMS 3.0, >64 MiB): EAX=largest KB, EDX=total KB, ECX=highest ending phys addr. */
          *(unsigned*)&regs.rax = xms_largest_free_kb(xms_pool_kb, res_lo, res_hi);
          *(unsigned*)&regs.rdx = xms_free_kb;
          *(unsigned*)&regs.rcx = 0x100000U + (xms_pool_kb << 10);  /* Phys address of last byte of extended memory. */
          *(unsigned char*)&regs.rbx = xms_free_kb ? 0 : 0xa0;
          if (DEBUG || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "xms: query free(any) -> largest=%luKB total=%luKB top=%08lx\n", (unsigned long)*(unsigned*)&regs.rax, (unsigned long)*(unsigned*)&regs.rdx, (unsigned long)*(unsigned*)&regs.rcx);
        } else if (ah == 0x09 || ah == 0x89) {  /* Allocate (any) EMB: 0x09 size in DX (16-bit), 0x89 in EDX (32-bit, >64 MiB). */
          const unsigned long kb = ah == 0x89 ? *(unsigned*)&regs.rdx : *(unsigned short*)&regs.rdx;
          unsigned short hi;
          unsigned long cand, i;
          char fits;
          if (!kb || kb > xms_free_kb) {
            regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa0;  /* Out of space. */
            return IA_NEXT;
          }
          for (hi = 1; hi < XMS_HANDLE_COUNT; ++hi) if (xms_block_sizes_kb[hi] == 0) break;
          if (hi >= XMS_HANDLE_COUNT) {
            regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa1;  /* No handles. */
            return IA_NEXT;
          }
          /* First-fit search over [64, xms_pool_kb), skipping the reserved band. */
          cand = 64;
          fits = 0;
          for (;;) {
            unsigned long end = cand + kb;
            if (end > xms_pool_kb) break;
            if (res_lo < res_hi && cand < res_hi && end > res_lo) {  /* Overlaps reserved band: jump above it. */
              cand = res_hi;
              continue;
            }
            fits = 1;
            for (i = 1; i < XMS_HANDLE_COUNT; ++i) {
              if (xms_block_sizes_kb[i] && xms_block_kb[i] < end && cand < xms_block_kb[i] + xms_block_sizes_kb[i]) {
                cand = xms_block_kb[i] + xms_block_sizes_kb[i];
                fits = 0;
                break;
              }
            }
            if (fits) break;
          }
          if (!fits) {
            regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa0;  /* Out of contiguous space. */
          } else {
            xms_block_kb[hi] = cand;
            xms_block_sizes_kb[hi] = kb;
            xms_lock_counts[hi] = 0;
            xms_free_kb -= kb;
            *(unsigned short*)&regs.rdx = hi;
            regs.rax = 1;
            *(unsigned char*)&regs.rbx = 0;
            if (DEBUG || DIAG_ON(DIAG_BIT_EXEC)) fprintf(g_diag_file, "xms: alloc %uKB -> handle %u phys %05x\n", (unsigned)kb, hi, (unsigned)(0x100000 + (cand << 10)));
          }
        } else if (ah == 0x0a) {  /* Free EMB. */
          unsigned short hi = *(unsigned short*)&regs.rdx;
          if (hi == 0 || hi >= XMS_HANDLE_COUNT || !xms_block_sizes_kb[hi]) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa2;  /* Invalid handle. */
          } else {
            xms_free_kb += xms_block_sizes_kb[hi];
            xms_block_kb[hi] = 0;
            xms_block_sizes_kb[hi] = 0;
            xms_lock_counts[hi] = 0;
            *(unsigned short*)&regs.rax = 1;
            *(unsigned char*)&regs.rbx = 0;
          }
        } else if (ah == 0x0b) {  /* Move EMB. */
          const char *m = (const char*)mem + ((unsigned)sregs.ds.selector << 4) + (*(unsigned short*)&regs.rsi);
          unsigned long len = *(const unsigned long*)(const void*)(m + 0);
          unsigned short sh = *(const unsigned short*)(const void*)(m + 4);
          unsigned long so = *(const unsigned long*)(const void*)(m + 6);
          unsigned short dh = *(const unsigned short*)(const void*)(m + 10);
          unsigned long doff = *(const unsigned long*)(const void*)(m + 12);
          char *sp, *dp;
          if (len == 0) {
            *(unsigned short*)&regs.rax = 1;
            *(unsigned char*)&regs.rbx = 0;
            return IA_NEXT;
          }
          /* Handle 0: offset is a real-mode seg:off far pointer. Nonzero handle: byte offset into the EMB. */
          if (sh == 0) {
            unsigned lin = ((so >> 16) << 4) + (so & 0xffff);
            sp = lin + len <= GUEST_MEM_LIMIT ? (char*)mem + lin : NULL;
          } else if (sh < XMS_HANDLE_COUNT && xms_block_sizes_kb[sh] && so + len <= ((unsigned long)xms_block_sizes_kb[sh] << 10)) {
            sp = (char*)emu->xmem + ((xms_block_kb[sh] << 10) + so);
          } else sp = NULL;
          if (dh == 0) {
            unsigned lin = ((doff >> 16) << 4) + (doff & 0xffff);
            dp = lin + len <= GUEST_MEM_LIMIT ? (char*)mem + lin : NULL;
          } else if (dh < XMS_HANDLE_COUNT && xms_block_sizes_kb[dh] && doff + len <= ((unsigned long)xms_block_sizes_kb[dh] << 10)) {
            dp = (char*)emu->xmem + ((xms_block_kb[dh] << 10) + doff);
          } else dp = NULL;
          if (!sp || !dp || !emu->xmem) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa3;  /* Invalid source/dest. */
          } else {
            memmove(dp, sp, (size_t)len);
            *(unsigned short*)&regs.rax = 1;
            *(unsigned char*)&regs.rbx = 0;
          }
        } else if (ah == 0x0c) {  /* Lock EMB: returns the 32-bit guest physical address. */
          unsigned short hi = *(unsigned short*)&regs.rdx;
          if (hi == 0 || hi >= XMS_HANDLE_COUNT || !xms_block_sizes_kb[hi]) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa2;
          } else {
            unsigned long addr = 0x100000 + (xms_block_kb[hi] << 10);
            if (xms_lock_counts[hi] != 0xffff) ++xms_lock_counts[hi];
            *(unsigned short*)&regs.rbx = (unsigned short)addr;
            *(unsigned short*)&regs.rdx = (unsigned short)(addr >> 16);
            *(unsigned short*)&regs.rax = 1;
          }
        } else if (ah == 0x0d) {  /* Unlock EMB. */
          unsigned short hi = *(unsigned short*)&regs.rdx;
          if (hi == 0 || hi >= XMS_HANDLE_COUNT || !xms_block_sizes_kb[hi] || xms_lock_counts[hi] == 0) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xaa;  /* Not locked. */
          } else {
            --xms_lock_counts[hi];
            *(unsigned short*)&regs.rax = 1;
            *(unsigned char*)&regs.rbx = 0;
          }
        } else if (ah == 0x0e) {  /* Get EMB handle information. */
          unsigned short hi = *(unsigned short*)&regs.rdx, free_handles = 0, i;
          if (hi == 0 || hi >= XMS_HANDLE_COUNT || !xms_block_sizes_kb[hi]) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa2;
          } else {
            for (i = 1; i < XMS_HANDLE_COUNT; ++i) if (!xms_block_sizes_kb[i]) ++free_handles;
            *(unsigned short*)&regs.rax = 1;
            ((unsigned char*)&regs.rbx)[1] = (unsigned char)xms_lock_counts[hi];  /* BH lock count. */
            *(unsigned short*)&regs.rdx = xms_block_sizes_kb[hi];  /* Size in KiB. */
            *(unsigned char*)&regs.rbx = free_handles > 255 ? 255 : (unsigned char)free_handles;  /* BL free handles. */
          }
        } else if (ah == 0x0f) {  /* Reallocate EMB. */
          unsigned short hi = *(unsigned short*)&regs.rdx;
          unsigned short new_kb = *(unsigned short*)&regs.rbx, i;
          if (hi == 0 || hi >= XMS_HANDLE_COUNT || !xms_block_sizes_kb[hi] || !new_kb) {
            *(unsigned short*)&regs.rax = 0;
            *(unsigned char*)&regs.rbx = 0xa2;
          } else if (new_kb <= xms_block_sizes_kb[hi]) {  /* Shrink in place. */
            xms_free_kb += xms_block_sizes_kb[hi] - new_kb;
            xms_block_sizes_kb[hi] = new_kb;
            *(unsigned short*)&regs.rax = 1;
          } else {  /* Grow: only into the free gap right after the block. */
            unsigned long end = xms_block_kb[hi] + new_kb;
            char fits = end <= xms_pool_kb;
            for (i = 1; fits && i < XMS_HANDLE_COUNT; ++i) {
              if (xms_block_sizes_kb[i] && xms_block_kb[i] < end && xms_block_kb[hi] < xms_block_kb[i] + xms_block_sizes_kb[i]) fits = 0;
            }
            if (!fits || (unsigned long)(new_kb - xms_block_sizes_kb[hi]) > xms_free_kb) {
              *(unsigned short*)&regs.rax = 0;
              *(unsigned char*)&regs.rbx = 0xa0;
            } else {
              xms_free_kb -= new_kb - xms_block_sizes_kb[hi];
              xms_block_sizes_kb[hi] = new_kb;
              *(unsigned short*)&regs.rax = 1;
            }
          }
        } else if (ah == 0x10 || ah == 0x11) {  /* Request/release UMB: none provided. */
          *(unsigned short*)&regs.rax = 0;
          *(unsigned char*)&regs.rbx = 0xb0;
        } else {
          *(unsigned short*)&regs.rax = 0;
          *(unsigned char*)&regs.rbx = 0x80;  /* Function not implemented. */
        }
  return IA_NEXT;
}

