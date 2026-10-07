#include "kvikdos.h"
#include "intrun.h"

/* DOS int 21h services: mem group. Returns an IA_* action. */
int i21_mem(void) {
  if (ah == 0x4a) {
  /* Modify allocated memory block (inplace_realloc()). */
            const unsigned new_size_para = *(unsigned short*)&regs.rbx;
            const unsigned short block_para = sregs.es.selector;
            unsigned available_para, old_size_para;
            char * const mcb = (char*)mem + (block_para << 4) - 16;
            char *next_mcb;
            if (is_mcb_bad(mem, block_para) || MCB_PID(mcb) == 0) {
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: inplace_realloc bad block_para=0x%04x new_size_para=0x%04x bad=%d pid=%04x\n", block_para, new_size_para, (int)is_mcb_bad(mem, block_para), MCB_PID(mcb));
             error_bad_mcb:
              /*fprintf(stderr, "fatal: bad MCB\n"); return IA_FATAL;*/
              *(unsigned short*)&regs.rax = 7;  /* Memory control blocks destroyed. */ /* !! anasm.com reports this. From where? */
              return dos_error_21();
            }
            if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: inplace_realloc block_para=0x%04x new_size_para=0x%04x old_size_para=0x%04x\n", block_para, new_size_para, MCB_SIZE_PARA(mcb));
            DEBUG_CHECK_ALL_MCBS(mem);
            old_size_para = MCB_SIZE_PARA(mcb);
            if (old_size_para != new_size_para) {
              next_mcb = MCB_TYPE(mcb) != 'Z' ? (mcb + 16 + (old_size_para << 4)) : NULL;
              if (next_mcb && is_mcb_bad(mem, block_para + 1 + old_size_para)) goto error_bad_mcb;
              available_para = !next_mcb ? (unsigned)(DOS_ALLOC_PARA_LIMIT - block_para) : MCB_PID(next_mcb) != 0 ? old_size_para : old_size_para + 1 + MCB_SIZE_PARA(next_mcb);
              if (new_size_para > available_para) {
                *(unsigned short*)&regs.rbx = available_para;
                if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: inplace_realloc insufficient memory\n");
               error_insufficient_memory:
                *(unsigned short*)&regs.rax = 8;  /* Insufficient memory. */
                return dos_error_21();
              }
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: inplace_realloc block_para=0x%04x new_size_para=0x%04x available_para=0x%04x\n", block_para, new_size_para, available_para);
              if (!next_mcb) {
                if (new_size_para < old_size_para) {  /* Shrink the last block: DOS keeps the whole conventional arena chained, so carve a free 'Z' tail (ALLOC.ASM $SETBLOCK/alloc_get_size splits and the remainder keeps 'Z'). */
                  char * const free_mcb = mcb + 16 + (new_size_para << 4);
                  memcpy(free_mcb, default_program_mcb, 16);
                  MCB_TYPE(free_mcb) = 'Z';
                  MCB_PID(free_mcb) = 0;
                  MCB_SIZE_PARA(free_mcb) = old_size_para - new_size_para - 1;
                  MCB_PSIZE_PARA(free_mcb) = new_size_para;
                  MCB_TYPE(mcb) = 'M';
                }
                MCB_SIZE_PARA(mcb) = new_size_para;
              } else if (MCB_PID(next_mcb) != 0) {  /* Insert a free block after the current block. */
                char * const free_mcb = mcb + 16 + (new_size_para << 4);
                memcpy(free_mcb, default_program_mcb, 16);
                MCB_TYPE(free_mcb) = 'M';
                MCB_PID(free_mcb) = 0;  /* Mark as free. */
                MCB_SIZE_PARA(free_mcb) = MCB_PSIZE_PARA(next_mcb) = old_size_para - new_size_para - 1;
                MCB_PSIZE_PARA(free_mcb) = MCB_SIZE_PARA(mcb) = new_size_para;
              } else if (new_size_para == available_para) {  /* Exact size match. Merge the following free block into the current block. */
                const char tail = MCB_TYPE(next_mcb);
                const unsigned next_mcb_size_para = MCB_SIZE_PARA(next_mcb);
                memset(next_mcb, 0, 16);
                next_mcb = next_mcb + 16 + (next_mcb_size_para << 4);
                if (tail == 'Z') MCB_TYPE(mcb) = 'Z';  /* Absorbed the last block: current becomes last. */
                else MCB_PSIZE_PARA(next_mcb) = available_para;
                MCB_SIZE_PARA(mcb) = available_para;
              } else {  /* Make the following free block smaller or larger. */
                const char tail = MCB_TYPE(next_mcb);
                char * const next_mcb2 = mcb + 16 + (new_size_para << 4);
                memcpy(next_mcb2, default_program_mcb, 16);
                MCB_TYPE(next_mcb2) = tail;  /* Remainder keeps the old signature ('Z' stays last). */
                MCB_PID(next_mcb2) = 0;  /* Mark as free. */
                MCB_SIZE_PARA(next_mcb2) = MCB_SIZE_PARA(next_mcb) + old_size_para - new_size_para;
                if (tail != 'Z') MCB_PSIZE_PARA(next_mcb + 16 + (MCB_SIZE_PARA(next_mcb) << 4)) = MCB_SIZE_PARA(next_mcb2);
                MCB_PSIZE_PARA(next_mcb2) = MCB_SIZE_PARA(mcb) = new_size_para;
                memset(next_mcb, 0, 16);
              }
              if (is_mcb_bad(mem, block_para)) {
                fprintf(stderr, "fatal: bad MCB after inplace_realloc()\n");
                exit(252);
              }
              /* A 'Z' result is the chain's last block: there is no next
               * MCB to validate (checking it would read past the arena). */
              if (MCB_TYPE(mcb) != 'Z' && is_mcb_bad(mem, available_para = block_para + 1 + MCB_SIZE_PARA(mcb))) {
                fprintf(stderr, "fatal: bad next/free MCB after inplace_realloc(): %d\n", is_mcb_bad(mem, available_para));
                exit(252);
              }
            }
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x48) {
  /* Allocate memory (malloc()). */
            const unsigned alloc_size_para = *(unsigned short*)&regs.rbx;
            if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc(0x%04x)\n", alloc_size_para);
            /*DEBUG_CHECK_ALL_MCBS(mem);*/  /* No need, the is_mcb_bad calls below do all the checks. */
            {
              unsigned fit_waste_para = (unsigned)-1;
              unsigned fit_block_para = 0;
              unsigned fit_prev_block_para = 0;  /* Preceding block. */
              unsigned largest_available_para = 0;
              { /* Try to find best match. */
                unsigned block_para = PSP_PARA, prev_block_para = 0;
                for (;;) {
                  const char * const mcb = (const char*)mem + (block_para << 4) - 16;
                  unsigned size_para;
                  if (is_mcb_bad(mem, block_para)) goto error_bad_mcb;
                  if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc find block=0x%04x...0x%04x size=0x%04x psize=0x%04x mcb_type=%c is_used=%d\n", block_para, block_para + MCB_SIZE_PARA(mcb), MCB_SIZE_PARA(mcb), MCB_PSIZE_PARA(mcb), MCB_TYPE(mcb), MCB_PID(mcb) != 0);
                  size_para = MCB_SIZE_PARA(mcb);
                  if (MCB_TYPE(mcb) == 'Z' && MCB_PID(mcb) != 0) {  /* Last block is in use; the space after it is an implicit tail (only reachable if the chain lacks a free tail). */
                    prev_block_para = block_para;
                    block_para += 1 + size_para;
                    if (block_para == DOS_ALLOC_PARA_LIMIT + 1) break;  /* There is nothing after the last block. */
                    size_para = DOS_ALLOC_PARA_LIMIT - block_para;
                    goto try_fit;
                  } else if (MCB_PID(mcb) == 0) {  /* A free block (including a free 'Z' last block). */
                   try_fit:
                    if (size_para >= alloc_size_para) {
                      const unsigned waste_para = malloc_strategy == MS_FIRST_FIT ? block_para : malloc_strategy == MS_BEST_FIT ? size_para - alloc_size_para : /* malloc_strategy >= MS_LAST_FIT ? */ ~block_para;
                      if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc fit prev_block=0x%04x block=0x%04x waste=0x%04x\n", prev_block_para, block_para, waste_para);
                      if (waste_para < fit_waste_para) {
                        fit_waste_para = waste_para;
                        fit_block_para = block_para;
                        fit_prev_block_para = prev_block_para;
                      }
                    } else if (size_para > largest_available_para) {
                      largest_available_para = size_para;
                    }
                    if (MCB_TYPE(mcb) == 'Z' || MCB_PID(mcb) != 0) break;  /* Chain ends here ('Z'), or the used-'Z' implicit tail jumped in above. */
                  }
                  prev_block_para = block_para;
                  block_para += 1 + size_para;
                }
              }
              if (fit_waste_para == (unsigned)-1) {
                *(unsigned short*)&regs.rbx = largest_available_para - (largest_available_para > 0);
                if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc insufficient memory\n");
                goto error_insufficient_memory;
              } else {
                char * const prev_mcb = (char*)mem + (fit_prev_block_para << 4) - 16;
                char * const mcb = (char*)mem + (fit_block_para << 4) - 16;
                char * const free_mcb = (char*)mem + ((fit_block_para + alloc_size_para) << 4);
                char mcb_error;
                if (MCB_TYPE(prev_mcb) == 'Z') {  /* Append after last block. */
                  if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) {
                    fprintf(g_diag_file, "debug: malloc append prev_block=0x%04x block=0x%04x free=0x%04x strategy=%u\n",
                            fit_prev_block_para, fit_block_para, fit_block_para + alloc_size_para + 1, malloc_strategy);
                  }
                  memcpy(mcb, default_program_mcb, 16);
                  /*MCB_TYPE(mcb) = 'Z';*/  /* Already set. */
                  /*MCB_PID(mcb) = PROCESS_ID;*/  /* Already set. */
                  MCB_TYPE(prev_mcb) = 'M';
                  MCB_PSIZE_PARA(mcb) = MCB_SIZE_PARA(prev_mcb);
                  if (malloc_strategy != MS_LAST_FIT ||
                     DOS_ALLOC_PARA_LIMIT - fit_block_para == alloc_size_para) {  /* Perfect fit, no need to split. */
                    MCB_SIZE_PARA(mcb) = alloc_size_para;
                    goto malloc_done;
                  }
                  /* Create free block, split it below. At this point we have enough paras to do a split. */
                  MCB_SIZE_PARA(mcb) = DOS_ALLOC_PARA_LIMIT - fit_block_para;
                  MCB_PID(mcb) = 0;  /* Mark it as free. */
                  MCB_PSIZE_PARA(mcb) = MCB_SIZE_PARA(prev_mcb);
                }
                {  /* Change existing free block. */
                  char * const next_mcb = mcb + (MCB_SIZE_PARA(mcb) << 4) + 16;
                  MCB_PID(mcb) = PROCESS_ID;  /* Mark as in use. */
                  if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) {
                    fprintf(g_diag_file, "debug: malloc middle prev_block=0x%04x block=0x%04x next=0x%04x free=0x%04x is_exact_fit=%d strategy=%u\n",
                            fit_prev_block_para, fit_block_para, fit_block_para + MCB_SIZE_PARA(mcb) + 1, fit_block_para + alloc_size_para + 1,
                            fit_block_para + MCB_SIZE_PARA(mcb) + 1 == fit_block_para + alloc_size_para + 1, malloc_strategy);
                  }
                  if (free_mcb == next_mcb || fit_block_para + alloc_size_para + 1 >= DOS_ALLOC_PARA_LIMIT) {  /* Exact fit, or a remainder too small to hold a free-tail MCB at the arena top: DOS merges it into the allocated block. */
                    if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc exact fit\n");
                  } else if (malloc_strategy == MS_LAST_FIT) {  /* Not an exact fit, prepend a free block. */
                    char * const after_mcb = mcb + ((MCB_SIZE_PARA(mcb) - alloc_size_para) << 4);
                    memcpy(after_mcb, default_program_mcb, 16);  /* 'Z' (last) by default. */
                    MCB_SIZE_PARA(after_mcb) = alloc_size_para;
                    if (MCB_TYPE(mcb) != 'Z' && fit_block_para + alloc_size_para < DOS_ALLOC_PARA_LIMIT) MCB_PSIZE_PARA(next_mcb) = alloc_size_para;
                    MCB_PSIZE_PARA(after_mcb) = MCB_SIZE_PARA(mcb) -= alloc_size_para + 1;
                    MCB_TYPE(after_mcb) = MCB_TYPE(mcb);
                    MCB_PID(mcb) = 0;  /* Free. */
                    MCB_TYPE(mcb) = 'M';  /* Non-last. */
                    mcb_error = is_mcb_bad(mem, fit_block_para);
                    if (mcb_error) {  /* mcb, which is free now. */
                      fprintf(stderr, "fatal: bad pre-free MCB after malloc(): %d\n", mcb_error);
                      exit(252);
                    }
                    fit_block_para += MCB_SIZE_PARA(mcb) + 1;
                    if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc last block=0x%04x\n", fit_block_para);
                  } else {  /* Not an exact fit, append a free block. */
                    const unsigned size_para = MCB_SIZE_PARA(mcb);
                    const char tail = MCB_TYPE(mcb);  /* 'Z' when this free block is the chain's last. */
                    memcpy(free_mcb, default_program_mcb, 16);
                    MCB_TYPE(mcb) = 'M';  /* A remainder follows the used part, so it is never last (a free 'Z' becomes 'M' here). */
                    MCB_PSIZE_PARA(free_mcb) = MCB_SIZE_PARA(mcb) = alloc_size_para;
                    /* MCB_PSIZE_PARA(mcb) is already correct. */
                    MCB_TYPE(free_mcb) = tail;  /* Remainder keeps the old signature ('Z' stays last). */
                    MCB_PID(free_mcb) = 0;
                    MCB_SIZE_PARA(free_mcb) = size_para - alloc_size_para - 1;
                    if (tail != 'Z') MCB_PSIZE_PARA(next_mcb) = MCB_SIZE_PARA(free_mcb);
                    mcb_error = is_mcb_bad(mem, fit_block_para + alloc_size_para + 1);
                    if (mcb_error) {  /* free_mcb. */
                      fprintf(stderr, "fatal: bad free MCB after malloc(): %d\n", mcb_error);
                      exit(252);
                    }
                  }
                }
               malloc_done:
                MCB_PID((char*)mem + (fit_block_para << 4) - 16) = (unsigned short)current_psp_para;  /* DOS stamps the MCB owner with the caller's current PSP (int 21h AH=50h), needed by extenders that juggle sub-PSPs. */
                mcb_error = is_mcb_bad(mem, fit_block_para);
                if (mcb_error) {
                  fprintf(stderr, "fatal: bad MCB after malloc(): %d\n", mcb_error);
                  exit(252);
                }
              }
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: malloc(0x%04x) == 0x%04x\n", alloc_size_para, fit_block_para);
              *(unsigned short*)&regs.rax = fit_block_para;  /* Insufficient memory. */
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            }
  }   else if (ah == 0x49) {
  /* Free allocated memory (free()). */
            const unsigned block_para = (unsigned short)sregs.es.selector;
            char *mcb = (char*)mem + (block_para << 4) - 16;
            if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free(0x%04x)\n", block_para);
            DEBUG_CHECK_ALL_MCBS(mem);
            if (block_para == PSP_PARA) {  /* It's not allowed to free the program image. */
              return dos_err_ax(0x57);
            } else if (block_para > PSP_PARA && block_para < DOS_ALLOC_PARA_LIMIT && mcb[0] == freed_mcb[0] && memcmp(mcb, freed_mcb, 16) == 0) {  /* Already free, has been freed. Succeed as noop just like DOSBox 0.74 and MS-DOS 6.22 do. */
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: already freed\n");
            } else if (is_mcb_bad(mem, block_para)) {
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: bad MCB para=0x%04x: %d\n", block_para, is_mcb_bad(mem, block_para));
              goto error_bad_mcb;
            } else if (MCB_PID(mcb) == 0) {  /* Already free. Succeed as noop just like DOSBox 0.74 and MS-DOS 6.22 do. */
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: already free\n");
            } else if (is_mcb_bad(mem, block_para - MCB_PSIZE_PARA(mcb) - 1)) {
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: bad prev MCB para=0x%04x: %d\n", block_para - MCB_PSIZE_PARA(mcb) - 1, is_mcb_bad(mem, block_para - MCB_PSIZE_PARA(mcb) - 1));
              goto error_bad_mcb;
            } else if (MCB_TYPE(mcb) != 'Z' && is_mcb_bad(mem, block_para + MCB_SIZE_PARA(mcb) + 1)) {
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) {
                const unsigned np = block_para + MCB_SIZE_PARA(mcb) + 1;
                const char *nm = (char*)mem + (np << 4) - 16;
                fprintf(g_diag_file, "debug: free: bad next MCB para=0x%04x: %d type=%c pid=%04x size=%04x psize=%04x\n", np, is_mcb_bad(mem, np), MCB_TYPE(nm), MCB_PID(nm), MCB_SIZE_PARA(nm), MCB_PSIZE_PARA(nm));
              }
              goto error_bad_mcb;
            } else {
              char *prev_mcb = mcb - 16 - (MCB_PSIZE_PARA(mcb) << 4);  /* Always exists since block_para != PSP_PARA. */
              char *next_mcb = mcb + 16 + (MCB_SIZE_PARA(mcb) << 4);
              if (MCB_TYPE(mcb) != 'Z' && MCB_PID(next_mcb) == 0) {  /* Merge it with the following free block. */
                char *next_mcb2 = next_mcb + 16 + (MCB_SIZE_PARA(next_mcb) << 4);
                const unsigned next_para2 = block_para + MCB_SIZE_PARA(mcb) + 1 + MCB_SIZE_PARA(next_mcb) + 1;
                const char next_type = MCB_TYPE(next_mcb);
                if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: merge with next free\n");
                if (next_type != 'Z' && is_mcb_bad(mem, next_para2)) {
                  if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: bad next2 MCB block_para=%04x next_para=0x%04x next_para2=0x%04x: %d\n", block_para, block_para + MCB_SIZE_PARA(mcb) + 1, next_para2, is_mcb_bad(mem, next_para2));
                  goto error_bad_mcb;
                }
                MCB_SIZE_PARA(mcb) += 1 + MCB_SIZE_PARA(next_mcb);
                memset(next_mcb, 0, 16);
                if (next_type != 'Z') MCB_PSIZE_PARA(next_mcb2) = MCB_SIZE_PARA(mcb);
                MCB_TYPE(mcb) = next_type;
              }
              MCB_PID(mcb) = 0;  /* Mark it as free. */
              if (MCB_PID(prev_mcb) == 0) {  /* Merge it with the preceding free block. */
                const char mcb_type = MCB_TYPE(mcb);
                if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free: merge with prev free\n");
                MCB_SIZE_PARA(prev_mcb) += 1 + MCB_SIZE_PARA(mcb);
                memcpy(mcb, freed_mcb, 16);
                if (mcb_type != 'Z') MCB_PSIZE_PARA(next_mcb) = MCB_SIZE_PARA(prev_mcb);
                MCB_TYPE(prev_mcb) = mcb_type;
                mcb = prev_mcb;
              }
              /* A free 'Z' last block stays: the DOS arena always ends with an MCB, so we never delete it (allocators merge/split it like any free block). */
            }
            if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: free(0x%04x) OK\n", block_para);
            DEBUG_CHECK_ALL_MCBS(mem);
            *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
  }   else if (ah == 0x58) {
  /* Get/set memory allocation strategy. */
            const unsigned char al = (unsigned char)regs.rax;
            if (al == 0x00) {  /* Get. */
              *(unsigned short*)&regs.rax = (unsigned short)malloc_strategy;
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else if (al == 0x01) {  /* Set. */
              /* Programs compiled by Borland C++ 5.02 compiler bcc.exe set it with BX == MS_LAST_FIT, and return ``Out of memory'' if not implemented correctly. */
              /* See mallocs.nasm for a test of MS_LAST_FIT functionality. */
              *(unsigned short*)&regs.rax = *(unsigned short*)&regs.rbx;
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
              malloc_strategy = *(unsigned short*)&regs.rbx;
              if (DEBUG || DEBUG_ALLOC || DIAG_ON(DIAG_BIT_VERBOSE)) fprintf(g_diag_file, "debug: set malloc strategy=%u\n", malloc_strategy);
            } else if (al == 0x02) {  /* Get UMB link state. */
              *(unsigned short*)&regs.rax = (unsigned short)(unsigned char)umb_link_state;
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else if (al == 0x03) {  /* Set UMB link state. */
              const unsigned short bx = *(unsigned short*)&regs.rbx;
              if (bx > 1) return dos_err_ax(0x57);
              umb_link_state = (char)bx;
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else if (al == 0x04) {  /* Get strategy + UMB link state. */
              *(unsigned short*)&regs.rbx = (unsigned short)(malloc_strategy | ((unsigned)(unsigned char)umb_link_state << 7));
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else if (al == 0x05) {  /* Set strategy + UMB link state. */
              const unsigned short bx = *(unsigned short*)&regs.rbx;
              malloc_strategy = bx & 0x3f;
              umb_link_state = !!(bx & 0x80);
              *(unsigned short*)&regs.rflags &= ~(1 << 0);  /* CF=0. */
            } else {
              return dos_err_ax(0x57);
            }
  } 
  return IA_NEXT;
}
