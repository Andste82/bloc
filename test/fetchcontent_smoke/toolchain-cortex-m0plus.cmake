# Bare-metal consumer toolchain (FC-08): Cortex-M0+ with arm-none-eabi-gcc and newlib-nano stubs.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m0plus -mthumb -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nosys.specs -Wl,--gc-sections")
