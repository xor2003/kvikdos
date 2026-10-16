#include "kvikdos.h"

/* Channel 0 of the 8253/8254. Access sequencing and count latching follow
 * msdos_player's pit_write/pit_read; IRQ periods follow the programmed
 * divisor, not the BIOS's default 18.2 Hz. Missed edges coalesce in the PIC. */
#define PIT_HZ 1193182.0
static unsigned reload = 65536;
static unsigned access_mode = 3, mode = 3;
static unsigned write_low, write_high, read_high, latched, latch_valid;
static double epoch, deadline;
static int running;

static double pit_time(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void pit_reset(void) {
  reload = 65536; access_mode = 3; mode = 3;
  write_low = write_high = read_high = latch_valid = 0;
  epoch = pit_time(); deadline = epoch + reload / PIT_HZ;
  running = 1;
}

static unsigned pit_count(void) {
  const double ticks = (pit_time() - epoch) * PIT_HZ;
  const unsigned phase = (unsigned)(ticks - (unsigned long)(ticks / reload) * (double)reload);
  unsigned count;
  if (mode == 3) count = reload - (phase * 2) % reload;
  else if (mode == 2) count = reload - phase;
  else count = ticks >= reload ? 0 : reload - (unsigned)ticks;
  return count & 0xffff;
}

static void pit_load(unsigned value) {
  struct itimerval it;
  unsigned long us;
  reload = value ? value : 65536;
  epoch = pit_time(); deadline = epoch + reload / PIT_HZ;
  running = 1;
  /* Wake CPU-bound guests at the new frequency too. A 1 ms floor bounds
   * host overhead for tiny divisors; overdue PIC edges still coalesce. */
  us = (unsigned long)(reload * (1000000.0 / PIT_HZ));
  if (us < 1000) us = 1000;
  memset(&it, 0, sizeof(it));
  it.it_interval.tv_usec = us;
  it.it_value = it.it_interval;
  (void)setitimer(ITIMER_REAL, &it, NULL);
}

void pit_port(unsigned port, int output, unsigned char *value) {
  if (port == 0x43) {
    if (!output || (*value >> 6) != 0) return;
    if ((*value & 0x30) == 0) {
      if (!latch_valid) { latched = pit_count(); latch_valid = 1; read_high = 0; }
    } else {
      access_mode = (*value >> 4) & 3;
      mode = (*value >> 1) & 7;
      if (mode > 5) mode -= 4;
      write_high = read_high = latch_valid = 0;
    }
  } else if (output) {
    if (access_mode == 1) pit_load(*value);
    else if (access_mode == 2) pit_load((unsigned)*value << 8);
    else if (!write_high) { write_low = *value; write_high = 1; }
    else { write_high = 0; pit_load(write_low | ((unsigned)*value << 8)); }
  } else {
    const unsigned count = latch_valid ? latched : pit_count();
    if (access_mode == 2 || read_high) {
      *value = (unsigned char)(count >> 8);
      read_high = latch_valid = 0;
    } else {
      *value = (unsigned char)count;
      if (access_mode == 3) {
        latched = count; latch_valid = 1; read_high = 1;
      } else latch_valid = 0;
    }
  }
}

int pit_irq_due(void) {
  const double now = pit_time();
  const double period = reload / PIT_HZ;
  if (!running || now < deadline) return 0;
  if (mode == 2 || mode == 3)
    deadline += ((unsigned long)((now - deadline) / period) + 1) * period;
  else running = 0;
  return 1;
}
