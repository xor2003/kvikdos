#include "kvikdos.h"

void init_emu(struct EmuState *emu) {
  emu->kvm_fds.kvm_fd = -1;
  emu->mem = NULL;
  emu->xmem = NULL;
  emu->xmem_size = 0;
  emu->ems_pool = NULL;
  emu->ems_pool_pages = 0;
}

char *guest_ptr(const EmuState *emu, unsigned long gpa) {
  if (gpa < GUEST_MEM_LIMIT) return (char*)emu->mem + gpa;
  if (emu->xmem_size && gpa - 0x100000UL < emu->xmem_size) return (char*)emu->xmem + (gpa - 0x100000UL);
  return NULL;
}

void reset_emu(struct EmuState *emu, const EmuParams *emu_params) {
  void *mem;
  if (emu->kvm_fds.kvm_fd < 0) {
    int kvm_fd, vm_fd, vcpu_fd;
    int kvm_run_mmap_size, api_version;
    struct kvm_userspace_memory_region region;
    struct kvm_regs dummy_regs;
    if ((kvm_fd = open("/dev/kvm", O_RDWR)) < 0) {
      perror("fatal: failed to open /dev/kvm");
      exit(252);
    }
    if ((api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0)) < 0) {
      perror("fatal: failed to create KVM vm");
      exit(252);
    }
    if (api_version != KVM_API_VERSION) {
      fprintf(stderr, "fatal: KVM API version mismatch: kernel=%d user=%d\n",
              api_version, KVM_API_VERSION);
    }
    if ((vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0)) < 0) {
      perror("fatal: failed to create KVM vm");
      exit(252);
    }
    /* If emu_params->mem_mb > 1, then we have allocate more. */
    if ((emu->mem = mem = mmap(NULL, GUEST_MEM_LIMIT, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)) ==
        NULL) {
      perror("fatal: mmap");
      exit(252);
    }

    memset(&region, 0, sizeof(region));
    region.slot = 0;
    region.guest_phys_addr = GUEST_MEM_MODULE_START;  /* Must be a multiple of the Linux page size (0x1000), otherwise KVM_SET_USER_MEMORY_REGION returns EINVAL. */
    region.memory_size = GUEST_MEM_LIMIT - GUEST_MEM_MODULE_START;
    region.userspace_addr = (uintptr_t)mem + GUEST_MEM_MODULE_START;
    /*region.flags = KVM_MEM_READONLY;*/  /* Not needed, read-write is default. */
    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
      perror("fatal: ioctl KVM_SET_USER_MEMORY_REGION");
      exit(252);
    }
    if (GUEST_MEM_MODULE_START != 0) {
      memset(&region, 0, sizeof(region));
      region.slot = 1;
      region.guest_phys_addr = 0;
      region.memory_size = 0x1000;  /* Magic interrupt table: 0x500 bytes, rounded up to page boundary. */
      region.userspace_addr = (uintptr_t)mem;
      region.flags = KVM_MEM_READONLY;
      if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("fatal: ioctl KVM_SET_USER_MEMORY_REGION");
        exit(252);
      }
    }
    /* Extended memory above 1 MiB, used by protected-mode programs and
     * reported via int 15h/XMS/CMOS. Lazily faulted by the kernel.
     */
    if (emu_params->mem_mb > 1) {
      emu->xmem_size = ((unsigned long)emu_params->mem_mb - 1) << 20;
      if ((emu->xmem = mmap(NULL, emu->xmem_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)) == NULL) {
        perror("fatal: mmap xmem");
        exit(252);
      }
      memset(&region, 0, sizeof(region));
      region.slot = 2;
      region.guest_phys_addr = 0x100000;
      region.memory_size = emu->xmem_size;
      region.userspace_addr = (uintptr_t)emu->xmem;
      if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("fatal: ioctl KVM_SET_USER_MEMORY_REGION xmem");
        exit(252);
      }
    }
    if ((vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0)) < 0) {
      perror("fatal: can not create KVM vcpu");
      exit(252);
    }
    { /* Populate the vCPU's CPUID table so guest CPUID returns real results.
       * Without this, CPUID exits to the kernel which has no entries and
       * returns zeros, breaking DOS extender CPU detection (386/486/Pentium). */
      static struct { struct kvm_cpuid2 hdr; struct kvm_cpuid_entry2 entries[256]; } cpuid_data;
      cpuid_data.hdr.nent = 256;
      if (ioctl(kvm_fd, KVM_GET_SUPPORTED_CPUID, &cpuid_data) >= 0) {
        (void)ioctl(vcpu_fd, KVM_SET_CPUID2, &cpuid_data);  /* Best effort. */
      }
    }
    kvm_run_mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (kvm_run_mmap_size < 0) {
      perror("fatal: ioctl KVM_GET_VCPU_MMAP_SIZE");
      exit(252);
    }
    if ((emu->kvm_run = (struct kvm_run *)mmap(
        NULL, kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0)) == NULL) {
      perror("fatal: mmap kvm_run");
      exit(252);
    }
    if (ioctl(vcpu_fd, KVM_GET_REGS, &dummy_regs) < 0) {  /* We don't use the result; but we just check here that ioctl KVM_GET_REGS works. */
      perror("fatal: KVM_GET_REGS");
      exit(252);
    }
    if (ioctl(vcpu_fd, KVM_GET_SREGS, &emu->initial_sregs) < 0) {  /* Will be reused by DOS exec(). */
      perror("fatal: KVM_GET_SREGS");
      exit(252);
    }
    emu->kvm_fds.kvm_fd = kvm_fd; emu->kvm_fds.vm_fd = vm_fd; emu->kvm_fds.vcpu_fd = vcpu_fd;
  } else {
    mem = emu->mem;
    if (madvise((char*)mem + (((PSP_PARA << 4) + 0xfff) & ~0xfff), DOS_MEM_LIMIT - (((PSP_PARA << 4) + 0xfff) & ~0xfff), MADV_DONTNEED) != 0) {
      perror("fatal: madvise MADV_DONTNEED");
      exit(252);
    }
    if ((PSP_PARA << 4) & 0xfff) memset((char*) mem + (PSP_PARA << 4), '\0', -(PSP_PARA << 4) & 0xfff);  /* Partial page not cleared by madvise() above. */
    if (*(const unsigned*)((char*)mem + (PSP_PARA << 4)) != 0) {
      fprintf(stderr, "madvise failed to zero PSP\n");
      exit(252);
    }
    memset(mem, '\0', ENV_PARA << 4);
    memset((char*)mem + ENV_LIMIT, '\0', (PSP_PARA << 4) - ENV_LIMIT);
  }
}
