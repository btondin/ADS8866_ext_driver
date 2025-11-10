# Driver Customizado Zephyr para ADS8866

[cite_start]Este projeto é uma implementação de exemplo de um driver "out-of-tree" para o **Zephyr RTOS**, focado em integrar o conversor Analógico-Digital (ADC) de 16 bits **Texas Instruments ADS8866** [cite: 11] com um microcontrolador da Nordic (nRF52832).

O objetivo principal é demonstrar como criar um módulo de driver customizado que implementa a API ADC padrão do Zephyr, comunicando-se com o sensor através da interface SPI e gerenciando pinos de controle específicos (como o `CONVST`).

## 🎯 Principais Funcionalidades

* **Driver Out-of-Tree:** Implementa a API de driver ADC do Zephyr (`adc_driver_api`) para o ADS8866, permitindo que o `main.c` use funções padrão como `adc_read()` e `adc_raw_to_millivolts_dt()`.
* [cite_start]**Interface SPI:** Utiliza a API SPIM moderna do Zephyr para comunicação com o ADC[cite: 6].
* [cite_start]**Controle do Pino CONVST:** Gerencia o pino `CONVST` (início de conversão) via GPIO[cite: 9], conforme exigido pelo datasheet do ADS8866.
* **Baseado em Devicetree:** Totalmente configurado via Devicetree, incluindo:
    * [cite_start]Um *binding* customizado (`ti,ADS8866.yaml`)[cite: 8].
    * [cite_start]Um *overlay* de aplicação (`bruno_nrf52832.overlay`) que habilita o `spi1` e desabilita periféricos conflitantes[cite: 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15].
* [cite_start]**Aplicação de Exemplo:** O `main.c` demonstra como inicializar o ADC e ler continuamente os valores brutos e em milivolts[cite: 13].

## 🛠️ Configuração de Hardware (Pinout)

Este projeto foi configurado para uma placa nRF52832. A conexão de hardware esperada, conforme definido em `bruno_nrf52832.overlay`, é:

| pino nRF52832 | Função      | Conectar ao pino do ADS8866 |
| :------------ | :---------- | :------------------------ |
| `P0.11`       | `SPI1_SCK`  | `SCLK`                    |
| `P0.12`       | `SPI1_MOSI` | `DIN`                     |
| `P0.13`       | `SPI1_MISO` | `DOUT`                    |
| `P0.10`       | `CS` (GPIO) | `CS`                      |
| `P0.09`       | `CONVST`    | `CONVST`                  |

[cite_start]**Nota:** O pino `DIN` do ADS8866 é usado como Chip Select (CS) quando operando no modo de 4 fios (CS-controlled), o que é o caso aqui[cite: 7].

## 🚀 Como Compilar e Usar

Este projeto é um módulo externo do Zephyr e deve ser posicionado em um local adequado dentro do seu ambiente nRF Connect SDK ou Zephyr.

1.  Clone este repositório.
2.  Certifique-se de que seu ambiente Zephyr está configurado.
3.  Compile e grave o projeto na sua placa de desenvolvimento (ex: `nrf52832_pca10040`):

    ```bash
    west build -b nrf52832_pca10040
    west flash
    ```

4.  Abra um terminal serial (ex: PuTTY, RealTerm) para ver a saída de log com as leituras do ADC.

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

## 📂 Estrutura do Projeto

* `/app`: Contém a lógica principal da aplicação (`main.c`).
* `/custom_driver_module`: O módulo do driver customizado.
    * `custom_ADS8866.c`: A implementação do driver.
    * `ti,ADS8866.yaml`: O *binding* do devicetree.
    * `Kconfig`/`CMakeLists.txt`: Arquivos que definem o driver como um módulo Zephyr.
* [cite_start]`bruno_nrf52832.overlay`: Arquivo de overlay do Devicetree para configurar os pinos e periféricos da placa[cite: 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15].
* `prj.conf`: Arquivo de configuração Kconfig do projeto.
* [cite_start]`.gitignore`: Lista de arquivos ignorados pelo Git, otimizado para Zephyr[cite: 1, 2].

## 📄 Licença

Este projeto é licenciado sob a **Licença MIT**. Veja o arquivo `LICENSE.md` para mais detalhes.
