#include "kvikdos.h"
#include "hv.h"
#ifdef __COSMOCC__
#  include <cosmo.h>
#endif

/* Backend selection plus the hv_ops vtable dispatch. The cosmopolitan APE
 * build can run on either host, so it picks at runtime; other builds are
 * fixed at compile time. */

struct hv *hv_create(void) {
#if defined(__COSMOCC__)
  if (IsWindows()) return hv_whpx_create();
#endif
#if defined(_WIN32)
  return hv_whpx_create();
#else
  return hv_kvm_create();
#endif
}

void hv_destroy(struct hv *hv) {
  if (hv) { hv->ops->destroy(hv); free(hv); }
}

const char *hv_name(const struct hv *hv) {
  return hv->ops->name;
}

int hv_set_memory(struct hv *hv, unsigned slot, unsigned long gpa,
                  unsigned long size, void *host, int readonly) {
  return hv->ops->set_memory(hv, slot, gpa, size, host, readonly);
}

int hv_create_vcpu(struct hv *hv) {
  return hv->ops->create_vcpu(hv);
}

int hv_run(struct hv *hv, struct hv_exit *hx) {
  return hv->ops->run(hv, hx);
}

int hv_get_fds(const struct hv *hv, int *out, int n) {
  return hv->ops->get_fds(hv, out, n);
}

int hv_get_regs(struct hv *hv, struct kvm_regs *regs) {
  return hv->ops->get_regs(hv, regs);
}

int hv_set_regs(struct hv *hv, const struct kvm_regs *regs) {
  return hv->ops->set_regs(hv, regs);
}

int hv_get_sregs(struct hv *hv, struct kvm_sregs *sregs) {
  return hv->ops->get_sregs(hv, sregs);
}

int hv_set_sregs(struct hv *hv, const struct kvm_sregs *sregs) {
  return hv->ops->set_sregs(hv, sregs);
}
int hv_interrupt(struct hv *hv, unsigned irq_line) {
  return hv->ops->interrupt ? hv->ops->interrupt(hv, irq_line) : -1;
}
