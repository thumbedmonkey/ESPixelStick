#pragma once
/*
* OutputRmt.hpp - RMT driver code for ESPixelStick RMT Channel
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2015 Shelby Merrick
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
#ifdef SUPPORT_RMT_OUTPUT
#include <driver/rmt.h>
#include "OutputPixel.hpp"
#include "OutputSerial.hpp"

class c_OutputRmt
{
public:
    enum RmtDataBitIdType_t
    {
        RMT_DATA_BIT_ZERO_ID = 0,   // UART 00
        RMT_DATA_BIT_ONE_ID,        // UART 01
        RMT_DATA_BIT_TWO_ID,        // UART 10
        RMT_DATA_BIT_THREE_ID,      // UART 11
        RMT_INTERFRAME_GAP_ID,      // UART Break / MAB
        RMT_STARTBIT_ID,
        RMT_STOPBIT_ID,             // UART Stop/start bit
        RMT_END_OF_FRAME,
        RMT_LIST_END,
        RMT_INVALID_VALUE,
        RMT_STOP_START_BIT_ID = RMT_STOPBIT_ID,
    };

    struct OutputRmtConfig_t
    {
        rmt_channel_t    RmtChannelId       = rmt_channel_t(-1);
        gpio_num_t       DataPin            = gpio_num_t(-1);
        rmt_idle_level_t idle_level         = rmt_idle_level_t::RMT_IDLE_LEVEL_LOW;
        c_OutputPixel    *pPixelDataSource  = nullptr;
        c_OutputSerial   *pSerialDataSource = nullptr;
    };

private:
#define RMT_INT_TX_END     (1)
#define RMT_INT_RX_END     (2)
#define RMT_INT_ERROR      (4)
#define RMT_INT_THR_EVNT   (1<<24)

#define RMT_INT_TX_END_BIT      (RMT_INT_TX_END   << (uint32_t (OutputRmtConfig.RmtChannelId)*3))
#define RMT_INT_RX_END_BIT      (RMT_INT_RX_END   << (uint32_t (OutputRmtConfig.RmtChannelId)*3))
#define RMT_INT_ERROR_BIT       (RMT_INT_ERROR    << (uint32_t (OutputRmtConfig.RmtChannelId)*3))
#define RMT_INT_THR_EVNT_BIT    (RMT_INT_THR_EVNT << (uint32_t (OutputRmtConfig.RmtChannelId)))

    OutputRmtConfig_t   OutputRmtConfig;

    rmt_item32_t        Intensity2Rmt[RmtDataBitIdType_t::RMT_LIST_END];
    bool                OutputIsPaused   = false;

    uint8_t             NumInterFrameRmtSlots             = 6;
    uint8_t             NumFrameStartRmtSlots             = 1;
    uint8_t             NumFrameStopRmtSlots              = 1;
    bool                SendInterIntensityBits            = false;
    bool                SendEndOfFrameBits                = false;
    uint32_t            NumRmtSlotsPerIntensityValue      = 8;
    uint32_t            NumRmtSlotOverruns                = 0;

    rmt_isr_handle_t       RMT_intr_handle = NULL;
    volatile rmt_item32_t *RmtStartAddr    = nullptr;
    volatile rmt_item32_t *RmtCurrentAddr  = nullptr;
    volatile rmt_item32_t *RmtEndAddr      = nullptr;

#define NUM_RMT_SLOTS (sizeof(RMTMEM.chan[0].data32) / sizeof(RMTMEM.chan[0].data32[0]))
#define MIN_FRAME_TIME_MS 25

    volatile size_t     NumAvailableRmtSlotsToFill = NUM_RMT_SLOTS;
    const size_t        NumRmtSlotsPerInterrupt    = NUM_RMT_SLOTS * 0.75;
    uint32_t            LastFrameStartTime         = 0;
    uint32_t            FrameMinDurationInMicroSec = 1000;

    void                  StartNewFrame ();
    void        IRAM_ATTR ISR_Handler_SendIntensityData ();
    inline void IRAM_ATTR ISR_EnqueueData(uint32_t value);

    long                IntensityMapDstMax          = 255;
    uint32_t            TxIntensityDataStartingMask = 0x80;
    RmtDataBitIdType_t  InterIntensityValueId       = RMT_INVALID_VALUE;

    bool MoreDataToSend()
    {
        if (nullptr != OutputRmtConfig.pPixelDataSource)
        {
            return OutputRmtConfig.pPixelDataSource->ISR_MoreDataToSend();
        }
        return OutputRmtConfig.pSerialDataSource->ISR_MoreDataToSend();
    }

    uint8_t GetNextIntensityToSend()
    {
        if (nullptr != OutputRmtConfig.pPixelDataSource)
        {
            return OutputRmtConfig.pPixelDataSource->ISR_GetNextIntensityToSend();
        }
        return OutputRmtConfig.pSerialDataSource->ISR_GetNextIntensityToSend();
    }

    void StartNewDataFrame()
    {
        if (nullptr != OutputRmtConfig.pPixelDataSource)
        {
            return OutputRmtConfig.pPixelDataSource->StartNewFrame();
        }
        return OutputRmtConfig.pSerialDataSource->StartNewFrame();
    }

#ifndef HasBeenInitialized
        bool HasBeenInitialized = false;
#endif // ndef HasBeenInitialized

    TaskHandle_t SendIntensityDataTaskHandle = NULL;

public:
    c_OutputRmt ();
    virtual ~c_OutputRmt ();

    void Begin                                  (OutputRmtConfig_t config);
    bool Render                                 ();
    void GetStatus                              (ArduinoJson::JsonObject& jsonStatus);
    void set_pin                                (gpio_num_t _DataPin) { OutputRmtConfig.DataPin = _DataPin; rmt_set_gpio (OutputRmtConfig.RmtChannelId, rmt_mode_t::RMT_MODE_TX, OutputRmtConfig.DataPin, false); }
    void PauseOutput                            (bool State);
    void SetNumIdleBits                         (uint8_t Value)  { NumInterFrameRmtSlots      = Value; }
    void SetNumStartBits                        (uint8_t Value)  { NumFrameStartRmtSlots      = Value; }
    void SetNumStopBits                         (uint8_t Value)  { NumFrameStopRmtSlots       = Value; }
    void SetSendInterIntensityBits              (bool Value)     { SendInterIntensityBits     = Value; }
    void SetSendEndOfFrameBits                  (bool Value)     { SendEndOfFrameBits         = Value; }
    void SetMinFrameDurationInUs                (uint32_t value) { FrameMinDurationInMicroSec = value; }
    inline uint32_t IRAM_ATTR GetRmtIntMask     ()               { return ((RMT_INT_TX_END_BIT | RMT_INT_ERROR_BIT | RMT_INT_ERROR_BIT | RMT_INT_THR_EVNT_BIT)); }

#define DisableInterrupts RMT.int_ena.val &= ~(RMT_INT_TX_END_BIT | RMT_INT_THR_EVNT_BIT)
#define EnableInterrupts  RMT.int_ena.val |=  (RMT_INT_TX_END_BIT | RMT_INT_THR_EVNT_BIT)

#define RMT_ClockRate       80000000.0
#define RMT_Clock_Divisor   2.0
#define RMT_TickLengthNS    float ( (1/ (RMT_ClockRate/RMT_Clock_Divisor)) * 1000000000.0)

    void SetIntensity2Rmt (rmt_item32_t NewValue, RmtDataBitIdType_t ID) { Intensity2Rmt[ID] = NewValue; }

    bool NoFrameInProgress () { return (0 == (RMT.int_ena.val & (RMT_INT_TX_END_BIT | RMT_INT_THR_EVNT_BIT))); }

    void IRAM_ATTR ISR_Handler ();
   
    void SetIntensityDataWidth(uint32_t DataWidth);

#define USE_RMT_DEBUG_COUNTERS
#ifdef USE_RMT_DEBUG_COUNTERS
   // debug counters
   uint32_t DataCallbackCounter = 0;
   uint32_t DataTaskcounter = 0;
   uint32_t DataISRcounter = 0;
   uint32_t FrameThresholdCounter = 0;
   uint32_t FrameEndISRcounter = 0;
   uint32_t FrameStartCounter = 0;
   uint32_t RxIsr = 0;
   uint32_t ErrorIsr = 0;
   uint32_t IsrIsNotForUs = 0;
   uint32_t IntensityBytesSent = 0;
   uint32_t IntensityBitsSent = 0;
   uint32_t IntensityBytesSentLastFrame = 0;
   uint32_t IntensityBitsSentLastFrame = 0;
   uint32_t IncompleteFrame = 0;
   uint32_t IncompleteFrameLastFrame = 0;
#endif // def USE_RMT_DEBUG_COUNTERS
};
#endif // def #ifdef SUPPORT_RMT_OUTPUT
