/*
* UCS1903Uart.cpp - UCS1903 driver code for ESPixelStick UART
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2015, 2022 Shelby Merrick
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

#include "../ESPixelStick.h"
#if defined(SUPPORT_OutputType_UCS1903) && defined(SUPPORT_UART_OUTPUT)

#include "OutputUCS1903Uart.hpp"

/*
 * Inverted 6N1 UART lookup table for UCS1903, first 2 bits ignored.
 * Start and stop bits are part of the pixel stream.
 */
struct Convert2BitIntensityToUCS1903UartDataStreamEntry_t
{
    uint8_t Translation;
    c_OutputUart::UartDataBitTranslationId_t Id;
};
const Convert2BitIntensityToUCS1903UartDataStreamEntry_t PROGMEM Convert2BitIntensityToUCS1903UartDataStream[] =
{
    {0b00110111, c_OutputUart::UartDataBitTranslationId_t::Uart_DATA_BIT_00_ID}, // 00 - (1)000 100(0)
    {0b00000111, c_OutputUart::UartDataBitTranslationId_t::Uart_DATA_BIT_01_ID}, // 01 - (1)000 111(0)
    {0b00110100, c_OutputUart::UartDataBitTranslationId_t::Uart_DATA_BIT_10_ID}, // 10 - (1)110 100(0)
    {0b00000100, c_OutputUart::UartDataBitTranslationId_t::Uart_DATA_BIT_11_ID}, // 11 - (1)110 111(0)
};

//----------------------------------------------------------------------------
c_OutputUCS1903Uart::c_OutputUCS1903Uart (c_OutputMgr::e_OutputChannelIds OutputChannelId,
    gpio_num_t outputGpio,
    uart_port_t uart,
    c_OutputMgr::e_OutputType outputType) :
    c_OutputUCS1903 (OutputChannelId, outputGpio, uart, outputType)
{
    // DEBUG_START;

    // DEBUG_END;
} // c_OutputUCS1903Uart

//----------------------------------------------------------------------------
c_OutputUCS1903Uart::~c_OutputUCS1903Uart ()
{
    // DEBUG_START;

    // DEBUG_END;
} // ~c_OutputUCS1903Uart

//----------------------------------------------------------------------------
/* Use the current config to set up the output port
*/
void c_OutputUCS1903Uart::Begin ()
{
    // DEBUG_START;

    c_OutputUCS1903::Begin();

    for (auto CurrentTranslation : Convert2BitIntensityToUCS1903UartDataStream)
    {
        Uart.SetIntensity2Uart(CurrentTranslation.Translation, CurrentTranslation.Id);
    }

    SetIntensityBitTimeInUS(float(UCS1903_PIXEL_NS_BIT_TOTAL) / 1000.0);

    c_OutputUart::OutputUartConfig_t OutputUartConfig;
    OutputUartConfig.ChannelId                     = OutputChannelId;
    OutputUartConfig.UartId                        = UartId;
    OutputUartConfig.DataPin                       = DataPin;
    OutputUartConfig.IntensityDataWidth            = UCS1903_NUM_DATA_BYTES_PER_INTENSITY_BYTE;
    OutputUartConfig.UartDataSize                  = c_OutputUart::UartDataSize_t::OUTPUT_UART_6N1;
    OutputUartConfig.TranslateIntensityData        = c_OutputUart::TranslateIntensityData_t::TwoToOne;
    OutputUartConfig.pPixelDataSource              = this;
    OutputUartConfig.Baudrate                      = int(UCS1903_NUM_DATA_BYTES_PER_INTENSITY_BYTE * UCS1903_PIXEL_DATA_RATE);;
    OutputUartConfig.InvertOutputPolarity          = true;
    Uart.Begin(OutputUartConfig);

#ifdef testPixelInsert
    static const uint32_t FrameStartData = 0;
    static const uint32_t FrameEndData = 0xFFFFFFFF;
    static const uint8_t  PixelStartData = 0xC0;

    SetFramePrependInformation ( (uint8_t*)&FrameStartData, sizeof (FrameStartData));
    SetFrameAppendInformation ( (uint8_t*)&FrameEndData, sizeof (FrameEndData));
    SetPixelPrependInformation (&PixelStartData, sizeof (PixelStartData));
#endif // def testPixelInsert

    HasBeenInitialized = true;

} // init

//----------------------------------------------------------------------------
bool c_OutputUCS1903Uart::SetConfig (ArduinoJson::JsonObject& jsonConfig)
{
    // DEBUG_START;

    bool response = c_OutputUCS1903::SetConfig (jsonConfig);
    response |= Uart.SetConfig(jsonConfig);

    // DEBUG_END;
    return response;

} // SetConfig

//----------------------------------------------------------------------------
void c_OutputUCS1903Uart::GetConfig(ArduinoJson::JsonObject &jsonConfig)
{
    // DEBUG_START;

    c_OutputUCS1903::GetConfig(jsonConfig);
    Uart.GetConfig(jsonConfig);

    // DEBUG_END;

} // GetConfig

//----------------------------------------------------------------------------
void c_OutputUCS1903Uart::GetStatus(ArduinoJson::JsonObject &jsonStatus)
{
    // DEBUG_START;

    c_OutputUCS1903::GetStatus(jsonStatus);
    Uart.GetStatus(jsonStatus);

#ifdef UCS1903_UART_DEBUG_COUNTERS
    JsonObject debugStatus = jsonStatus.createNestedObject("UCS1903 UART Debug");
    debugStatus["NewFrameCounter"] = NewFrameCounter;
    debugStatus["TimeSinceLastFrameMS"] = TimeSinceLastFrameMS;
    debugStatus["TimeLastFrameStartedMS"] = TimeLastFrameStartedMS;
#endif // def UCS1903_UART_DEBUG_COUNTERS

    // DEBUG_END;

} // GetStatus

//----------------------------------------------------------------------------
void c_OutputUCS1903Uart::Render ()
{
    // DEBUG_START;

    // DEBUG_V (String ("RemainingIntensityCount: ") + RemainingIntensityCount)

    if (gpio_num_t (-1) == DataPin) { return; }
    if (!canRefresh ()) { return; }

    // get the next frame started
    StartNewFrame ();
    ReportNewFrame ();

    // DEBUG_END;

} // render

//----------------------------------------------------------------------------
void c_OutputUCS1903Uart::PauseOutput (bool State)
{
    // DEBUG_START;

    c_OutputUCS1903::PauseOutput(State);
    Uart.PauseOutput(State);

    // DEBUG_END;
} // PauseOutput

#endif // defined(SUPPORT_OutputType_UCS1903) && defined(SUPPORT_UART_OUTPUT)
