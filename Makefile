CC = clang

LIBBPFSRC = libbpf/src

LIBBPFOBJS += $(LIBBPFSRC)/staticobjs/bpf_prog_linfo.o $(LIBBPFSRC)/staticobjs/bpf.o $(LIBBPFSRC)/staticobjs/btf_dump.o
LIBBPFOBJS += $(LIBBPFSRC)/staticobjs/btf.o $(LIBBPFSRC)/staticobjs/hashmap.o $(LIBBPFSRC)/staticobjs/libbpf_errno.o
LIBBPFOBJS += $(LIBBPFSRC)/staticobjs/libbpf_probes.o $(LIBBPFSRC)/staticobjs/libbpf.o $(LIBBPFSRC)/staticobjs/netlink.o
LIBBPFOBJS += $(LIBBPFSRC)/staticobjs/nlattr.o $(LIBBPFSRC)/staticobjs/ringbuf.o $(LIBBPFSRC)/staticobjs/str_error.o
LIBBPFOBJS += $(LIBBPFSRC)/staticobjs/xsk.o

LOADEROBJS += src/xdpfwd.o
LOADERFLAGS += -lelf -lz

ADDOBJS += src/xdpfwd-add.o

all: loader xdp_add xdp_prog
loader: libbpf $(LOADEROBJS)
	clang -I$(LIBBPFSRC) $(LOADERFLAGS) -O1 -o xdpfwd $(LIBBPFOBJS) $(LOADEROBJS)
xdp_add: libbpf
	clang -I$(LIBBPFSRC) $(LOADERFLAGS) -o xdpfwd-add $(LIBBPFOBJS) src/xdpfwd-add.c
xdp_prog:
	clang -I$(LIBBPFSRC) -D__BPF__ -Wall -Wextra -O2 -emit-llvm -c src/xdp_prog.c -o src/xdp_prog.bc
	llc -march=bpf -filetype=obj src/xdp_prog.bc -o src/xdp_prog.o
libbpf:
	$(MAKE) -C $(LIBBPFSRC)
clean:
	$(MAKE) -C $(LIBBPFSRC) clean
	rm -f src/*.o src/*.bc
	rm -f xdpfwd
.PHONY: libbpf all
.DEFAULT: all
