#include "kvikdos.h"
#include "intrun.h"

int int67_dispatch(void) {
/* Various. */
        const unsigned char al = (unsigned char)regs.rax;
        if ((unsigned short)regs.rax == 0xde00) {  /* VCPI installation check. */
          /* Doing nothing means it's not installed. */
        } else if (ah == 0x40) {  /* Get EMM status. */
          ((unsigned char*)&regs.rax)[1] = 0;  /* AH=0 success. */
        } else if (ah == 0x41) {  /* Get page frame segment. */
          ((unsigned char*)&regs.rax)[1] = 0;  /* AH=0 success. */
          *(unsigned short*)&regs.rbx = 0xe000;  /* Conventional EMS page frame. */
        } else if (ah == 0x42) {  /* Get number of pages. */
          ((unsigned char*)&regs.rax)[1] = 0;  /* AH=0 success. */
          *(unsigned short*)&regs.rbx = ems_free_pages;
          *(unsigned short*)&regs.rdx = ems_total_pages;
        } else if (ah == 0x43) {  /* Allocate pages. */
          unsigned short req = *(unsigned short*)&regs.rbx, hi;
          if (!req || req > ems_free_pages || (unsigned)ems_pool_next + req > ems_total_pages) {
            ((unsigned char*)&regs.rax)[1] = 0x88;  /* Insufficient pages. */
          } else {
            for (hi = 1; hi < EMS_HANDLE_COUNT; ++hi) if (ems_pages_by_handle[hi] == 0) break;
            if (hi >= EMS_HANDLE_COUNT) {
              ((unsigned char*)&regs.rax)[1] = 0x85;  /* No handles. */
            } else {
              if (!emu->ems_pool && !(emu->ems_pool = (char*)calloc((size_t)ems_total_pages << 14, 1))) {
                ((unsigned char*)&regs.rax)[1] = 0x80;  /* Internal error. */
              } else {
                ems_pages_by_handle[hi] = req;
                ems_handle_base[hi] = ems_pool_next;
                ems_pool_next += req;
                ems_free_pages -= req;
                *(unsigned short*)&regs.rdx = hi;
                ((unsigned char*)&regs.rax)[1] = 0;
              }
            }
          }
        } else if (ah == 0x44) {  /* Map page (or unmap if BX=0xffff). */
          unsigned short logical = *(unsigned short*)&regs.rbx;
          unsigned short phys = *(unsigned short*)&regs.rdx;
          unsigned short handle = *(unsigned short*)&regs.rsi;
          if (phys >= 4) {
            ((unsigned char*)&regs.rax)[1] = 0x8a;  /* Invalid physical page. */
          } else if (logical == 0xffff) {  /* Unmap. */
            if (hv_set_memory(hv, 3 + phys, 0xe0000 + ((unsigned long)phys << 14), 0, NULL, 0) < 0) {
              perror("fatal: hv_set_memory ems unmap");
              exit(252);
            }
            ems_page_map[phys] = 0;
            ems_phys_handle[phys] = 0xffff;
            ((unsigned char*)&regs.rax)[1] = 0;
          } else if (handle >= EMS_HANDLE_COUNT || ems_pages_by_handle[handle] == 0) {
            ((unsigned char*)&regs.rax)[1] = 0x83;  /* Invalid handle. */
          } else if (logical >= ems_pages_by_handle[handle]) {
            ((unsigned char*)&regs.rax)[1] = 0x8a;  /* Invalid logical page. */
          } else if (!emu->ems_pool) {
            ((unsigned char*)&regs.rax)[1] = 0x80;
          } else {
            const unsigned pool_page = ems_handle_base[handle] + logical;
            if (hv_set_memory(hv, 3 + phys, 0xe0000 + ((unsigned long)phys << 14), 0x4000, emu->ems_pool + ((unsigned long)pool_page << 14), 0) < 0) {
              perror("fatal: hv_set_memory ems map");
              exit(252);
            }
            ems_page_map[phys] = pool_page + 1;
            ems_phys_handle[phys] = handle;
            ((unsigned char*)&regs.rax)[1] = 0;
          }
        } else if (ah == 0x45) {  /* Release handle. */
          unsigned short handle = *(unsigned short*)&regs.rdx, phys;
          if (handle == 0 || handle >= EMS_HANDLE_COUNT || ems_pages_by_handle[handle] == 0) {
            ((unsigned char*)&regs.rax)[1] = 0x83;
          } else {
            for (phys = 0; phys < 4; ++phys) {  /* Unmap frame slots owned by this handle. */
              if (ems_phys_handle[phys] == handle) {
                if (hv_set_memory(hv, 3 + phys, 0xe0000 + ((unsigned long)phys << 14), 0, NULL, 0) < 0) {
                  perror("fatal: hv_set_memory ems free");
                  exit(252);
                }
                ems_page_map[phys] = 0;
                ems_phys_handle[phys] = 0xffff;
              }
            }
            ems_free_pages += ems_pages_by_handle[handle];
            ems_pages_by_handle[handle] = 0;
            ((unsigned char*)&regs.rax)[1] = 0;
          }
        } else if (ah == 0x46) {  /* Get EMM version. */
          ((unsigned char*)&regs.rax)[1] = 0;  /* AH=0 success. */
          ((unsigned char*)&regs.rax)[0] = 0x40;  /* AL=4.0 */
        } else if (ah == 0x4b) {  /* Get number of handles/pages. */
          unsigned short used_handles = 0, h;
          for (h = 1; h < EMS_HANDLE_COUNT; ++h) if (ems_pages_by_handle[h]) ++used_handles;
          ((unsigned char*)&regs.rax)[1] = 0;
          *(unsigned short*)&regs.rbx = EMS_HANDLE_COUNT - 1 - used_handles;  /* free handles */
          *(unsigned short*)&regs.rcx = ems_total_pages - ems_free_pages;  /* allocated pages */
          *(unsigned short*)&regs.rdx = ems_total_pages;
        } else if (ah == 0x4c) {  /* Get pages for one handle. */
          unsigned short handle = *(unsigned short*)&regs.rdx;
          if (handle == 0 || handle >= EMS_HANDLE_COUNT || ems_pages_by_handle[handle] == 0) {
            ((unsigned char*)&regs.rax)[1] = 0x83;
          } else {
            ((unsigned char*)&regs.rax)[1] = 0;
            *(unsigned short*)&regs.rbx = ems_pages_by_handle[handle];
          }
        } else if (ah == 0x58 && al == 0x00) {  /* Allocate standard pages / trivial success for probes. */
          ((unsigned char*)&regs.rax)[1] = 0;
        } else {
          return IA_FATAL_UIC;
        }
  return IA_NEXT;
}

