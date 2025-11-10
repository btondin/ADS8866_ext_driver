# custom_driver_module/drivers/CMakeLists.txt

# Adiciona o subdiretório do driver SOMENTE SE a Kconfig estiver habilitada
add_subdirectory_ifdef(CONFIG_ADC_ADS8866 custom_ADS8866)