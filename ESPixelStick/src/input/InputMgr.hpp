#pragma once
/*
* InputMgr.hpp - Input Management class
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
*   This is a factory class used to manage the input channels. It creates and deletes
*   the input channel functionality as needed to support any new configurations
*   that get sent from from the WebPage.
*
*/

#include "../ESPixelStick.h"
#include "../FileMgr.hpp"
#include "../output/OutputMgr.hpp"
#include "externalInput.h"

class c_InputCommon; ///< forward declaration to the pure virtual Input class that will be defined later.

class c_InputMgr
{
public:
    // handles to determine which input channel we are dealing with
    // Channel 1 = primary / show input; Channel 2 = service input
    enum e_InputChannelIds
    {
        InputPrimaryChannelId = 0,
        InputSecondaryChannelId = 1,
        InputChannelId_End,
        InputChannelId_Start = InputPrimaryChannelId,
        InputChannelId_ALL = InputChannelId_End,
        EffectsChannel = InputSecondaryChannelId
    };

    c_InputMgr ();
    virtual ~c_InputMgr ();

    void Begin                (uint8_t * BufferStart, uint16_t BufferSize);
    void LoadConfig           ();
    void GetConfig            (byte * Response, size_t maxlen);
    void GetStatus            (JsonObject & jsonStatus);
    void SetConfig            (const char * NewConfig);
    void Process              ();
    void SetBufferInfo        (uint8_t* BufferStart, uint16_t BufferSize);
    void SetOperationalState  (bool Active);
    void NetworkStateChanged  (bool IsConnected);
    void DeleteConfig         () { FileMgr.DeleteConfigFile (ConfigFileName); }
    bool GetNetworkState      () { return IsConnected; }
    void GetDriverName        (String & Name) { Name = "InputMgr"; }
    void RestartBlankTimer    (c_InputMgr::e_InputChannelIds Selector) { BlankEndTime[int(Selector)] = (millis () / 1000) + config.BlankDelay; }
    bool BlankTimerHasExpired (c_InputMgr::e_InputChannelIds Selector) { return !(BlankEndTime[int(Selector)] > (millis () / 1000)); }

    enum e_InputType
    {
        InputType_E1_31 = 0,
        InputType_Effects,
        InputType_MQTT,
        InputType_Alexa,
        InputType_DDP,
        InputType_FPP,
        InputType_Artnet,
        InputType_Disabled,
        InputType_End,
        InputType_Start   = InputType_E1_31,
        InputType_Default = InputType_Disabled,
    };

private:

    void InstantiateNewInputChannel (e_InputChannelIds InputChannelId, e_InputType NewChannelType, bool StartDriver = true);
    void CreateNewConfig ();

    c_InputCommon * pInputChannelDrivers[InputChannelId_End]; ///< pointer(s) to the current active Input driver
    uint8_t       * InputDataBuffer     = nullptr;
    uint16_t        InputDataBufferSize = 0;
    bool            HasBeenInitialized  = false;
    c_ExternalInput ExternalInput;
    bool            EffectEngineIsConfiguredToRun[InputChannelId_End];
    bool            IsConnected         = false;
    bool            configInProgress    = false;
    bool            configLoadNeeded    = false;

    // configuration parameter names for the channel manager within the config file
#   define IM_EffectsControlButtonName F ("ecb")

    bool ProcessJsonConfig           (JsonObject & jsonConfig);
    void CreateJsonConfig            (JsonObject & jsonConfig);
    bool ProcessJsonChannelConfig    (JsonObject & jsonConfig, uint32_t ChannelIndex);
    bool InputTypeIsAllowedOnChannel (e_InputType type, e_InputChannelIds ChannelId);
    void ProcessEffectsButtonActions (void);

    String ConfigFileName;
    bool   rebootNeeded = false;

    time_t BlankEndTime[InputChannelId_End];

#define IM_JSON_SIZE (5 * 1024)

}; // c_InputMgr

extern c_InputMgr InputMgr;
