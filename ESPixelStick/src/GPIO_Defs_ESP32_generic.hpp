#pragma once
/*
* GPIO_Defs_ESP32_generic.hpp - Output Management class
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2021 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*/

//Output Manager
#define DEFAULT_UART_1_GPIO     gpio_num_t::GPIO_NUM_2
#define DEFAULT_UART_2_GPIO     gpio_num_t::GPIO_NUM_13
#define UART_LAST               OutputChannelId_UART_2

#define SUPPORT_RMT_OUTPUT
#define DEFAULT_RMT_0_GPIO      gpio_num_t::GPIO_NUM_12
#define DEFAULT_RMT_1_GPIO      gpio_num_t::GPIO_NUM_14
#define DEFAULT_RMT_2_GPIO      gpio_num_t::GPIO_NUM_32
#define DEFAULT_RMT_3_GPIO      gpio_num_t::GPIO_NUM_33
#define RMT_LAST                OutputChannelId_RMT_4

// #define SUPPORT_OutputType_WS2801    // requires a change in the html directory
// #define SUPPORT_OutputType_APA102    // requires a change in the html directory
// #define SUPPORT_OutputType_TM1814    // requires a change in the html directory
// #define SUPPORT_OutputType_TLS3001   // requires a change in the html directory

#define SUPPORT_RELAY_OUTPUT

#if defined(SUPPORT_OutputType_WS2801) || defined(SUPPORT_OutputType_APA102)
#   define SUPPORT_SPI_OUTPUT

// SPI Output
#define DEFAULT_SPI_DATA_GPIO  gpio_num_t::GPIO_NUM_15
#define DEFAULT_SPI_CLOCK_GPIO gpio_num_t::GPIO_NUM_25

#endif // defined(SUPPORT_OutputType_WS2801) || defined(SUPPORT_OutputType_TM1814)

// File Manager
#define SD_CARD_MISO_PIN        gpio_num_t::GPIO_NUM_19
#define SD_CARD_MOSI_PIN        gpio_num_t::GPIO_NUM_23
#define SD_CARD_CLK_PIN         gpio_num_t::GPIO_NUM_18
#define SD_CARD_CS_PIN          gpio_num_t::GPIO_NUM_4
