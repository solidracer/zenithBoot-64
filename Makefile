cc = gcc
ld = ld
objcopy = objcopy

src = $(wildcard src/*.c)
obj = $(patsubst src/%.c,build/%.o,$(src))

# only the "ZENITH_QUIET" flag is supported
# make flags=-DZENITH_QUIET
flags ?= 

cflags = $(flags) -DEFIAPI=\_\_attribute\_\_\(\(ms_abi\)\) -I/usr/include/efi/ -fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
ldflags = -shared -Bsymbolic -L/usr/lib -T/usr/lib/elf_x86_64_efi.lds /usr/lib/crt0-efi-x86_64.o

target = build/main.efi

all: $(target)

./build:
	@mkdir -p build

$(target): ./build build/build.so
	$(objcopy) -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym  -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc --target efi-app-x86_64 --subsystem=10 build/build.so $(target)

build/build.so: $(obj)
	$(ld) $(ldflags) $(obj) -o build/build.so -lgnuefi -lefi

build/%.o: src/%.c
	$(cc) $(cflags) -c $< -o $@

boot: $(target)
	uefi-run -b OVMF/OVMF.fd -q /usr/bin/qemu-system-x86_64 -f kernel.elf $(target) --boot -- -cpu host -enable-kvm -m 3G

clean:
	@rm -rf build

.PHONY: clean all boot
