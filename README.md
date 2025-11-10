# Zephyr Custom Driver for ADS8866

This project is a sample implementation of an "out-of-tree" driver for the **Zephyr RTOS**, focusing on integrating the **Texas Instruments ADS8866** 16-bit Analog-to-Digital Converter (ADC) with a Nordic microcontroller (nRF52832).

The primary goal is to demonstrate how to create a custom driver module that implements the standard Zephyr ADC API, communicates with the sensor via the SPI interface, and manages specific control pins (like `CONVST`).

## 🎯 Key Features

* **Out-of-Tree Driver:** Implements the Zephyr ADC driver API (`adc_driver_api`) for the ADS8866, allowing the `main.c` application to use standard functions like `adc_read()` and `adc_raw_to_millivolts_dt()`.
* **SPI Interface:** Utilizes the modern Zephyr SPIM API for communication with the ADC.
* [cite_start]**CONVST Pin Control:** Manages the `CONVST` (conversion start) pin via GPIO, as required by the ADS8866 datasheet[cite: 9].
* **Devicetree Driven:** Fully configured via Devicetree, including:
    * [cite_start]A custom binding (`ti,ADS8866.yaml`)[cite: 1].
    * [cite_start]An application overlay (`bruno_nrf52832.overlay`) that enables `spi1` and disables conflicting peripherals[cite: 5, 14].
* **Sample Application:** The `main.c` file demonstrates how to initialize the ADC and continuously read raw and millivolt values.

## 🛠️ Hardware Setup (Pinout)

This project is configured for an nRF52832 board. The expected hardware connection, as defined in `bruno_nrf52832.overlay`, is:

| nRF52832 Pin | Function    | Connect to ADS8866 Pin |
| :----------- | :---------- | :--------------------- |
| `P0.11`      | `SPI1_SCK`  | [cite_start]`SCLK` [cite: 15]      |
| `P0.12`      | `SPI1_MOSI` | [cite_start]`DIN` [cite: 15]       |
| `P0.13`      | `SPI1_MISO` | [cite_start]`DOUT` [cite: 15]      |
| `P0.10`      | `CS` (GPIO) | [cite_start]`CS` [cite: 7]         |
| `P0.09`      | `CONVST`    | [cite_start]`CONVST` [cite: 9]     |

[cite_start]**Note:** The ADS8866's `DIN` pin is used as Chip Select (CS) when operating in 4-wire (CS-controlled) mode, which is the case here[cite: 7].

## 🚀 How to Build and Use

This project is a Zephyr external module and should be placed in a suitable location within your nRF Connect SDK or Zephyr environment.

1.  Clone this repository.
2.  Ensure your Zephyr environment is set up.
3.  Build and flash the project to your development board (e.g., `nrf52832_pca10040`):

    ```bash
    west build -b nrf52832_pca10040
    west flash
    ```

4.  Open a serial terminal (e.g., PuTTY, RealTerm) to view the log output with the ADC readings.

    ```
    *** Booting Zephyr OS build v3.5.99-ncs1 ***
    [00:00:00.375,555] <inf> Lesson6_Exercise1: Aplicativo iniciado. Verificando dispositivos...
    [00:00:00.375,616] <inf> Lesson6_Exercise1: Dispositivo ADC MY_ADS8866@0 pronto.
    [00:00:00.375,769] <inf> Lesson6_Exercise1: Setup completo. Entrando no loop principal...
    [00:00:00.375,769] <inf> Lesson6_Exercise1: Tentando ler do ADC...
    [00:00:00.375,983] <inf> Lesson6_Exercise1: ADC reading[0]: MY_ADS8866@0, channel 0: Raw: 32768
    [00:00:00.375,983] <inf> Lesson6_Exercise1:  = 1650 mV
    [00:00:01.376,013] <inf> Lesson6_Exercise1: Tentando ler do ADC...
    [00:00:01.376,227] <inf> Lesson6_Exercise1: ADC reading[1]: MY_ADS8866@0, channel 0: Raw: 32769
    [00:00:01.376,227] <inf> Lesson6_Exercise1:  = 1650 mV
    ```
    *(Note: Log messages from `main.c` are in Portuguese as per the source file.)*

## 📂 Project Structure

* `/app`: Contains the main application logic (`main.c`).
* `/custom_driver_module`: The custom driver module.
    * `custom_ADS8866.c`: The driver implementation.
    * `ti,ADS8866.yaml`: The devicetree binding.
    * `Kconfig`/`CMakeLists.txt`: Files that define the driver as a Zephyr module.
* `bruno_nrf52832.overlay`: The Devicetree overlay file for configuring board pins and peripherals.
* `prj.conf`: The project's Kconfig configuration file.
* `.gitignore`: List of files ignored by Git, optimized for Zephyr.

## 📄 License

This project is licensed under the **MIT License**. See the `LICENSE.md` file for more details.
