.PHONY: all clean run test test-batch test-mem test-cli test-cli-matrix test-static test-sanitizers test-valgrind test-tooling test-x86dec ape
.SUFFIXES:
MAKEFLAGS += -r

ALL = kvikdos guest.com slowp.com malloct.com mallocs.com printenv.com cat.com waitkey.com

# -Werror=int-conversion: GCC 4.8.4 fails.
CFLAGS = -ansi -pedantic -s -O2 -W -Wall -Wextra -Wuninitialized -Wmaybe-uninitialized -Werror -fno-strict-aliasing -Wno-overlength-strings $(XCFLAGS)
XCFLAGS =  # To be overridden from the command-line.

# Split translation units (was a single kvikdos.c). kvikdos.h is the shared
# internal header with the common types, constants, externs and prototypes.
KVIKDOS_SRCS = util.c diag.c resolve.c dospath.c args.c argsparse.c argspost.c batchline.c batchcmd_a.c batchcmd_b.c batchcmd_c.c loader.c mzdetect.c fds.c video.c tty.c vm.c hv_kvm.c hv_whpx.c hvpick.c x86dec.c spawn.c runstate.c run.c vmexit.c intmisc.c intdos.c intcon.c intopen.c intio.c intmem.c intfcb.c intfind.c intexec.c intdmisc.c intvid.c intems.c intxms.c batch.c main.c
SRCDEPS = $(KVIKDOS_SRCS) kvikdos.h mini_kvm.h mini_whpx.h hv.h x86dec.h

all: $(ALL)

clean:
	rm -f $(ALL) kvikdos32 kvikdos64 kvikdos.static kvikdos.ape

run: kvikdos guest.com
	./kvikdos guest.com hello world

test: test-batch test-mem test-cli test-cli-matrix

test-batch: kvikdos
	./tests/test_batch.sh ./kvikdos

test-mem: kvikdos
	./tests/test_mem_services.sh ./kvikdos

test-cli: kvikdos
	./tests/test_cli_parse.sh ./kvikdos

test-cli-matrix: kvikdos
	./tests/test_cli_matrix.sh ./kvikdos

test-static:
	./tests/test_static.sh

test-sanitizers:
	./tests/test_sanitizers.sh

test-valgrind: kvikdos
	./tests/test_valgrind.sh ./kvikdos

test-tooling: test-static test-sanitizers test-valgrind

%.com: %.nasm
	nasm -O0 -f bin -o $@ $<

kvikdos: $(SRCDEPS)
	gcc $(CFLAGS) -o $@ $(KVIKDOS_SRCS)

kvikdos32: $(SRCDEPS)
	gcc -m32 -fno-pic -march=i686 -mtune=generic $(CFLAGS) -o $@ $(KVIKDOS_SRCS)

kvikdos64: $(SRCDEPS)
	gcc -m64 -march=k8 -mtune=generic $(CFLAGS) -o $@ $(KVIKDOS_SRCS)

kvikdos.static: $(SRCDEPS)
	xstatic gcc -m32 -fno-pic -D_FILE_OFFSET_BITS=64 -DUSE_MINI_KVM -march=i686 -mtune=generic $(CFLAGS) -o $@ $(KVIKDOS_SRCS)

kvikdos.diet: $(SRCDEPS)
	minicc --gcc=4.8 --diet -DUSE_MINI_KVM -fno-strict-aliasing -o kvikdos.diet $(KVIKDOS_SRCS)

# Cosmopolitan APE build: one x86-64 binary that runs on Linux (KVM
# backend) and Windows (WHPX backend, picked via IsWindows()). The
# single-arch unknown-cosmo target is used deliberately: KVM/WHPX are
# x86-only hypervisors, so the aarch64 half of a fat cosmopolitan binary
# would be dead weight. Stripped for release. Copy/rename kvikdos.ape to
# kvikdos.com (or .exe) to run it on Windows.
COSMOCC ?= x86_64-unknown-cosmo-cc
COSTRIP ?= x86_64-unknown-cosmo-strip
kvikdos.ape: $(SRCDEPS)
	$(COSMOCC) -O2 -o $@ $(KVIKDOS_SRCS)
	$(COSTRIP) $@

ape: kvikdos.ape

# Host-native unit test for the mini instruction decoder the WHPX backend
# uses to complete memory-access exits (no guest or hypervisor needed).
test-x86dec: tests/test_x86dec.c x86dec.c x86dec.h mini_kvm.h
	gcc -O2 -Wall -Wextra -I. -o /tmp/test_x86dec tests/test_x86dec.c x86dec.c
	/tmp/test_x86dec
