#ifndef X86DEC_H
#define X86DEC_H

/* Minimal x86-16/32 instruction decoder for the WHPX backend. WHPX memory-
 * access exits provide instruction bytes but not the operand value (writes)
 * or destination register (reads) -- unlike KVM, which completes the access
 * itself. These helpers recover just enough: the stored value, or the
 * register a load targets so it can be filled with the served data.
 * Only forms DOS tools actually emit are covered; anything else returns -1
 * and the caller skips the instruction by its reported byte count. */

/* Store: put the value the instruction writes into *value, its size (1/2/4)
 * into *size. Returns 0 on success, -1 if the form isn't covered. */
int x86dec_store(const unsigned char *inst, unsigned instlen,
                 const struct kvm_regs *regs, const struct kvm_sregs *sregs,
                 unsigned long *value, unsigned *size);

/* Load: write `fill` (len bytes) into the register the load targets
 * (or a segment register / rflags). Returns 0 on success, -1 otherwise. */
int x86dec_load(const unsigned char *inst, unsigned instlen,
                struct kvm_regs *regs, struct kvm_sregs *sregs,
                unsigned long fill, unsigned fill_len);

/* Access size of the memory operand a load/store touches (1/2/4); -1 if
 * the form isn't covered. Used to size the mmio read buffer. */
int x86dec_access_size(const unsigned char *inst, unsigned instlen);

#endif
