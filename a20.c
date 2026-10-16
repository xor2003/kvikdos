#include "kvikdos.h"
#include "intrun.h"

static void map_hma(EmuState *state, unsigned slot, unsigned long gpa,
                    unsigned long size, void *host, int readonly) {
  if (hv_set_memory(state->hv, slot, gpa, size, host, readonly) < 0) {
    perror("fatal: hv_set_memory A20");
    exit(252);
  }
}

/* A real-mode far pointer can reach 10ffefh. With A20 disabled, that
 * window aliases low DOS RAM; enabling A20 exposes the actual HMA. Keep
 * the first page read-only in the alias so IVT/BDA writes still trap. */
void emu_set_a20(EmuState *state, int enabled) {
  char * const backing = enabled ? (char*)state->xmem : (char*)state->mem;
  /* KVM rejects replacing a live slot's backing/readonly flag and also
   * rejects deleting an empty slot. Track whether the window exists. */
  if (state->hma_mapped) {
    map_hma(state, 4, 0x100000, 0, NULL, 0);
    map_hma(state, 5, 0x101000, 0, NULL, 0);
    state->hma_mapped = 0;
  }
  if (backing) {
    map_hma(state, 4, 0x100000, 0x1000, backing, !enabled);
    map_hma(state, 5, 0x101000, 0xf000, backing + 0x1000, 0);
    state->hma_mapped = 1;
  }
  port_0x92_a20 = enabled != 0;
}
