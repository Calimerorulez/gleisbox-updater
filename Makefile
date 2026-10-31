CC = cc
CROSS_COMPILE ?= mipsel-openwrt-linux-
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Werror
WRAPS = -Wl,--wrap=write -Wl,--wrap=socket -Wl,--wrap=send -Wl,--wrap=clock_gettime -Wl,--wrap=nanosleep
FIRMWARE ?=

.PHONY: all test sanitize omega2 clean
all: build/gb2-recovery
build:
	mkdir -p $@
build/gb2-recovery: src/gb2-recovery.c | build
	$(CC) $(CFLAGS) $(WARN) $< -o $@
build/test-recovery: tests/test_recovery.c src/gb2-recovery.c | build
	$(CC) $(CFLAGS) $(WARN) $< $(WRAPS) -o $@
test: build/test-recovery
	@test -n "$(FIRMWARE)" || { echo 'Set FIRMWARE to your local 016-gb2.bin (1.70)'; exit 2; }
	./build/test-recovery "$(FIRMWARE)"
sanitize: | build
	@test -n "$(FIRMWARE)" || { echo 'Set FIRMWARE to your local 016-gb2.bin (1.70)'; exit 2; }
	clang -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(WARN) tests/test_recovery.c $(WRAPS) -o build/test-recovery-sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/test-recovery-sanitize "$(FIRMWARE)"
omega2: | build
	mkdir -p build/omega2
	$(CROSS_COMPILE)gcc -Os -g $(WARN) -EL -mabi=32 -mips32r2 -mtune=24kc -msoft-float -mno-mips16 -mno-micromips -ffunction-sections -fdata-sections src/gb2-recovery.c -static -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack -o build/omega2/gb2-recovery
	$(CROSS_COMPILE)strip --strip-all -o build/omega2/gb2-recovery.stripped build/omega2/gb2-recovery
clean:
	rm -rf build
