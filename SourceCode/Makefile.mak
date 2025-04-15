
# Kernel module name
MODULE_NAME := pwm_led_controller

# Kernel module source file
MODULE_SRC := $(MODULE_NAME).c

# Kernel build directory
KDIR := /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Rust compiler and flags
RUSTC := rustc
RUSTFLAGS := -O

# Rust source files
RUST_SRC_DEV := device_driver.rs
RUST_SRC_SYSFS := sysfs.rs

# Binary output names
RUST_BIN_DEV := device_driver
RUST_BIN_SYSFS := sysfs

# Default target builds everything
all: module rust_apps

# Kernel module targets
module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean_module:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install_module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a
	# Load the module
	modprobe $(MODULE_NAME) || insmod ./$(MODULE_NAME).ko

uninstall_module:
	# Unload the module
	rmmod $(MODULE_NAME) || true
	rm -f /lib/modules/$(shell uname -r)/extra/$(MODULE_NAME).ko
	depmod -a

# Rust application targets
rust_apps: $(RUST_BIN_DEV) $(RUST_BIN_SYSFS)

$(RUST_BIN_DEV): $(RUST_SRC_DEV)
	$(RUSTC) $(RUSTFLAGS) -o $@ $<

$(RUST_BIN_SYSFS): $(RUST_SRC_SYSFS)
	$(RUSTC) $(RUSTFLAGS) -o $@ $<

clean_rust:
	rm -f $(RUST_BIN_DEV) $(RUST_BIN_SYSFS)

# Combined targets
clean: clean_module clean_rust

install: install_module
	# Install the binaries to /usr/local/bin (requires sudo)
	install -m 755 $(RUST_BIN_DEV) /usr/local/bin/
	install -m 755 $(RUST_BIN_SYSFS) /usr/local/bin/

uninstall: uninstall_module
	# Remove the binaries from /usr/local/bin
	rm -f /usr/local/bin/$(RUST_BIN_DEV)
	rm -f /usr/local/bin/$(RUST_BIN_SYSFS)


obj-m := $(MODULE_NAME).o


.PHONY: all module rust_apps clean clean_module clean_rust install install_module uninstall uninstall_module