#include "kvikdos.h"
#include "hv.h"
#include "mini_whpx.h"
#include "x86dec.h"
#include <dlfcn.h>

/* Windows Hypervisor Platform backend (WinHvPlatform.dll). Unlike KVM,
 * WHPX exits do not complete the faulting instruction: the emulator owns
 * it. For OUT writes and memory stores we can finish immediately (the exit
 * reports the value or we decode it); for IN reads and memory loads the
 * run loop fills hx.io.data / hx.mmio.data, and we commit the result into
 * the destination register at the start of the next hv_run, also advancing
 * RIP past the instruction. Undecodable accesses are skipped (RIP += the
 * exit's byte count) so unknown forms degrade to no-ops, not faults. */

enum { PEND_NONE, PEND_IO_IN, PEND_MMIO_READ };

struct whpx_impl {
  WHV_PARTITION_HANDLE partition;
  whpx_api api;
  WHV_RUN_VP_EXIT_CONTEXT ctx;
  /* Pending exit emulation to commit at next hv_run. */
  int pend_kind;
  unsigned char pend_inst[16];
  unsigned char pend_instlen;
  unsigned char pend_size;    /* io.size or mmio.len */
  uint64_t pend_rip;          /* Faulting instruction's RIP. */
  unsigned char iobuf[16];    /* hx.io.data backing. */
  unsigned char mmbuf[8];     /* hx.mmio.data backing. */
  /* Region tracking: WHPX has no slot API, remember ranges for remap. */
  struct { unsigned long gpa, size; void *host; int ro; } regions[16];
};

static const int gpr_names[] = {
  WHvX64RegisterRax, WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterRbx,
  WHvX64RegisterRsp, WHvX64RegisterRbp, WHvX64RegisterRsi, WHvX64RegisterRdi,
  WHvX64RegisterR8, WHvX64RegisterR9, WHvX64RegisterR10, WHvX64RegisterR11,
  WHvX64RegisterR12, WHvX64RegisterR13, WHvX64RegisterR14, WHvX64RegisterR15,
  WHvX64RegisterRip, WHvX64RegisterRflags
};

static int whpx_get_regs(struct hv *hv, struct kvm_regs *regs) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  WHV_REGISTER_VALUE v[18];
  if (w->api.GetVirtualProcessorRegisters(w->partition, 0, gpr_names, 18, v) < 0)
    return -1;
  regs->rax = v[0].Reg64; regs->rcx = v[1].Reg64; regs->rdx = v[2].Reg64;
  regs->rbx = v[3].Reg64; regs->rsp = v[4].Reg64; regs->rbp = v[5].Reg64;
  regs->rsi = v[6].Reg64; regs->rdi = v[7].Reg64;
  regs->r8 = v[8].Reg64; regs->r9 = v[9].Reg64; regs->r10 = v[10].Reg64;
  regs->r11 = v[11].Reg64; regs->r12 = v[12].Reg64; regs->r13 = v[13].Reg64;
  regs->r14 = v[14].Reg64; regs->r15 = v[15].Reg64;
  regs->rip = v[16].Reg64; regs->rflags = v[17].Reg64;
  return 0;
}

static int whpx_set_regs(struct hv *hv, const struct kvm_regs *regs) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  WHV_REGISTER_VALUE v[18];
  v[0].Reg64 = regs->rax; v[1].Reg64 = regs->rcx; v[2].Reg64 = regs->rdx;
  v[3].Reg64 = regs->rbx; v[4].Reg64 = regs->rsp; v[5].Reg64 = regs->rbp;
  v[6].Reg64 = regs->rsi; v[7].Reg64 = regs->rdi;
  v[8].Reg64 = regs->r8; v[9].Reg64 = regs->r9; v[10].Reg64 = regs->r10;
  v[11].Reg64 = regs->r11; v[12].Reg64 = regs->r12; v[13].Reg64 = regs->r13;
  v[14].Reg64 = regs->r14; v[15].Reg64 = regs->r15;
  v[16].Reg64 = regs->rip; v[17].Reg64 = regs->rflags;
  return w->api.SetVirtualProcessorRegisters(w->partition, 0, gpr_names, 18, v) < 0 ? -1 : 0;
}

/* kvm_segment -> WHPX attributes field. */
static uint16_t seg_attr(const struct kvm_segment *s) {
  if (s->unusable || !s->present) return 0;
  return (uint16_t)((s->type & 0xf) | ((s->s & 1) << 4) | ((s->dpl & 3) << 5) |
         (1 << 7) | ((s->avl & 1) << 12) | ((s->l & 1) << 13) |
         ((s->db & 1) << 14) | ((s->g & 1) << 15));
}

static void seg_from_whp(struct kvm_segment *d, const WHV_X64_SEGMENT_REGISTER *s) {
  d->base = s->Base;
  d->limit = s->Limit;
  d->selector = s->Selector;
  d->type = s->Attributes & 0xf;
  d->s = (s->Attributes >> 4) & 1;
  d->dpl = (s->Attributes >> 5) & 3;
  d->present = (s->Attributes >> 7) & 1;
  d->avl = (s->Attributes >> 12) & 1;
  d->l = (s->Attributes >> 13) & 1;
  d->db = (s->Attributes >> 14) & 1;
  d->g = (s->Attributes >> 15) & 1;
  d->unusable = !d->present;
  d->padding = 0;
}

static void seg_to_whp(WHV_X64_SEGMENT_REGISTER *d, const struct kvm_segment *s) {
  d->Base = s->base;
  d->Limit = s->limit;
  d->Selector = s->selector;
  d->Attributes = seg_attr(s);
}

static const int sreg_names[] = {
  WHvX64RegisterEs, WHvX64RegisterCs, WHvX64RegisterSs, WHvX64RegisterDs,
  WHvX64RegisterFs, WHvX64RegisterGs, WHvX64RegisterLdtr, WHvX64RegisterTr,
  WHvX64RegisterIdtr, WHvX64RegisterGdtr,
  WHvX64RegisterCr0, WHvX64RegisterCr2, WHvX64RegisterCr3,
  WHvX64RegisterCr4, WHvX64RegisterCr8, WHvX64RegisterEfer
};

static int whpx_get_sregs(struct hv *hv, struct kvm_sregs *sregs) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  WHV_REGISTER_VALUE v[16];
  memset(sregs, 0, sizeof(*sregs));
  if (w->api.GetVirtualProcessorRegisters(w->partition, 0, sreg_names, 16, v) < 0)
    return -1;
  seg_from_whp(&sregs->es, &v[0].Segment);
  seg_from_whp(&sregs->cs, &v[1].Segment);
  seg_from_whp(&sregs->ss, &v[2].Segment);
  seg_from_whp(&sregs->ds, &v[3].Segment);
  seg_from_whp(&sregs->fs, &v[4].Segment);
  seg_from_whp(&sregs->gs, &v[5].Segment);
  seg_from_whp(&sregs->ldt, &v[6].Segment);
  seg_from_whp(&sregs->tr, &v[7].Segment);
  sregs->idt.base = v[8].Table.Base;
  sregs->idt.limit = v[8].Table.Limit;
  sregs->gdt.base = v[9].Table.Base;
  sregs->gdt.limit = v[9].Table.Limit;
  sregs->cr0 = v[10].Reg64; sregs->cr2 = v[11].Reg64; sregs->cr3 = v[12].Reg64;
  sregs->cr4 = v[13].Reg64; sregs->cr8 = v[14].Reg64; sregs->efer = v[15].Reg64;
  return 0;
}

static int whpx_set_sregs(struct hv *hv, const struct kvm_sregs *sregs) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  WHV_REGISTER_VALUE v[16];
  int rc = 0;
  seg_to_whp(&v[0].Segment, &sregs->es);
  seg_to_whp(&v[1].Segment, &sregs->cs);
  seg_to_whp(&v[2].Segment, &sregs->ss);
  seg_to_whp(&v[3].Segment, &sregs->ds);
  seg_to_whp(&v[4].Segment, &sregs->fs);
  seg_to_whp(&v[5].Segment, &sregs->gs);
  seg_to_whp(&v[6].Segment, &sregs->ldt);
  seg_to_whp(&v[7].Segment, &sregs->tr);
  memset(&v[8], 0, sizeof(v[8]));
  v[8].Table.Base = sregs->idt.base;
  v[8].Table.Limit = sregs->idt.limit;
  memset(&v[9], 0, sizeof(v[9]));
  v[9].Table.Base = sregs->gdt.base;
  v[9].Table.Limit = sregs->gdt.limit;
  v[10].Reg64 = sregs->cr0; v[11].Reg64 = sregs->cr2; v[12].Reg64 = sregs->cr3;
  v[13].Reg64 = sregs->cr4; v[14].Reg64 = sregs->cr8; v[15].Reg64 = sregs->efer;
  /* Set in two batches so a single rejected register doesn't lose the rest:
   * segments+tables first, then control regs (Cr8/Efer may be unsupported). */
  if (w->api.SetVirtualProcessorRegisters(w->partition, 0, sreg_names, 10, v) < 0)
    rc = -1;
  if (w->api.SetVirtualProcessorRegisters(w->partition, 0, sreg_names + 10, 6, v + 10) < 0)
    rc = -1;
  return rc;
}

static int whpx_get_fds(const struct hv *hv, int *out, int n) {
  (void)hv; (void)out; (void)n;
  return 0;  /* No fds: the Windows handle table is separate. */
}

/* Apply the pending exit emulation at the start of hv_run: fill the
 * destination register with the data the run loop provided, and advance
 * RIP past the emulated instruction. */
static void whpx_commit(struct hv *hv) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  struct kvm_regs regs;
  struct kvm_sregs sregs;
  unsigned long fill;
  if (w->pend_kind == PEND_NONE) return;
  if (w->pend_kind == PEND_IO_IN) {
    const unsigned long mask = w->pend_size == 1 ? 0xffUL :
        w->pend_size == 2 ? 0xffffUL : 0xffffffffUL;
    fill = 0;
    memcpy(&fill, w->iobuf, w->pend_size < 8 ? w->pend_size : 8);
    if (whpx_get_regs(hv, &regs) == 0) {
      regs.rax = (regs.rax & ~mask) | (fill & mask);
      regs.rip = w->pend_rip + w->pend_instlen;
      (void)whpx_set_regs(hv, &regs);
    }
  } else {  /* PEND_MMIO_READ */
    fill = 0;
    memcpy(&fill, w->mmbuf, w->pend_size < 8 ? w->pend_size : 8);
    if (whpx_get_regs(hv, &regs) == 0 && whpx_get_sregs(hv, &sregs) == 0) {
      if (x86dec_load(w->pend_inst, w->pend_instlen, &regs, &sregs, fill,
                      w->pend_size) == 0) {
        regs.rip = w->pend_rip + w->pend_instlen;
        (void)whpx_set_regs(hv, &regs);
        (void)whpx_set_sregs(hv, &sregs);
      } else {
        regs.rip = w->pend_rip + w->pend_instlen;  /* Undecodable: just skip. */
        (void)whpx_set_regs(hv, &regs);
      }
    }
  }
  w->pend_kind = PEND_NONE;
}

/* Advance RIP past the faulting instruction (completes it as a no-op). */
static void whpx_skip(struct whpx_impl *w, uint64_t rip, unsigned len) {
  int name = WHvX64RegisterRip;
  WHV_REGISTER_VALUE v;
  v.Reg64 = rip + len;
  (void)w->api.SetVirtualProcessorRegisters(w->partition, 0, &name, 1, &v);
}

static int whpx_run(struct hv *hv, struct hv_exit *hx) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  memset(hx, 0, sizeof(*hx));
  whpx_commit(hv);
  for (;;) {
    WHV_RUN_VP_EXIT_CONTEXT *ctx = &w->ctx;
    if (w->api.RunVirtualProcessor(w->partition, 0, ctx, sizeof(*ctx)) < 0) {
      fprintf(stderr, "fatal: WHvRunVirtualProcessor failed\n");
      return -1;
    }
    switch (ctx->ExitReason) {
     case WHvRunVpExitReasonX64Halt:
      hx->reason = HV_EXIT_HLT;
      return 0;
     case WHvRunVpExitReasonX64IoPortAccess: {
       const WHV_X64_IO_PORT_ACCESS_CONTEXT *io = &ctx->u.IoPortAccess;
       const unsigned size = (io->AccessInfo >> 1) & 7;  /* AccessSize: bytes. */
       if (io->AccessInfo & (0x10 | 0x20)) {  /* StringOp or RepPrefix. */
         fprintf(stderr, "fatal: WHPX string/rep port IO not supported\n");
         hx->reason = HV_EXIT_INTERNAL;
         return 0;
       }
       if (io->AccessInfo & 1) {  /* OUT: value in Rax; completes now. */
         hx->reason = HV_EXIT_IO;
         hx->io.port = io->PortNumber;
         hx->io.direction = 1;
         hx->io.size = size == 0 || size > 4 ? 1 : size;
         hx->io.count = 1;
         memcpy(w->iobuf, &io->Rax, 8);
         hx->io.data = w->iobuf;
         whpx_skip(w, ctx->VpContext.Rip, io->InstructionByteCount);
         return 0;
       } else {  /* IN: run loop fills iobuf; commit merges to RAX. */
         hx->reason = HV_EXIT_IO;
         hx->io.port = io->PortNumber;
         hx->io.direction = 0;
         hx->io.size = size == 0 || size > 4 ? 1 : size;
         hx->io.count = 1;
         memset(w->iobuf, 0, sizeof(w->iobuf));
         hx->io.data = w->iobuf;
         w->pend_kind = PEND_IO_IN;
         w->pend_size = hx->io.size;
         w->pend_rip = ctx->VpContext.Rip;
         w->pend_instlen = io->InstructionByteCount;
         return 0;
       }
     }
     case WHvRunVpExitReasonMemoryAccess: {
       const WHV_MEMORY_ACCESS_CONTEXT *ma = &ctx->u.MemoryAccess;
       const unsigned atype = ma->AccessInfo & 3;
       if (atype == 1) {  /* Write: decode the stored value, complete now. */
         struct kvm_regs regs;
         struct kvm_sregs sregs;
         unsigned long value = 0;
         unsigned size = 0;
         if (whpx_get_regs(hv, &regs) < 0 || whpx_get_sregs(hv, &sregs) < 0 ||
             x86dec_store(ma->InstructionBytes, ma->InstructionByteCount,
                          &regs, &sregs, &value, &size) < 0 || size > 8) {
           /* Undecodable store: skip it, keep running. */
           static int store_warned = 0;
           if (!store_warned) {
             fprintf(stderr, "info: WHPX undecodable store skipped (rip=%lx)\n",
                     (unsigned long)ctx->VpContext.Rip);
             store_warned = 1;
           }
           whpx_skip(w, ctx->VpContext.Rip, ma->InstructionByteCount);
           continue;
         }
         hx->reason = HV_EXIT_MMIO;
         hx->mmio.phys_addr = ma->Gpa;
         hx->mmio.len = size;
         hx->mmio.is_write = 1;
         memcpy(w->mmbuf, &value, size);
         hx->mmio.data = w->mmbuf;
         whpx_skip(w, ctx->VpContext.Rip, ma->InstructionByteCount);
         return 0;
       } else if (atype == 0) {  /* Read: run loop fills mmbuf, then commit. */
         const int asize = x86dec_access_size(ma->InstructionBytes,
                                              ma->InstructionByteCount);
         hx->reason = HV_EXIT_MMIO;
         hx->mmio.phys_addr = ma->Gpa;
         hx->mmio.len = asize > 0 && asize <= 8 ? asize : 1;
         hx->mmio.is_write = 0;
         memset(w->mmbuf, 0, sizeof(w->mmbuf));
         hx->mmio.data = w->mmbuf;
         memcpy(w->pend_inst, ma->InstructionBytes,
                ma->InstructionByteCount < 16 ? ma->InstructionByteCount : 16);
         w->pend_instlen = ma->InstructionByteCount;
         w->pend_size = hx->mmio.len;
         w->pend_rip = ctx->VpContext.Rip;
         w->pend_kind = PEND_MMIO_READ;
         return 0;
       }
       /* Execute or unknown: internal error. */
       hx->reason = HV_EXIT_INTERNAL;
       return 0;
     }
     case WHvRunVpExitReasonX64InterruptWindow:
     case WHvRunVpExitReasonCanceled:
     case WHvRunVpExitReasonNone:
      continue;  /* Nothing to service; run again. */
     case WHvRunVpExitReasonUnrecoverableException:
      fprintf(stderr, "fatal: WHPX unrecoverable exception\n");
      hx->reason = HV_EXIT_INTERNAL;
      return 0;
     case WHvRunVpExitReasonInvalidVpRegisterValue:
      fprintf(stderr, "fatal: WHPX invalid vp register value\n");
      hx->reason = HV_EXIT_INTERNAL;
      return 0;
     case WHvRunVpExitReasonUnsupportedFeature:
      fprintf(stderr, "fatal: WHPX unsupported feature\n");
      hx->reason = HV_EXIT_INTERNAL;
      return 0;
     default:
      hx->reason = HV_EXIT_INTERNAL;
      return 0;
    }
  }
}

static int whpx_set_memory(struct hv *hv, unsigned slot, unsigned long gpa,
                           unsigned long size, void *host, int readonly) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  uint32_t flags = WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagExecute;
  if (slot >= sizeof(w->regions) / sizeof(w->regions[0])) return -1;
  if (w->regions[slot].size) {  /* Slot already mapped: unmap the old range. */
    (void)w->api.UnmapGpaRange(w->partition, w->regions[slot].gpa, w->regions[slot].size);
    w->regions[slot].size = 0;
  }
  if (!size) return 0;  /* Unmap only. */
  if (!readonly) flags |= WHvMapGpaRangeFlagWrite;
  if (w->api.MapGpaRange(w->partition, host, gpa, size, flags) < 0) return -1;
  w->regions[slot].gpa = gpa;
  w->regions[slot].size = size;
  w->regions[slot].host = host;
  w->regions[slot].ro = readonly;
  return 0;
}

static int whpx_create_vcpu(struct hv *hv) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  return w->api.CreateVirtualProcessor(w->partition, 0, 0) < 0 ? -1 : 0;
}

static void whpx_destroy(struct hv *hv) {
  struct whpx_impl *w = (struct whpx_impl*)hv->impl;
  if (w) {
    if (w->partition) {
      (void)w->api.DeleteVirtualProcessor(w->partition, 0);
      (void)w->api.DeletePartition(w->partition);
    }
    free(w);
    hv->impl = NULL;
  }
}

static const struct hv_ops whpx_ops = {
  "WHPX",
  whpx_destroy,
  whpx_set_memory,
  whpx_create_vcpu,
  whpx_run,
  whpx_get_fds,
  whpx_get_regs,
  whpx_set_regs,
  whpx_get_sregs,
  whpx_set_sregs
};

static void *whpx_sym(void *lib, const char *name) {
  void *p = dlsym(lib, name);
  if (!p) fprintf(stderr, "fatal: WinHvPlatform.dll missing export %s\n", name);
  return p;
}

struct hv *hv_whpx_create(void) {
  struct hv *hv;
  struct whpx_impl *w;
  void *lib;
  uint32_t cap = 0, written = 0;
  uint64_t prop;

  lib = dlopen("WinHvPlatform.dll", RTLD_NOW);
  if (!lib) {
    fprintf(stderr, "fatal: cannot load WinHvPlatform.dll (WHPX feature not enabled?)\n");
    return NULL;
  }
  hv = (struct hv*)calloc(1, sizeof(*hv));
  w = (struct whpx_impl*)calloc(1, sizeof(*w));
  if (!hv || !w) { free(hv); free(w); return NULL; }
  hv->ops = &whpx_ops;
  hv->impl = w;

#define WHPX_LOAD(field, dllname) do { \
    void *p_ = whpx_sym(lib, "WHv" dllname); \
    if (!p_) goto fail; \
    memcpy(&w->api.field, &p_, sizeof p_);  /* fn-ptr via object repr (pedantic-safe). */ \
  } while (0)
  WHPX_LOAD(GetCapability, "GetCapability");
  WHPX_LOAD(CreatePartition, "CreatePartition");
  WHPX_LOAD(SetupPartition, "SetupPartition");
  WHPX_LOAD(SetPartitionProperty, "SetPartitionProperty");
  WHPX_LOAD(DeletePartition, "DeletePartition");
  WHPX_LOAD(MapGpaRange, "MapGpaRange");
  WHPX_LOAD(UnmapGpaRange, "UnmapGpaRange");
  WHPX_LOAD(CreateVirtualProcessor, "CreateVirtualProcessor");
  WHPX_LOAD(DeleteVirtualProcessor, "DeleteVirtualProcessor");
  WHPX_LOAD(RunVirtualProcessor, "RunVirtualProcessor");
  WHPX_LOAD(GetVirtualProcessorRegisters, "GetVirtualProcessorRegisters");
  WHPX_LOAD(SetVirtualProcessorRegisters, "SetVirtualProcessorRegisters");
#undef WHPX_LOAD

  if (w->api.GetCapability(WHvCapabilityCodeHypervisorPresent, &cap,
                           sizeof(cap), &written) < 0 || !cap) {
    fprintf(stderr, "fatal: WHPX hypervisor not present\n");
    goto fail;
  }
  if (w->api.CreatePartition(&w->partition) < 0) {
    fprintf(stderr, "fatal: WHvCreatePartition failed\n");
    goto fail;
  }
  prop = 1;  /* One virtual processor. */
  if (w->api.SetPartitionProperty(w->partition,
                                  WHvPartitionPropertyCodeProcessorCount,
                                  &prop, sizeof(prop)) < 0) {
    fprintf(stderr, "fatal: WHvSetPartitionProperty(ProcessorCount) failed\n");
    goto fail;
  }
  if (w->api.SetupPartition(w->partition) < 0) {
    fprintf(stderr, "fatal: WHvSetupPartition failed\n");
    goto fail;
  }
  return hv;
 fail:
  if (w->partition) (void)w->api.DeletePartition(w->partition);
  free(w);
  free(hv);
  return NULL;
}
