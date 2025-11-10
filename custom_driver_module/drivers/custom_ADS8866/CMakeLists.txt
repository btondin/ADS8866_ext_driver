zephyr_library()
zephyr_library_sources(custom_ADS8866.c)

# Adiciona o diretório do adc_context.h ao include path
zephyr_include_directories(${ZEPHYR_BASE}/drivers/adc)