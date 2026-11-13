// CONFIGURACION TFT_eSPI PARA LA PLACA ORIGINAL DEL USUARIO
// ESP32-2432S028 / CYD 320x240 con controlador ILI9341.
//
// CONFIGURACION INTEGRADA: la copia local de TFT_eSPI carga este archivo.
// No copiarlo a Documents/Arduino/libraries ni ejecutar instaladores externos.

#define USER_SETUP_INFO "ESP32-2432S028R CNC POLIFAN ILI9341_2"
#define USER_SETUP_ID 2432028
#define DISABLE_ALL_LIBRARY_WARNINGS

// La CYD clasica necesita la secuencia de encendido alternativa. La version
// 2.0.3 compilaba por error ILI9341_DRIVER y podia dejar el panel en blanco.
#define ILI9341_2_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1

#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// NO definir USE_HSPI_PORT aqui. La configuracion original funcionaba
// dejando que TFT_eSPI inicie el SPI con estos pines personalizados.

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// 20 MHz prioriza compatibilidad con paneles clon durante la puesta en marcha.
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  16000000

// El touch de esta placa esta en OTRO bus SPI (25/39/32/33).
// Por eso no se define TOUCH_CS en TFT_eSPI; el firmware usa touch_driver.cpp.
