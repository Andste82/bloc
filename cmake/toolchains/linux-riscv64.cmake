# Tier-1E target riscv64: Linux cross GCC, static linking, tests run under qemu user-mode emulation.
# Static linking plus an explicit emulator lets ctest run the binaries without a sysroot and
# without binfmt_misc.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-riscv64)
