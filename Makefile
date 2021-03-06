DIRS = init x86_64 memory lib64 device vm_monitor

IMAGE = Zelda64.img

include mk/kernel64.mk
include mk/bootloader64.mk

.PHONY: GUEST_CLEAN
GUEST_CLEAN:
	@make clean --no-print-directory -C guest

.PHONY:clean
clean: KERNEL_CLEAN BOOTLOADER_CLEAN GUEST_CLEAN
	@rm -f $(IMAGE)

.PHONY: GUEST_IMAGE
GUEST_IMAGE:
	@make --no-print-directory -C guest

.PHONY:image
image: GUEST_IMAGE KERNEL_IMAGE BOOTLOADER_IMAGE
	@echo "[IMAGE] $(IMAGE)"
	@dd conv=notrunc if=$(BL_BIN) of=$(IMAGE) status=none
	@dd conv=notrunc obs=512 if=$(BIN) of=$(IMAGE) seek=1 status=none


# KVM='--enable-kvm'
# DEBUG = '-d cpu_reset'
.PHONY:run
run:clean image
	@echo "[RUN] $(IMAGE)"
	@qemu-system-x86_64 $(DEBUG) $(KVM) -smp 4 -m 4096M -serial stdio -monitor null -nographic -vnc :100 -drive file=$(IMAGE),if=ide  -gdb tcp::5070
