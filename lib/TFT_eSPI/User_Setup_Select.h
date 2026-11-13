#pragma once

// Arduino IDE carga la configuracion local. PlatformIO puede inyectar una
// configuracion de diagnostico con USER_SETUP_LOADED.
#ifndef USER_SETUP_LOADED
  #include "User_Setup.h"
#endif

// Seleccion local del driver para la CYD clasica. Esta parte es necesaria
// ademas del User_Setup.h: define los comandos TFT_SWRST, TFT_CASET, etc.
#if defined(ILI9341_DRIVER) || defined(ILI9341_2_DRIVER) || defined(ILI9342_DRIVER)
  #include "TFT_Drivers/ILI9341_Defines.h"
  #define TFT_DRIVER 0x9341
#else
  #error "La copia integrada solo admite ILI9341/ILI9341_2 en el firmware principal"
#endif
