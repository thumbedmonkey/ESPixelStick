/*
* ESPixelStick.ino
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2016 Shelby Merrick
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

#include <Arduino.h>

// Core
#include "src/ESPixelStick.h"
#include "src/EFUpdate.h"
#include <Int64String.h>

// Input modules
#include "src/input/InputMgr.hpp"

// Output modules
#include "src/output/OutputMgr.hpp"

// Network interface
#include "src/network/NetworkMgr.hpp"

// WEB interface
#include "src/WebMgr.hpp"

// File System Interface
#include "src/FileMgr.hpp"

// Services
#include "src/service/FPPDiscovery.h"

#ifdef ARDUINO_ARCH_ESP8266
#include <Hash.h>
extern "C"
{
#   include <user_interface.h>
} // extern "C"

#elif defined ARDUINO_ARCH_ESP32
    // ESP32 user_interface is now built in
#   include <Update.h>
#   include <esp_task_wdt.h>
#else
#	error "Unsupported CPU type."
#endif

// Debugging support
#if defined(DEBUG)
extern "C" void system_set_os_print(uint8 onoff);
extern "C" void ets_install_putc1(void* routine);

static void _u0_putc(char c){
  while(((U0S >> USTXC) & 0x7F) == 0x7F);
  U0F = c;
}
#endif

/////////////////////////////////////////////////////////
//
//  Globals
//
/////////////////////////////////////////////////////////

#ifdef ESPS_VERSION
const String VERSION = STRING(ESPS_VERSION);
#else
const String VERSION = "4.x-dev";
#endif

const String BUILD_DATE = String(__DATE__) + " - " + String(__TIME__);
const uint8_t CurrentConfigVersion = 1;

config_t config;                    // Current configuration
bool     reboot = false;            // Reboot flag
uint32_t lastUpdate;                // Update timeout tracker
bool     ResetWiFi = false;
bool     IsBooting = true;  // Configuration initialization flag
bool     ConfigLoadNeeded = false;
bool     ConfigSaveNeeded = false;

/////////////////////////////////////////////////////////
//
//  Forward Declarations
//
/////////////////////////////////////////////////////////

void loadConfig();
void GetConfig (JsonObject & json);
void GetDriverName (String & Name) { Name = "ESP"; }

/// Radio configuration
/** ESP8266 radio configuration routines that are executed at startup. */
#ifdef ARDUINO_ARCH_ESP8266
RF_PRE_INIT() {
    system_phy_set_powerup_option(3);   // Do full RF calibration on power-up
}
#endif

/// Arduino Setup
/** Arduino based setup code that is executed at startup. */
void setup()
{
    config.BlankDelay = 5;

    // Setup serial log port
    LOG_PORT.begin(115200);
    delay(10);

    // DEBUG_START;
    // DEBUG_HW_START;
#if defined(DEBUG)
    ets_install_putc1((void *) &_u0_putc);
    system_set_os_print(1);
#endif

    // Dump version and build information
    LOG_PORT.println ();
    logcon (String(CN_ESPixelStick) + " v" + VERSION + " (" + BUILD_DATE + ")");
#ifdef ARDUINO_ARCH_ESP8266
    logcon (ESP.getFullVersion ());
#else
    logcon (ESP.getSdkVersion ());
#endif

    // DEBUG_V ("");
    FileMgr.Begin ();

    // Load configuration from the File System and set Hostname
    loadConfig();
    // DEBUG_V ("");

    // Set up the output manager to start sending data to the serial ports
    OutputMgr.Begin ();
    // DEBUG_V ("");

    // connect the input processing to the output processing.
    InputMgr.Begin (OutputMgr.GetBufferAddress (), OutputMgr.GetBufferUsedSize ());
    // DEBUG_V ("");

    NetworkMgr.Begin ();
    // DEBUG_V ("");

    // Configure and start the web server
    WebMgr.Begin(&config);

    FPPDiscovery.begin ();

#ifdef ARDUINO_ARCH_ESP8266
    // * ((volatile uint32_t*)0x60000900) &= ~(1); // Hardware WDT OFF
    ESP.wdtEnable (2000); // 2 seconds
#else
    esp_task_wdt_init (5, true);
#endif
    // DEBUG_END;

    // Done with initialization
    IsBooting = false;

} // setup

/////////////////////////////////////////////////////////
//
//  JSON / Configuration Section
//
/////////////////////////////////////////////////////////

/// Configuration Validations
/** Validates the config_t (core) configuration structure and forces defaults for invalid entries */
bool validateConfig()
{
    // DEBUG_START;

    bool configValid = true;

    // Device defaults
    if (!config.id.length ())
    {
        config.id = "ESPixelStick";
        configValid = false;
        // DEBUG_V ();
    }

    return configValid;

    // DEBUG_END;
} // validateConfig

/// Deserialize device configuration JSON to config structure - returns true if config change detected
bool dsDevice(JsonObject & json)
{
    // DEBUG_START;
    // extern void PrettyPrint (JsonObject & jsonStuff, String Name);
    // PrettyPrint (json, "dsDevice");

    bool ConfigChanged = false;
    if (json.containsKey(CN_device))
    {
        JsonObject JsonDeviceConfig = json[CN_device];

//TODO: Add configuration upgrade handling - cfgver moved to root level
        ConfigChanged |= setFromJSON (config.id,         JsonDeviceConfig, CN_id);
        ConfigChanged |= setFromJSON (config.BlankDelay, JsonDeviceConfig, CN_blanktime);
    }
    else
    {
        logcon (String (F ("No device settings found.")));
    }

    // DEBUG_V (String("ConfigChanged: ") + String(ConfigChanged));
    // DEBUG_END;

    return ConfigChanged;
} // dsDevice

// Save the config and schedule a load operation
void SetConfig (const char * DataString)
{
    // DEBUG_START;

//TODO: This is being called from c_WebMgr::processCmdSet() with no validation
//      of the data. Chance for 3rd party software to muck up the configuraton
//      if they send bad json data.

    FileMgr.SaveConfigFile (ConfigFileName, DataString);
    ConfigLoadNeeded = true;

    // DEBUG_END;

} // SetConfig

bool deserializeCore (JsonObject & json)
{
    // DEBUG_START;

    bool DataHasBeenAccepted = false;

    // extern void PrettyPrint (JsonObject & jsonStuff, String Name);
    // PrettyPrint (json, "Main Config");

    do // once
    {
        if (json.containsKey(CN_cfgver))
        {
            uint8_t TempVersion = uint8_t(-1);
            setFromJSON (TempVersion, json, CN_cfgver);
            if (TempVersion != CurrentConfigVersion)
            {
                //TODO: Add configuration update handler
                logcon (String (F ("Incorrect Config Version ID")));
            }
        }
        else
        {
            logcon (String (F ("Missing Config Version ID")));
        }

        // is this an initial config from the flash tool?
        if (json.containsKey (CN_init))
        {
            // trigger a save operation
            ConfigSaveNeeded = true;
        }

        dsDevice  (json);
        FileMgr.SetConfig (json);
        ConfigSaveNeeded |= NetworkMgr.SetConfig (json);
        DataHasBeenAccepted = true;

    } while (false);

    // DEBUG_END;

    return DataHasBeenAccepted;
}

void deserializeCoreHandler (DynamicJsonDocument & jsonDoc)
{
    // DEBUG_START;

    JsonObject json = jsonDoc.as<JsonObject> ();
    deserializeCore (json);

    // DEBUG_END;
}

// Save configuration JSON file
void SaveConfig()
{
    // DEBUG_START;

    ConfigSaveNeeded = false;

    // Save Config
    String DataToSave = serializeCore (false);
    // DEBUG_V ("ConfigFileName: " + ConfigFileName);
    // DEBUG_V ("DataToSave: " + DataToSave);
    FileMgr.SaveConfigFile(ConfigFileName, DataToSave);

    // DEBUG_END;
} // SaveConfig

/// Load configuration file
/** Loads and validates the JSON configuration file from the file system.
 *  If no configuration file is found, a new one will be created.
 */
void loadConfig()
{
    // DEBUG_START;

    ConfigLoadNeeded = false;

    String temp;
    // DEBUG_V ("");
    FileMgr.LoadConfigFile (ConfigFileName, &deserializeCoreHandler);

    ConfigSaveNeeded |= !validateConfig ();

    // DEBUG_START;
} // loadConfig

void DeleteConfig ()
{
    // DEBUG_START;
    FileMgr.DeleteConfigFile (ConfigFileName);

    // DEBUG_END;

} // DeleteConfig

void GetConfig (JsonObject & json)
{
    // DEBUG_START;

    // Config Version
    json[CN_cfgver] = CurrentConfigVersion;

    // Device
    JsonObject device       = json.createNestedObject(CN_device);
    device[CN_id]           = config.id;
    device[CN_blanktime]    = config.BlankDelay;

    FileMgr.GetConfig (device);

    NetworkMgr.GetConfig (json);

    // DEBUG_END;
} // GetConfig

// Serialize the current config into a JSON string
String serializeCore(bool pretty)
{
    // DEBUG_START;

    // Create buffer and root object
    DynamicJsonDocument jsonConfigDoc(2048);
    JsonObject JsonConfig = jsonConfigDoc.createNestedObject();

    String jsonConfigString;

    GetConfig (JsonConfig);

    if (pretty)
    {
        serializeJsonPretty (JsonConfig, jsonConfigString);
    }
    else
    {
        serializeJson (JsonConfig, jsonConfigString);
    }

    // DEBUG_V (String ("jsonConfigString: ") + jsonConfigString);

    // DEBUG_END;

    return jsonConfigString;
} // serializeCore

/////////////////////////////////////////////////////////
//
//  Main Loop
//
/////////////////////////////////////////////////////////
/// Main Loop
/** Arduino based main loop */
void loop()
{
    // DEBUG_START;

    FeedWDT ();

    // Keep the Network Open
    NetworkMgr.Poll ();

    // Process input data
    InputMgr.Process ();

    // Render output
    OutputMgr.Render();

    WebMgr.Process ();

    // need to keep the rx pipeline empty
    size_t BytesToDiscard = min (100, LOG_PORT.available ());
    while (0 < BytesToDiscard)
    {
        FeedWDT ();

        // DEBUG_V (String("BytesToDiscard: ") + String(BytesToDiscard));
        BytesToDiscard--;
        LOG_PORT.read();
    } // end discard loop

    // Reboot handler
    if (reboot)
    {
        logcon (String(CN_stars) + CN_minussigns + F ("Internal Reboot Requested. Rebooting Now"));
        delay (REBOOT_DELAY);
        ESP.restart ();
    }

    if (ConfigLoadNeeded)
    {
        FeedWDT ();
        loadConfig ();
    }

    if (ConfigSaveNeeded)
    {
        FeedWDT ();
        SaveConfig ();
    }

} // loop

void _logcon (String & DriverName, String Message)
{
    char Spaces[] = { "       " };
    if (DriverName.length() < (sizeof(Spaces)-1))
    {
        Spaces[(sizeof (Spaces) - 1) - DriverName.length ()] = '\0';
    }
    else
    {
        Spaces[0] = '\0';
    }

    LOG_PORT.println ("[" + String (Spaces) + DriverName + "] " + Message);
    LOG_PORT.flush ();
} // logcon

void FeedWDT ()
{
#ifdef ARDUINO_ARCH_ESP32
    esp_task_wdt_reset ();
#else
    ESP.wdtFeed ();
#endif // def ARDUINO_ARCH_ESP32
}
