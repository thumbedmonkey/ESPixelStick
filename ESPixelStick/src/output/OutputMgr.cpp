/*
* OutputMgr.cpp - Output Management class
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
*   This is a factory class used to manage the output port. It creates and deletes
*   the output channel functionality as needed to support any new configurations
*   that get sent from from the WebPage.
*
*/

#include "../ESPixelStick.h"
#include "../FileMgr.hpp"

//-----------------------------------------------------------------------------
// bring in driver definitions
#include "OutputDisabled.hpp"
#include "OutputAPA102Spi.hpp"
#include "OutputGECE.hpp"
#include "OutputRelay.hpp"
#include "OutputSerial.hpp"
#include "OutputServoPCA9685.hpp"
#include "OutputTM1814Rmt.hpp"
#include "OutputTM1814Uart.hpp"
#include "OutputUCS1903Rmt.hpp"
#include "OutputUCS1903Uart.hpp"
#include "OutputWS2801Spi.hpp"
#include "OutputWS2811Rmt.hpp"
#include "OutputWS2811Uart.hpp"
// needs to be last
#include "OutputMgr.hpp"

#include "../input/InputMgr.hpp"

//-----------------------------------------------------------------------------
// Local Data definitions
//-----------------------------------------------------------------------------
typedef struct
{
    c_OutputMgr::e_OutputType id;
    String name;
}OutputTypeXlateMap_t;

static const OutputTypeXlateMap_t OutputTypeXlateMap[c_OutputMgr::e_OutputType::OutputType_End] =
{
    {c_OutputMgr::e_OutputType::OutputType_WS2811,        "WS2811"        },
    {c_OutputMgr::e_OutputType::OutputType_GECE,          "GECE"          },
    {c_OutputMgr::e_OutputType::OutputType_DMX,           "DMX"           },
    {c_OutputMgr::e_OutputType::OutputType_Renard,        "Renard"        },
    {c_OutputMgr::e_OutputType::OutputType_Serial,        "Serial"        },
#ifdef SUPPORT_RELAY_OUTPUT
    {c_OutputMgr::e_OutputType::OutputType_Relay,         "Relay"         },
    {c_OutputMgr::e_OutputType::OutputType_Servo_PCA9685, "Servo_PCA9685" },
#endif // def SUPPORT_RELAY_OUTPUT
    {c_OutputMgr::e_OutputType::OutputType_Disabled,      "Disabled"      },

#ifdef SUPPORT_OutputType_UCS1903
    {c_OutputMgr::e_OutputType::OutputType_UCS1903,       "UCS1903"       },
#endif // def SUPPORT_OutputType_TM1814

#ifdef SUPPORT_OutputType_TM1814
    {c_OutputMgr::e_OutputType::OutputType_TM1814,        "TM1814"        },
#endif // def SUPPORT_OutputType_TM1814

#ifdef SUPPORT_OutputType_WS2801
    {c_OutputMgr::e_OutputType::OutputType_WS2801,        "WS2801"        },
#endif // def SUPPORT_OutputType_WS2801

#ifdef SUPPORT_OutputType_APA102
    {c_OutputMgr::e_OutputType::OutputType_APA102,        "APA102"        }
#endif // def SUPPORT_OutputType_APA102
};

//-----------------------------------------------------------------------------
typedef struct
{
    gpio_num_t dataPin;
    uart_port_t UartId;
} OutputChannelIdToGpioAndPortEntry_t;

//-----------------------------------------------------------------------------
static const OutputChannelIdToGpioAndPortEntry_t OutputChannelIdToGpioAndPort[] =
{
#ifdef DEFAULT_UART_1_GPIO
    {DEFAULT_UART_1_GPIO, UART_NUM_1},
#endif // def DEFAULT_UART_1_GPIO

#ifdef DEFAULT_UART_2_GPIO
    {DEFAULT_UART_2_GPIO, UART_NUM_2},
#endif // def DEFAULT_UART_2_GPIO

    // RMT ports
#ifdef DEFAULT_RMT_0_GPIO
    {DEFAULT_RMT_0_GPIO,  uart_port_t (0)},
#endif // def DEFAULT_RMT_0_GPIO

#ifdef DEFAULT_RMT_1_GPIO
    {DEFAULT_RMT_1_GPIO,  uart_port_t (1)},
#endif // def DEFAULT_RMT_1_GPIO

#ifdef DEFAULT_RMT_2_GPIO
    {DEFAULT_RMT_2_GPIO,  uart_port_t (2)},
#endif // def DEFAULT_RMT_2_GPIO

#ifdef DEFAULT_RMT_3_GPIO
    {DEFAULT_RMT_3_GPIO,  uart_port_t (3)},
#endif // def DEFAULT_RMT_3_GPIO

#ifdef SUPPORT_SPI_OUTPUT
    {DEFAULT_SPI_DATA_GPIO, uart_port_t (-1)},
#endif

#ifdef SUPPORT_RELAY_OUTPUT
    {gpio_num_t::GPIO_NUM_10, uart_port_t (-1)},
#endif // def SUPPORT_RELAY_OUTPUT

};

//-----------------------------------------------------------------------------
// Methods
//-----------------------------------------------------------------------------
///< Start up the driver and put it into a safe mode
c_OutputMgr::c_OutputMgr ()
{
    ConfigFileName = String (F ("/")) + String (CN_output_config) + F (".json");

    // clear the input data buffer
    memset ((char*)&OutputBuffer[0], 0, sizeof (OutputBuffer));

    // this gets called pre-setup so there is nothing we can do here.
    int pOutputChannelDriversIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        pOutputChannelDrivers[pOutputChannelDriversIndex++] = nullptr;
    }

} // c_OutputMgr

//-----------------------------------------------------------------------------
///< deallocate any resources and put the output channels into a safe state
c_OutputMgr::~c_OutputMgr()
{
    // DEBUG_START;

    // delete pOutputInstances;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        // the drivers will put the hardware in a safe state
        delete CurrentOutput;
    }
    // DEBUG_END;

} // ~c_OutputMgr

//-----------------------------------------------------------------------------
///< Start the module
void c_OutputMgr::Begin ()
{
    // DEBUG_START;

    // prevent recalls
    if (true == HasBeenInitialized) { return; }
    HasBeenInitialized = true;

#ifdef LED_FLASH_GPIO
    pinMode (LED_FLASH_GPIO, OUTPUT);
    digitalWrite (LED_FLASH_GPIO, LED_FLASH_OFF);
#endif // def LED_FLASH_GPIO

    // make sure the pointers are set up properly
    int ChannelIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        InstantiateNewOutputChannel (e_OutputChannelIds(ChannelIndex++),
                                     e_OutputType::OutputType_Disabled);
        // DEBUG_V ("");
    }

    // load up the configuration from the saved file. This also starts the drivers
    LoadConfig ();

    // CreateNewConfig ();

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_OutputMgr::CreateJsonConfig (JsonObject& jsonConfig)
{
    // DEBUG_START;

    // extern void PrettyPrint (JsonObject&, String);
    // PrettyPrint (jsonConfig, String ("jsonConfig"));

    // add OM config parameters
    // DEBUG_V ("");
    
    // add the channels header
    JsonObject OutputMgrChannelsData;
    if (true == jsonConfig.containsKey (CN_channels))
    {
        // DEBUG_V ("");
        OutputMgrChannelsData = jsonConfig[CN_channels];
    }
    else
    {
        // add our section header
        // DEBUG_V ("");
        OutputMgrChannelsData = jsonConfig.createNestedObject (CN_channels);
    }

    // add the channel configurations
    // DEBUG_V ("For Each Output Channel");
    for (auto CurrentChannel : pOutputChannelDrivers)
    {
        // DEBUG_V (String("Create Section in Config file for the output channel: '") + CurrentChannel->GetOutputChannelId() + "'");
        // create a record for this channel
        JsonObject ChannelConfigData;
        String sChannelId = String (CurrentChannel->GetOutputChannelId ());
        if (true == OutputMgrChannelsData.containsKey (sChannelId))
        {
            // DEBUG_V ("");
            ChannelConfigData = OutputMgrChannelsData[sChannelId];
        }
        else
        {
            // add our section header
            // DEBUG_V ("");
            ChannelConfigData = OutputMgrChannelsData.createNestedObject (sChannelId);
        }

        // save the name as the selected channel type
        ChannelConfigData[CN_type] = int (CurrentChannel->GetOutputType ());

        String DriverTypeId = String (int (CurrentChannel->GetOutputType ()));
        JsonObject ChannelConfigByTypeData;
        if (true == ChannelConfigData.containsKey (String (DriverTypeId)))
        {
            ChannelConfigByTypeData = ChannelConfigData[DriverTypeId];
            // DEBUG_V ("");
        }
        else
        {
            // add our section header
            // DEBUG_V ("");
            ChannelConfigByTypeData = ChannelConfigData.createNestedObject (DriverTypeId);
        }

        // DEBUG_V ("");
        // PrettyPrint (ChannelConfigByTypeData, String ("jsonConfig"));

        // ask the channel to add its data to the record
        // DEBUG_V ("Add the output channel configuration for type: " + DriverTypeId);

        // Populate the driver name
        String DriverName = "";
        CurrentChannel->GetDriverName (DriverName);
        // DEBUG_V (String ("DriverName: ") + DriverName);

        ChannelConfigByTypeData[CN_type] = DriverName;

        // DEBUG_V ("");
        // PrettyPrint (ChannelConfigByTypeData, String ("jsonConfig"));
        // DEBUG_V ("");

        CurrentChannel->GetConfig (ChannelConfigByTypeData);

        // DEBUG_V ("");
        // PrettyPrint (ChannelConfigByTypeData, String ("jsonConfig"));
        // DEBUG_V ("");

    } // for each output channel

    // DEBUG_V ("");
    // PrettyPrint (jsonConfig, String ("jsonConfig"));

    // smile. Your done
    // DEBUG_END;
} // CreateJsonConfig

//-----------------------------------------------------------------------------
/*
    The running config is made from a composite of running and not instantiated
    objects. To create a complete config we need to start each output type on
    each output channel and collect the configuration at each stage.
*/
void c_OutputMgr::CreateNewConfig ()
{
    // DEBUG_START;
    if (!IsBooting)
    {
        logcon (String (F ("--- WARNING: Creating a new Output Manager configuration Data set ---")));
    }

    BuildingNewConfig = true;

    // create a place to save the config
    DynamicJsonDocument JsonConfigDoc (OM_MAX_CONFIG_SIZE);
    // DEBUG_V ("");

    JsonObject JsonConfig = JsonConfigDoc.createNestedObject (CN_output_config);
    // DEBUG_V ("");

    JsonConfig[CN_cfgver] = CurrentConfigVersion;
    JsonConfig[F ("MaxChannels")] = sizeof(OutputBuffer);

    // DEBUG_V ("for each output type");
    for (int outputTypeId = int (OutputType_Start);
         outputTypeId < int (OutputType_End);
         ++outputTypeId)
    {
        // DEBUG_V ("for each interface");
        int ChannelIndex = 0;
        for (auto CurrentOutput : pOutputChannelDrivers)
        {
            // DEBUG_V (String ("ChannelIndex: ") + String (ChannelIndex));
            // DEBUG_V (String ("instantiate output type: ") + String (outputTypeId));
            InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex++), e_OutputType (outputTypeId), false);
            // DEBUG_V ("");

        }// end for each interface

        // DEBUG_V ("collect the config data for this output type");
        CreateJsonConfig (JsonConfig);

        // DEBUG_V ("");

    } // end for each output type

    // DEBUG_V ("leave the outputs disabled");
    int ChannelIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex++), e_OutputType::OutputType_Disabled);
    }// end for each interface
    // DEBUG_V ("Outputs Are disabled");
    CreateJsonConfig (JsonConfig);

    // DEBUG_V ("");
    String ConfigData;
    serializeJson (JsonConfigDoc, ConfigData);
    // DEBUG_V (String("ConfigData: ") + ConfigData);
    SetConfig (ConfigData.c_str());
    // DEBUG_V (String ("ConfigData: ") + ConfigData);

    // logcon (String (F ("--- WARNING: Creating a new Output Manager configuration Data set - Done ---")));

    BuildingNewConfig = true;

    // DEBUG_END;

} // CreateNewConfig

//-----------------------------------------------------------------------------
void c_OutputMgr::GetConfig (String & Response)
{
    // DEBUG_START;

    FileMgr.ReadConfigFile (ConfigFileName, Response);

    // DEBUG_END;

} // GetConfig

//-----------------------------------------------------------------------------
void c_OutputMgr::GetConfig (byte * Response, size_t maxlen )
{
    // DEBUG_START;

    FileMgr.ReadConfigFile (ConfigFileName, Response, maxlen);

    // DEBUG_END;

} // GetConfig

//-----------------------------------------------------------------------------
void c_OutputMgr::GetStatus (JsonObject & jsonStatus)
{
    // DEBUG_START;

    JsonArray OutputStatus = jsonStatus.createNestedArray (CN_output);
    uint8_t channelIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        // DEBUG_V("");
        JsonObject channelStatus = OutputStatus.createNestedObject ();
        CurrentOutput->GetStatus (channelStatus);
        channelIndex++;
        // DEBUG_V("");
    }

    // DEBUG_END;
} // GetStatus

//-----------------------------------------------------------------------------
/* Create an instance of the desired output type in the desired channel
*
* WARNING:  This function assumes there is a driver running in the identified
*           out channel. These must be set up and started when the manager is
*           started.
*
    needs
        channel ID
        channel type
    returns
        nothing
*/
void c_OutputMgr::InstantiateNewOutputChannel (e_OutputChannelIds ChannelIndex, e_OutputType NewOutputChannelType, bool StartDriver)
{
    // DEBUG_START;

    do // once
    {
        // is there an existing driver?
        if (nullptr != pOutputChannelDrivers[ChannelIndex])
        {
            // int temp = pOutputChannelDrivers[ChannelIndex]->GetOutputType ();
            // DEBUG_V (String("pOutputChannelDrivers[ChannelIndex]->GetOutputType () '") + temp + String("'"));
            // DEBUG_V (String("NewOutputChannelType '") + int(NewOutputChannelType) + "'");

            // DEBUG_V ("does the driver need to change?");
            if (pOutputChannelDrivers[ChannelIndex]->GetOutputType () == NewOutputChannelType)
            {
                // DEBUG_V ("nothing to change");
                break;
            }

            String DriverName;
            pOutputChannelDrivers[ChannelIndex]->GetDriverName (DriverName);
            if (!IsBooting)
            {
                 logcon (String (F (" Shutting Down '")) + DriverName + String (F ("' on Output: ")) + String (ChannelIndex));
            }
            delete pOutputChannelDrivers[ChannelIndex];
            pOutputChannelDrivers[ChannelIndex] = nullptr;
            // DEBUG_V ("");
        } // end there is an existing driver

        // DEBUG_V ("");

        // get the new data and UART info
        gpio_num_t dataPin = OutputChannelIdToGpioAndPort[ChannelIndex].dataPin;
        uart_port_t UartId = OutputChannelIdToGpioAndPort[ChannelIndex].UartId;

        switch (NewOutputChannelType)
        {
            case e_OutputType::OutputType_Disabled:
            {
                // logcon (CN_stars + String (F (" Disabled output type for channel '")) + ChannelIndex + "'. **************");
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }

            case e_OutputType::OutputType_DMX:
            {
                if ((ChannelIndex >= OutputChannelId_UART_FIRST) && (ChannelIndex <= OutputChannelId_UART_LAST))
                {
                    // logcon (CN_stars + String (F (" Starting DMX for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputSerial (ChannelIndex, dataPin, UartId, OutputType_DMX);
                    // DEBUG_V ("");
                    break;
                }

                // DEBUG_V ("");
                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start DMX for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }

            case e_OutputType::OutputType_GECE:
            {
                if ((ChannelIndex >= OutputChannelId_UART_FIRST) && (ChannelIndex <= OutputChannelId_UART_LAST))
                {
                    // logcon (CN_stars + String (F (" Starting GECE for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputGECE (ChannelIndex, dataPin, UartId, OutputType_GECE);
                    // DEBUG_V ("");
                    break;
                }
                // DEBUG_V ("");

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start GECE for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }

            case e_OutputType::OutputType_Serial:
            {
                if (OM_IS_UART)
                {
                    // logcon (CN_stars + String (F (" Starting Generic Serial for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputSerial (ChannelIndex, dataPin, UartId, OutputType_Serial);
                    // DEBUG_V ("");
                    break;
                }
                // DEBUG_V ("");

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start Generic Serial for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }

            case e_OutputType::OutputType_Renard:
            {
                if (OM_IS_UART)
                {
                    // logcon (CN_stars + String (F (" Starting Renard for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputSerial (ChannelIndex, dataPin, UartId, OutputType_Renard);
                    // DEBUG_V ("");
                    break;
                }
                // DEBUG_V ("");

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start Renard for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }

#ifdef SUPPORT_RELAY_OUTPUT
            case e_OutputType::OutputType_Relay:
            {
                if (ChannelIndex == OutputChannelId_Relay)
                {
                    // logcon (CN_stars + String (F (" Starting RELAY for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputRelay (ChannelIndex, dataPin, UartId, OutputType_Relay);
                    // DEBUG_V ("");
                    break;
                }
                // DEBUG_V ("");

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start RELAY for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }

            case e_OutputType::OutputType_Servo_PCA9685:
            {
                if (ChannelIndex == OutputChannelId_Relay)
                {
                    // logcon (CN_stars + String (F (" Starting Servo PCA9685 for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputServoPCA9685 (ChannelIndex, dataPin, UartId, OutputType_Servo_PCA9685);
                    // DEBUG_V ("");
                    break;
                }
                // DEBUG_V ("");

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start Servo PCA9685 for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }
#endif // SUPPORT_RELAY_OUTPUT

            case e_OutputType::OutputType_WS2811:
            {
#ifdef SUPPORT_RMT_OUTPUT
                if (OM_IS_RMT)
                {
                    // logcon (CN_stars + String (F (" Starting WS2811 RMT for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputWS2811Rmt (ChannelIndex, dataPin, UartId, OutputType_WS2811);
                    // DEBUG_V ("");
                    break;
                }
#endif // def SUPPORT_RMT_OUTPUT

                // DEBUG_V ("");
                if (OM_IS_UART)
                {
                    // logcon (CN_stars + String (F (" Starting WS2811 UART for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputWS2811Uart (ChannelIndex, dataPin, UartId, OutputType_WS2811);
                    // DEBUG_V ("");
                    break;
                }

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start WS2811 for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }

#ifdef SUPPORT_OutputType_UCS1903
            case e_OutputType::OutputType_UCS1903:
            {
#ifdef SUPPORT_RMT
                if (OM_IS_RMT)
                {
                    // logcon (CN_stars + String (F (" Starting TM1814 RMT for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputUCS1903Rmt (ChannelIndex, dataPin, UartId, OutputType_UCS1903);
                    // DEBUG_V ("");
                    break;
                }
#endif // def SUPPORT_RMT
                // DEBUG_V ("");
                if (OM_IS_UART)
                {
                    // logcon (CN_stars + String (F (" Starting TM1814 UART for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputUCS1903Uart (ChannelIndex, dataPin, UartId, OutputType_UCS1903);
                    // DEBUG_V ("");
                    break;
                }

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start UCS1903 for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }
#endif // def SUPPORT_OutputType_UCS1903

#ifdef SUPPORT_OutputType_TM1814
            case e_OutputType::OutputType_TM1814:
            {
#ifdef SUPPORT_RMT
                if (OM_IS_RMT)
                {
                    // logcon (CN_stars + String (F (" Starting TM1814 RMT for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputTM1814Rmt (ChannelIndex, dataPin, UartId, OutputType_TM1814);
                    // DEBUG_V ("");
                    break;
                }
#endif // def SUPPORT_RMT
                // DEBUG_V ("");
                if (OM_IS_UART)
                {
                    // logcon (CN_stars + String (F (" Starting TM1814 UART for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputTM1814Uart (ChannelIndex, dataPin, UartId, OutputType_TM1814);
                    // DEBUG_V ("");
                    break;
                }

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start TM1814 for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }
#endif // def SUPPORT_OutputType_TM1814

#ifdef SUPPORT_OutputType_WS2801
            case e_OutputType::OutputType_WS2801:
            {
                if (ChannelIndex == OutputChannelId_SPI_1)
                {
                    // logcon (CN_stars + String (F (" Starting WS2811 RMT for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputWS2801Spi (ChannelIndex, dataPin, UartId, OutputType_WS2801);
                    // DEBUG_V ("");
                    break;
                }

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start WS2801 for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }
#endif // def SUPPORT_OutputType_WS2801

#ifdef SUPPORT_OutputType_APA102
            case e_OutputType::OutputType_APA102:
            {
                if (ChannelIndex == OutputChannelId_SPI_1)
                {
                    // logcon (CN_stars + String (F (" Starting WS2811 RMT for channel '")) + ChannelIndex + "'. " + CN_stars);
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputAPA102Spi (ChannelIndex, dataPin, UartId, OutputType_APA102);
                    // DEBUG_V ("");
                    break;
                }

                if (!BuildingNewConfig) { logcon (CN_stars + String (F (" Cannot Start WS2801 for channel '")) + ChannelIndex + "'. " + CN_stars); }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");

                break;
            }
#endif // def SUPPORT_OutputType_APA102

            default:
            {
                if (!IsBooting)
                {
                    logcon (CN_stars + String (F (" Unknown output type: '")) + String (NewOutputChannelType) + String(F(" for channel '")) + ChannelIndex + String(F("'. Using disabled. ")) + CN_stars );
                }
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }
        } // end switch (NewChannelType)

        // DEBUG_V ("");
        String sDriverName;
        pOutputChannelDrivers[ChannelIndex]->GetDriverName (sDriverName);
        if (!IsBooting)
        {
            logcon ("'" + sDriverName + F ("' Initialization for Output: ") + String (ChannelIndex));
        }
        if (StartDriver)
        {
            pOutputChannelDrivers[ChannelIndex]->Begin ();
        }

    } while (false);

    // String temp;
    // pOutputChannelDrivers[ChannelIndex]->GetDriverName (temp);
    // DEBUG_V (String ("Driver Name: ") + temp);

    // DEBUG_END;

} // InstantiateNewOutputChannel

//-----------------------------------------------------------------------------
/* Load and process the current configuration
*
*   needs
*       Nothing
*   returns
*       Nothing
*/
void c_OutputMgr::LoadConfig ()
{
    // DEBUG_START;

    // try to load and process the config file
    if (!FileMgr.LoadConfigFile (ConfigFileName, [this](DynamicJsonDocument & JsonConfigDoc)
        {
            // DEBUG_V ("");
            JsonObject JsonConfig = JsonConfigDoc.as<JsonObject> ();
            // DEBUG_V ("");
            this->ProcessJsonConfig (JsonConfig);
            // DEBUG_V ("");
        }))
    {
        if (!IsBooting) {
            logcon (CN_stars + String (F (" Error loading Output Manager Config File ")) + CN_stars);
        }

        // create a config file with default values
        // DEBUG_V ("");
        CreateNewConfig ();
    }

    // DEBUG_END;

} // LoadConfig

//-----------------------------------------------------------------------------
/*
    check the contents of the config and send
    the proper portion of the config to the currently instantiated channels

    needs
        ref to data from config file
    returns
        true - config was properly processes
        false - config had an error.
*/
bool c_OutputMgr::ProcessJsonConfig (JsonObject& jsonConfig)
{
    // DEBUG_START;
    bool Response = false;

    // DEBUG_V ("");

    do // once
    {
        if (false == jsonConfig.containsKey (CN_output_config))
        {
            logcon (String (F ("No Output Interface Settings Found. Using Defaults")));
            break;
        }
        JsonObject OutputChannelMgrData = jsonConfig[CN_output_config];
        // DEBUG_V ("");

        uint8_t TempVersion = !CurrentConfigVersion;
        setFromJSON (TempVersion, OutputChannelMgrData, CN_cfgver);

        // DEBUG_V (String ("TempVersion: ") + String (TempVersion));
        // DEBUG_V (String ("CurrentConfigVersion: ") + String (CurrentConfigVersion));
        // extern void PrettyPrint (JsonObject & jsonStuff, String Name);
        // PrettyPrint (OutputChannelMgrData, "Output Config");
        if (TempVersion != CurrentConfigVersion)
        {
            logcon (String (F ("OutputMgr: Incorrect Version found. Using existing/default config.")));
            // break;
        }

        // do we have a channel configuration array?
        if (false == OutputChannelMgrData.containsKey (CN_channels))
        {
            // if not, flag an error and stop processing
            logcon (String (F ("No Output Channel Settings Found. Using Defaults")));
            break;
        }
        JsonObject OutputChannelArray = OutputChannelMgrData[CN_channels];
        // DEBUG_V ("");

        // for each output channel
        for (uint32_t ChannelIndex = uint32_t (OutputChannelId_Start);
            ChannelIndex < uint32_t (OutputChannelId_End);
            ChannelIndex++)
        {
            // get access to the channel config
            if (false == OutputChannelArray.containsKey (String (ChannelIndex).c_str ()))
            {
                // if not, flag an error and stop processing
                logcon (String (F ("No Output Settings Found for Channel '")) + ChannelIndex + String (F ("'. Using Defaults")));
                break;
            }
            JsonObject OutputChannelConfig = OutputChannelArray[String (ChannelIndex).c_str ()];
            // DEBUG_V ("");

            // set a default value for channel type
            uint32_t ChannelType = uint32_t (OutputType_End);
            setFromJSON (ChannelType, OutputChannelConfig, CN_type);
            // DEBUG_V ("");

            // is it a valid / supported channel type
            if (/*(ChannelType < uint32_t (OutputType_Start)) || */ (ChannelType >= uint32_t (OutputType_End)))
            {
                // if not, flag an error and move on to the next channel
                logcon (String (F ("Invalid Channel Type in config '")) + ChannelType + String (F ("'. Specified for channel '")) + ChannelIndex + "'. Disabling channel");
                InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType::OutputType_Disabled);
                continue;
            }
            // DEBUG_V ("");

            // do we have a configuration for the channel type?
            if (false == OutputChannelConfig.containsKey (String (ChannelType)))
            {
                // if not, flag an error and stop processing
                logcon (String (F ("No Output Settings Found for Channel '")) + ChannelIndex + String (F ("'. Using Defaults")));
                InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType::OutputType_Disabled);
                continue;
            }

            JsonObject OutputChannelDriverConfig = OutputChannelConfig[String (ChannelType)];
            // DEBUG_V ("");

            // make sure the proper output type is running
            InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType (ChannelType));
            // DEBUG_V ("");

            // send the config to the driver. At this level we have no idea what is in it
            pOutputChannelDrivers[ChannelIndex]->SetConfig (OutputChannelDriverConfig);

        } // end for each channel

        // all went well
        Response = true;

    } while (false);

    // did we get a valid config?
    if (false == Response)
    {
        // save the current config since it is the best we have.
        // DEBUG_V ("");
        CreateNewConfig ();
    }

    UpdateDisplayBufferReferences ();

    // DEBUG_END;
    return Response;

} // ProcessJsonConfig

//-----------------------------------------------------------------------------
/*
    This is a bit tricky. The running config is only a portion of the total
    configuration. We need to get the existing saved configuration and add the
    current configuration to it.

*   Save the current configuration to NVRAM
*
*   needs
*       Nothing
*   returns
*       Nothing
*/
void c_OutputMgr::SetConfig (const char * ConfigData)
{
    // DEBUG_START;

    // DEBUG_V (String ("ConfigData: ") + ConfigData);

    if (true == FileMgr.SaveConfigFile (ConfigFileName, ConfigData))
    {
        ConfigLoadNeeded = true;
        // FileMgr logs for us
        // logcon (CN_stars + String (F (" Saved Output Manager Config File. ")) + CN_stars);
    } // end we got a config and it was good
    else
    {
        logcon (CN_stars + String (F (" Error Saving Output Manager Config File ")) + CN_stars);
    }

    // DEBUG_END;

} // SaveConfig

//-----------------------------------------------------------------------------
///< Called from loop(), renders output data
void c_OutputMgr::Render()
{
    // DEBUG_START;
    // do we need to save the current config?
    if (true == ConfigLoadNeeded)
    {
        ConfigLoadNeeded = false;
        LoadConfig ();
    } // done need to save the current config

    if (false == IsOutputPaused)
    {
        // DEBUG_START;
        for (c_OutputCommon* pOutputChannel : pOutputChannelDrivers)
        {
            pOutputChannel->Render ();
        }
    }
    // DEBUG_END;
} // render

//-----------------------------------------------------------------------------
void c_OutputMgr::UpdateDisplayBufferReferences (void)
{
    // DEBUG_START;

    uint16_t OutputBufferOffset = 0;

    // DEBUG_V (String ("        BufferSize: ") + String (sizeof(OutputBuffer)));
    // DEBUG_V (String ("OutputBufferOffset: ") + String (OutputBufferOffset));

    for (c_OutputCommon* pOutputChannel : pOutputChannelDrivers)
    {
        pOutputChannel->SetOutputBufferAddress (&OutputBuffer[OutputBufferOffset]);
        uint16_t ChannelsNeeded     = pOutputChannel->GetNumChannelsNeeded ();
        uint16_t AvailableChannels  = sizeof(OutputBuffer) - OutputBufferOffset;
        uint16_t ChannelsToAllocate = min (ChannelsNeeded, AvailableChannels);

        // DEBUG_V (String ("    ChannelsNeeded: ") + String (ChannelsNeeded));
        // DEBUG_V (String (" AvailableChannels: ") + String (AvailableChannels));
        // DEBUG_V (String ("ChannelsToAllocate: ") + String (ChannelsToAllocate));

        pOutputChannel->SetOutputBufferSize (ChannelsToAllocate);

        if (AvailableChannels < ChannelsNeeded)
        {
            logcon (String (F ("--- OutputMgr: ERROR: Too many output channels have been Requested: ")) + String (ChannelsNeeded));
            // DEBUG_V (String ("    ChannelsNeeded: ") + String (ChannelsNeeded));
            // DEBUG_V (String (" AvailableChannels: ") + String (AvailableChannels));
            // DEBUG_V (String ("ChannelsToAllocate: ") + String (ChannelsToAllocate));
        }

        OutputBufferOffset += ChannelsToAllocate;
        // DEBUG_V (String ("pOutputChannel->GetBufferUsedSize: ") + String (pOutputChannel->GetBufferUsedSize ()));
        // DEBUG_V (String ("OutputBufferOffset: ") + String(OutputBufferOffset));
    }

    // DEBUG_V (String ("   TotalBufferSize: ") + String (OutputBufferOffset));
    UsedBufferSize = OutputBufferOffset;
    // DEBUG_V (String ("       OutputBuffer: 0x") + String (uint32_t (OutputBuffer), HEX));
    // DEBUG_V (String ("     UsedBufferSize: ") + String (uint32_t (UsedBufferSize)));
    InputMgr.SetBufferInfo (OutputBuffer, OutputBufferOffset);

    // DEBUG_END;

} // UpdateDisplayBufferReferences

//-----------------------------------------------------------------------------
void c_OutputMgr::PauseOutputs (void)
{
    // DEBUG_START;

    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        CurrentOutput->PauseOutput ();
    }

    // DEBUG_END;
} // PauseOutputs

// create a global instance of the output channel factory
c_OutputMgr OutputMgr;
