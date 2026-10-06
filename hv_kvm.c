#include "kvikdos.h"
#include "hv.h"

/* Linux /dev/kvm backend for the hv_* interface. */

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
  return ioctl(ki->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0 ? -1 : 0;
}

static int kvm_create_vcpu(struct hv *hv) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  int kvm_run_mmap_size;
  struct kvm_regs dummy_regs;
  if ((ki->vcpu_fd = ioctl(ki->vm_fd, KVM_CREATE_VCPU, 0)) < 0) {
    perror("fatal: can not create KVM vcpu");
    return -1;
  }
  { /* Populate the vCPU's CPUID table so guest CPUID returns real results.
     * Without this, CPUID exits to the kernel which has no entries and
     * returns zeros, breaking DOS extender CPU detection (386/486/Pentium). */
    struct kvm_cpuid2 *cpuid_data = (struct kvm_cpuid2*)calloc(
        1, sizeof(*cpuid_data) + 256 * sizeof(struct kvm_cpuid_entry2));
    if (cpuid_data) {
      cpuid_data->nent = 256;
      if (ioctl(ki->kvm_fd, KVM_GET_SUPPORTED_CPUID, cpuid_data) >= 0) {
        (void)ioctl(ki->vcpu_fd, KVM_SET_CPUID2, cpuid_data);  /* Best effort. */
      }
      free(cpuid_data);
    }
  }
  kvm_run_mmap_size = ioctl(ki->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
  if (kvm_run_mmap_size < 0) {
    perror("fatal: ioctl KVM_GET_VCPU_MMAP_SIZE");
    return -1;
  }
  if ((ki->run = (struct kvm_run *)mmap(
      NULL, kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, ki->vcpu_fd, 0)) == MAP_FAILED) {
    perror("fatal: mmap kvm_run");
    return -1;
  }
  if (ioctl(ki->vcpu_fd, KVM_GET_REGS, &dummy_regs) < 0) {  /* We don't use the result; but we just check here that ioctl KVM_GET_REGS works. */
    perror("fatal: KVM_GET_REGS");
    return -1;
  }
  return 0;
}

static int kvm_run_vcpu(struct hv *hv, struct hv_exit *hx) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  const int ret = ioctl(ki->vcpu_fd, KVM_RUN, 0);
  if (ret < 0) {
    perror("fatal: KVM_RUN failed");
    return -1;
  }
  memset(hx, 0, sizeof(*hx));
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
  return ioctl(ki->vcpu_fd, KVM_GET_REGS, regs) < 0 ? -1 : 0;
}

static int kvm_set_regs(struct hv *hv, const struct kvm_regs *regs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return ioctl(ki->vcpu_fd, KVM_SET_REGS, (struct kvm_regs*)regs) < 0 ? -1 : 0;
}

static int kvm_get_sregs(struct hv *hv, struct kvm_sregs *sregs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return ioctl(ki->vcpu_fd, KVM_GET_SREGS, sregs) < 0 ? -1 : 0;
}

static int kvm_set_sregs(struct hv *hv, const struct kvm_sregs *sregs) {
  struct kvm_impl *ki = (struct kvm_impl*)hv->impl;
  return ioctl(ki->vcpu_fd, KVM_SET_SREGS, (struct kvm_sregs*)sregs) < 0 ? -1 : 0;
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
  kvm_set_sregs
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
  if ((api_version = ioctl(ki->kvm_fd, KVM_GET_API_VERSION, 0)) < 0) {
    perror("fatal: failed to get KVM api version");
    goto fail;
  }
  if (api_version != KVM_API_VERSION) {
    fprintf(stderr, "fatal: KVM API version mismatch: kernel=%d user=%d\n",
            api_version, KVM_API_VERSION);
  }
  if ((ki->vm_fd = ioctl(ki->kvm_fd, KVM_CREATE_VM, 0)) < 0) {
    perror("fatal: failed to create KVM vm");
    goto fail;
  }
  return hv;
 fail:
  kvm_destroy(hv);
  free(hv);
  return NULL;
}
