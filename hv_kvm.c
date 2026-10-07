#include "kvikdos.h"
#include "hv.h"

/* Linux /dev/kvm backend for the hv_* interface. */

/* musl declares ioctl(int, int, ...) while glibc takes (int, unsigned long,
 * ...) — the large _IOW/_IOR request constants overflow an implicit int
 * conversion and trip -Werror=overflow on musl.  Cast the request once
 * here: the kernel matches on the low 32 bits, so the int round-trip is
 * identical on both ABIs. */
static int kvm_ioctl(int fd, unsigned long req, void *arg) {
  return ioctl(fd, (int)req, arg);
}

struct kvm_impl {
  int kvm_fd, vm_fd, vcpu_fd;
  struct kvm_run *run;  /* mmap'd from vcpu_fd. */
};

static void kvm_destroy(struct hv *hv) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  if (ki) {
    if (ki->vcpu_fd >= 0) close(ki->vcpu_fd);
    if (ki->vm_fd >= 0) close(ki->vm_fd);
    if (ki->kvm_fd >= 0) close(ki->kvm_fd);
    free(ki);
    hv->impl = NULL;
  }
}

static int kvm_set_memory(struct hv *hv, unsigned slot, unsigned long gpa,
                          unsigned long size, void *host, int readonly) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  struct kvm_userspace_memory_region region;
  memset(&region, 0, sizeof(region));
  region.slot = slot;
  region.guest_phys_addr = gpa;
  region.memory_size = size;
  region.userspace_addr = (uintptr_t)host;
  region.flags = readonly ? KVM_MEM_READONLY : 0;
  return kvm_ioctl(ki->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0 ? -1 : 0;
}

static int kvm_create_vcpu(struct hv *hv) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  int kvm_run_mmap_size;
  struct kvm_regs dummy_regs;
  if ((ki->vcpu_fd = kvm_ioctl(ki->vm_fd, KVM_CREATE_VCPU, 0)) < 0) {
    perror("fatal: can not create KVM vcpu");
    return -1;
  }
  { /* Populate the vCPU's CPUID table so guest CPUID returns real results.
     * Without this, CPUID exits to the kernel which has no entries and
     * returns zeros, breaking DOS extender CPU detection (386/486/Pentium).
     * However, modern feature bitmaps actively break DOS-era code: e.g.
     * DOS/32A's fpu_detect does `mov cx,8' then a 32-bit `dec ecx; jnz'
     * delay loop, so the leaf-1 ECX feature mask (nonzero on every
     * post-Pentium-III CPU) turns an 8-iteration FPU-stack cleanup into a
     * ~4-billion-iteration hang.  Report leaf-1 ECX and the leaf-7
     * structured features as zero, like a Pentium II would. */
    struct kvm_cpuid2 *cpuid_data = (struct kvm_cpuid2*)calloc(
        1, sizeof(*cpuid_data) + 256 * sizeof(struct kvm_cpuid_entry2));
    if (cpuid_data) {
      cpuid_data->nent = 256;
      if (kvm_ioctl(ki->kvm_fd, KVM_GET_SUPPORTED_CPUID, cpuid_data) >= 0) {
        unsigned i;
        for (i = 0; i < cpuid_data->nent; ++i) {
          struct kvm_cpuid_entry2 *e = &cpuid_data->entries[i];
          if (e->function == 1)
            e->ecx = 0;
          else if (e->function == 7)
            e->ebx = e->ecx = e->edx = 0;
        }
        (void)kvm_ioctl(ki->vcpu_fd, KVM_SET_CPUID2, cpuid_data);  /* Best effort. */
      }
      free(cpuid_data);
    }
  }
  kvm_run_mmap_size = kvm_ioctl(ki->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
  if (kvm_run_mmap_size < 0) {
    perror("fatal: ioctl KVM_GET_VCPU_MMAP_SIZE");
    return -1;
  }
  if ((ki->run = (struct kvm_run *)mmap(
      NULL, kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, ki->vcpu_fd, 0)) == MAP_FAILED) {
    perror("fatal: mmap kvm_run");
    return -1;
  }
  if (kvm_ioctl(ki->vcpu_fd, KVM_GET_REGS, &dummy_regs) < 0) {  /* We don't use the result; but we just check here that ioctl KVM_GET_REGS works. */
    perror("fatal: KVM_GET_REGS");
    return -1;
  }
  return 0;
}

static int kvm_run_vcpu(struct hv *hv, struct hv_exit *hx) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  memset(hx, 0, sizeof(*hx));
  if (kvm_ioctl(ki->vcpu_fd, KVM_RUN, 0) < 0) {
    if (errno == EINTR) {
      /* A host signal (the ~18.2 Hz SIGALRM BIOS tick) interrupted KVM_RUN
       * while the guest ran plain CPU code — no exit would otherwise reach
       * the run loop. Surface it as a tick so the loop can do its periodic
       * housekeeping (tty drain, IRQ1 injection, repaint). Real hardware
       * delivers IRQ0/IRQ1 asynchronously; this is our delivery point. */
      hx->reason = HV_EXIT_TICK;
      return 0;
    }
    perror("fatal: KVM_RUN failed");
    return -1;
  }
  switch (ki->run->exit_reason) {
   case KVM_EXIT_IO:
    hx->reason = HV_EXIT_IO;
    hx->io.port = ki->run->io.port;
    hx->io.direction = ki->run->io.direction;
    hx->io.size = ki->run->io.size;
    hx->io.count = ki->run->io.count;
    hx->io.data = (unsigned char*)ki->run + ki->run->io.data_offset;
    break;
   case KVM_EXIT_MMIO:
    hx->reason = HV_EXIT_MMIO;
    hx->mmio.phys_addr = ki->run->mmio.phys_addr;
    hx->mmio.len = ki->run->mmio.len;
    hx->mmio.is_write = ki->run->mmio.is_write;
    hx->mmio.data = ki->run->mmio.data;  /* Kernel's buffer; read-fill lands back in the guest. */
    break;
   case KVM_EXIT_DEBUG: hx->reason = HV_EXIT_DEBUG; break;
   case KVM_EXIT_SHUTDOWN: hx->reason = HV_EXIT_SHUTDOWN; break;
   case KVM_EXIT_HLT: hx->reason = HV_EXIT_HLT; break;
   default: hx->reason = HV_EXIT_INTERNAL; break;
  }
  return 0;
}

static int kvm_get_fds(const struct hv *hv, int *out, int n) {
  const struct kvm_impl *ki = (const struct kvm_impl*)hv->impl;
  int count = 0;
  if (count < n) out[count++] = ki->kvm_fd;
  if (count < n) out[count++] = ki->vm_fd;
  if (count < n) out[count++] = ki->vcpu_fd;
  return count;
}

static int kvm_get_regs(struct hv *hv, struct kvm_regs *regs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return kvm_ioctl(ki->vcpu_fd, KVM_GET_REGS, regs) < 0 ? -1 : 0;
}

static int kvm_set_regs(struct hv *hv, const struct kvm_regs *regs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return kvm_ioctl(ki->vcpu_fd, KVM_SET_REGS, (struct kvm_regs*)regs) < 0 ? -1 : 0;
}

static int kvm_get_sregs(struct hv *hv, struct kvm_sregs *sregs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return kvm_ioctl(ki->vcpu_fd, KVM_GET_SREGS, sregs) < 0 ? -1 : 0;
}

static int kvm_set_sregs(struct hv *hv, const struct kvm_sregs *sregs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return kvm_ioctl(ki->vcpu_fd, KVM_SET_SREGS, (struct kvm_sregs*)sregs) < 0 ? -1 : 0;
}

static int kvm_interrupt(struct hv *hv, unsigned irq_line) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  struct kvm_interrupt irq;
  /* KVM_INTERRUPT injects a vector, not a line: translate through the PIC
   * base like real hardware (master ICW2=8, slave ICW2=0x70 — same mapping
   * hv_whpx.c applies for WHvRequestInterrupt). */
  irq.irq = irq_line < 8 ? 8 + irq_line : 0x68 + irq_line;
  return kvm_ioctl(ki->vcpu_fd, KVM_INTERRUPT, &irq) < 0 ? -1 : 0;
}

static const struct hv_ops kvm_ops = {
  "KVM",
  kvm_destroy,
  kvm_set_memory,
  kvm_create_vcpu,
  kvm_run_vcpu,
  kvm_get_fds,
  kvm_get_regs,
  kvm_set_regs,
  kvm_get_sregs,
  kvm_set_sregs,
  kvm_interrupt
};

struct hv *hv_kvm_create(void) {
  struct hv *hv;
  struct kvm_impl *ki;
  int api_version;
  hv = (struct hv*)calloc(1, sizeof(*hv));
  ki = (struct kvm_impl*)calloc(1, sizeof(*ki));
  if (!hv || !ki) { free(hv); free(ki); return NULL; }
  hv->ops = &kvm_ops;
  hv->impl = ki;
  ki->kvm_fd = ki->vm_fd = ki->vcpu_fd = -1;
  if ((ki->kvm_fd = open("/dev/kvm", O_RDWR)) < 0) {
    perror("fatal: failed to open /dev/kvm");
    goto fail;
  }
  if ((api_version = kvm_ioctl(ki->kvm_fd, KVM_GET_API_VERSION, 0)) < 0) {
    perror("fatal: failed to get KVM api version");
    goto fail;
  }
  if (api_version != KVM_API_VERSION) {
    fprintf(stderr, "fatal: KVM API version mismatch: kernel=%d user=%d\n",
            api_version, KVM_API_VERSION);
  }
  if ((ki->vm_fd = kvm_ioctl(ki->kvm_fd, KVM_CREATE_VM, 0)) < 0) {
    perror("fatal: failed to create KVM vm");
    goto fail;
  }
  /* No KVM_CREATE_IRQCHIP on purpose: with an in-kernel PIC, guest HLT with
   * IF=1 blocks inside KVM_RUN instead of exiting to userspace — and our
   * whole int-stub model needs the exit.  Interrupts are injected by hand
   * (guest_inject_irq pushes the frame + jumps the IVT vector). */
  return hv;
 fail:
  kvm_destroy(hv);
  free(hv);
  return NULL;
}
