cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zephyr_low_energy_demo)

target_sources(app PRIVATE src/main.c)

# En este Windows, ld.bfd no puede cargar liblto_plugin.dll del SDK 1.0.1.
# La demo no activa LTO, asi que evitamos que GCC intente cargar ese plugin.
if(CMAKE_HOST_WIN32)
  zephyr_link_libraries(-fno-use-linker-plugin)
endif()
