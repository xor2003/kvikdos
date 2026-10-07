#ifndef HV_H
#define HV_H

/* Hypervisor backend abstraction. Two implementations exist:
 * hv_kvm.c  - Linux /dev/kvm.
 * hv_whpx.c - Windows Hypervisor Platform (WinHvPlatform.dll, loaded via dlopen).
 * Guest register state uses the kvm_* POD layouts (platform-neutral) as the
 * universal register format. Each backend provides a hv_ops vtable; hv_*
 * functions in hvpick.c dispatch through it. */

struct hv {
  const struct hv_ops *ops;
  void *impl;  /* Backend-private state. */
};

enum hv_exit_reason {
  HV_EXIT_HLT,       /* Guest executed HLT (magic int stubs land here). */
  HV_EXIT_IO,        /* Port I/O needs service. */
  HV_EXIT_MMIO,      /* Memory-mapped access needs service. */
  HV_EXIT_DEBUG,     /* Debug exit; ignored by the run loop. */
  HV_EXIT_SHUTDOWN,  /* Triple fault / shutdown. */
  HV_EXIT_TICK,      /* Host timer tick interrupted a CPU-bound guest: housekeeping. */
  HV_EXIT_INTERNAL   /* Unrecoverable or unimplemented exit. */
};

struct hv_io {
  unsigned short port;
  unsigned char direction;  /* 0 = IN (fill data), 1 = OUT (consume data). */
  unsigned char size;       /* Bytes per access: 1, 2 or 4. */
  unsigned char count;      /* Access count (rep/string; normally 1). */
  unsigned char *data;      /* size*count bytes; points into backend storage. */
};

struct hv_mmio {
  unsigned long phys_addr;
  unsigned char len;        /* Access size in bytes. */
  unsigned char is_write;
  unsigned char *data;      /* Written value (is_write) or buffer to fill;
                               points into backend storage. */
};

struct hv_exit {
  int reason;  /* enum hv_exit_reason. */
  struct hv_io io;
  struct hv_mmio mmio;
};

struct hv_ops {
  const char *name;
  void (*destroy)(struct hv *hv);
  /* Map a host buffer to guest physical memory. slot is a small index for
   * later remapping (EMS pages); size==0 unmaps. readonly traps guest
   * writes as MMIO exits. */
  int (*set_memory)(struct hv *hv, unsigned slot, unsigned long gpa,
                    unsigned long size, void *host, int readonly);
  int (*create_vcpu)(struct hv *hv);
  /* Run the vCPU until an exit; fills the exit context. 0 = normal exit,
   * -1 = backend failure. */
  int (*run)(struct hv *hv, struct hv_exit *hx);
  /* Host fds that must not leak into the DOS handle table (security);
   * fills up to `n`, returns count. */
  int (*get_fds)(const struct hv *hv, int *out, int n);
  int (*get_regs)(struct hv *hv, struct kvm_regs *regs);
  int (*set_regs)(struct hv *hv, const struct kvm_regs *regs);
  int (*get_sregs)(struct hv *hv, struct kvm_sregs *sregs);
  int (*set_sregs)(struct hv *hv, const struct kvm_sregs *sregs);
  /* Raise a PIC interrupt line (0=timer, 1=keyboard, ...); the backend routes
   * it to the guest vector. 0 = accepted, -1 = unsupported. */
  int (*interrupt)(struct hv *hv, unsigned irq_line);
};

/* Create/destroy a VM. hv_create picks the backend for the host OS:
 * WinHvPlatform on Windows (cosmopolitan IsWindows()), /dev/kvm elsewhere.
 * Returns NULL (with a message on stderr) when no backend is available. */
struct hv *hv_create(void);
void hv_destroy(struct hv *hv);
const char *hv_name(const struct hv *hv);
int hv_set_memory(struct hv *hv, unsigned slot, unsigned long gpa,
                  unsigned long size, void *host, int readonly);
int hv_create_vcpu(struct hv *hv);
int hv_run(struct hv *hv, struct hv_exit *hx);
int hv_get_fds(const struct hv *hv, int *out, int n);
int hv_get_regs(struct hv *hv, struct kvm_regs *regs);
int hv_set_regs(struct hv *hv, const struct kvm_regs *regs);
int hv_get_sregs(struct hv *hv, struct kvm_sregs *sregs);
int hv_set_sregs(struct hv *hv, const struct kvm_sregs *sregs);
int hv_interrupt(struct hv *hv, unsigned irq_line);

/* Backend-specific constructors (hv_create dispatches on the host OS). */
struct hv *hv_kvm_create(void);
struct hv *hv_whpx_create(void);

#endif
