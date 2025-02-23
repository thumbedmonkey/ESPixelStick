/*
* WiFiDriver.cpp - Output Management class
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

#include "../ESPixelStick.h"

#ifdef ARDUINO_ARCH_ESP8266
#   include <eagle_soc.h>
#   include <ets_sys.h>
#else
#   include <esp_wifi.h>
#endif // def ARDUINO_ARCH_ESP8266

#include "WiFiDriver.hpp"
#include "NetworkMgr.hpp"
#include "../FileMgr.hpp"

//-----------------------------------------------------------------------------
// Create secrets.h with a #define for SECRETS_SSID and SECRETS_PASS
// or delete the #include and enter the strings directly below.
// #include "secrets.h"
#ifndef SECRETS_SSID
#   define SECRETS_SSID "DEFAULT_SSID_NOT_SET"
#   define SECRETS_PASS "DEFAULT_PASSPHRASE_NOT_SET"
#endif // ndef SECRETS_SSID

/* Fallback configuration if config.json is empty or fails */
const String default_ssid       = SECRETS_SSID;
const String default_passphrase = SECRETS_PASS;

/// Radio configuration
/** ESP8266 radio configuration routines that are executed at startup. */
/* Disabled for now, possible flash wear issue. Need to research further
RF_PRE_INIT() {
#ifdef ARDUINO_ARCH_ESP8266
    system_phy_set_powerup_option(3);   // Do full RF calibration on power-up
    system_phy_set_max_tpw(82);         // Set max TX power
#else
    esp_phy_erase_cal_data_in_nvs(); // Do full RF calibration on power-up
    esp_wifi_set_max_tx_power (78);  // Set max TX power
#endif
}
*/

/*****************************************************************************/
/* FSM                                                                       */
/*****************************************************************************/
fsm_WiFi_state_Boot                    fsm_WiFi_state_Boot_imp;
fsm_WiFi_state_ConnectingUsingConfig   fsm_WiFi_state_ConnectingUsingConfig_imp;
fsm_WiFi_state_ConnectingUsingDefaults fsm_WiFi_state_ConnectingUsingDefaults_imp;
fsm_WiFi_state_ConnectedToAP           fsm_WiFi_state_ConnectedToAP_imp;
fsm_WiFi_state_ConnectingAsAP          fsm_WiFi_state_ConnectingAsAP_imp;
fsm_WiFi_state_ConnectedToSta          fsm_WiFi_state_ConnectedToSta_imp;
fsm_WiFi_state_ConnectionFailed        fsm_WiFi_state_ConnectionFailed_imp;
fsm_WiFi_state_Disabled                fsm_WiFi_state_Disabled_imp;

//-----------------------------------------------------------------------------
///< Start up the driver and put it into a safe mode
c_WiFiDriver::c_WiFiDriver ()
{
    fsm_WiFi_state_Boot_imp.SetParent (this);
    fsm_WiFi_state_ConnectingUsingConfig_imp.SetParent (this);
    fsm_WiFi_state_ConnectingUsingDefaults_imp.SetParent (this);
    fsm_WiFi_state_ConnectedToAP_imp.SetParent (this);
    fsm_WiFi_state_ConnectingAsAP_imp.SetParent (this);
    fsm_WiFi_state_ConnectedToSta_imp.SetParent (this);
    fsm_WiFi_state_ConnectionFailed_imp.SetParent (this);
    fsm_WiFi_state_Disabled_imp.SetParent (this);

    fsm_WiFi_state_Boot_imp.Init ();
} // c_WiFiDriver

//-----------------------------------------------------------------------------
///< deallocate any resources and put the output channels into a safe state
c_WiFiDriver::~c_WiFiDriver()
{
 // DEBUG_START;

 // DEBUG_END;

} // ~c_WiFiDriver

//-----------------------------------------------------------------------------
void c_WiFiDriver::AnnounceState ()
{
    // DEBUG_START;

    String StateName;
    pCurrentFsmState->GetStateName (StateName);
    logcon (String (F ("WiFi Entering State: ")) + StateName);

    // DEBUG_END;

} // AnnounceState

//-----------------------------------------------------------------------------
///< Start the module
void c_WiFiDriver::Begin ()
{
    // DEBUG_START;

    // save the pointer to the config
    // config = NewConfig;

    if (FileMgr.SdCardIsInstalled())
    {
        DynamicJsonDocument jsonConfigDoc(1024);
        // DEBUG_V ("read the sdcard config");
        if (FileMgr.ReadSdFile (F("wificonfig.json"), jsonConfigDoc))
        {
            // DEBUG_V ("Process the sdcard config");
            JsonObject jsonConfig = jsonConfigDoc.as<JsonObject> ();

            // copy the fields of interest into the local structure
            setFromJSON (ssid,       jsonConfig, CN_ssid);
            setFromJSON (passphrase, jsonConfig, CN_passphrase);

            ConfigSaveNeeded = true;

            FileMgr.DeleteSdFile (F ("wificonfig.json"));
        }
        else
        {
            // DEBUG_V ("ERROR: Could not read SD card config");
        }
    }

    // Disable persistant credential storage and configure SDK params
    WiFi.persistent (false);

#ifdef ARDUINO_ARCH_ESP8266
    wifi_set_sleep_type (NONE_SLEEP_T);
    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-class.html#setoutputpower
    // https://github.com/esp8266/Arduino/issues/6366
    // AI Thinker FCC certification performed at 17dBm
    WiFi.setOutputPower (16);

#elif defined(ARDUINO_ARCH_ESP32)
    esp_wifi_set_ps (WIFI_PS_NONE);
#endif

    // DEBUG_V ("");

    // Setup WiFi Handlers
#ifdef ARDUINO_ARCH_ESP8266
    wifiConnectHandler    = WiFi.onStationModeGotIP        ([this](const WiFiEventStationModeGotIP& event) {this->onWiFiConnect (event); });
    wifiDisconnectHandler = WiFi.onStationModeDisconnected ([this](const WiFiEventStationModeDisconnected& event) {this->onWiFiDisconnect (event); });
#else
    WiFi.onEvent ([this](WiFiEvent_t event, arduino_event_info_t info) {this->onWiFiStaConn (event, info); }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent ([this](WiFiEvent_t event, arduino_event_info_t info) {this->onWiFiStaDisc (event, info); }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.onEvent ([this](WiFiEvent_t event, arduino_event_info_t info) {this->onWiFiConnect    (event, info);}, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent ([this](WiFiEvent_t event, arduino_event_info_t info) {this->onWiFiDisconnect (event, info);}, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif

    // set up the poll interval
    NextPollTime = millis () + PollInterval;

    // Main loop should start polling for us
    // pCurrentFsmState->Poll ();

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_WiFiDriver::connectWifi (const String & current_ssid, const String & current_passphrase)
{
    // DEBUG_START;

    // WiFi reset flag is set which will be handled in the next iteration of the main loop.
    // Ignore connect request to lessen boot spam.
    if (ResetWiFi)
    {
        // DEBUG_V ("WiFi Reset Requested");
        return;
    }

    SetUpIp ();

    String Hostname;
    NetworkMgr.GetHostname (Hostname);
    // DEBUG_V (String ("Hostname: ") + Hostname);

    // Hostname must be set after the mode on ESP8266 and before on ESP32
#ifdef ARDUINO_ARCH_ESP8266
    WiFi.disconnect ();

    // Switch to station mode
    WiFi.mode (WIFI_STA);
    // DEBUG_V ("");

    if (0 != Hostname.length ())
    {
        // DEBUG_V (String ("Setting WiFi hostname: ") + Hostname);
        WiFi.hostname (Hostname);
    }
#else
    WiFi.persistent (false);
    // DEBUG_V ("");
    WiFi.disconnect (true);

    if (0 != Hostname.length ())
    {
        // DEBUG_V (String ("Setting WiFi hostname: ") + Hostname);
        WiFi.hostname (Hostname);
    }

    // Switch to station mode
    WiFi.mode (WIFI_STA);
    // DEBUG_V ("");
#endif
    // DEBUG_V (String ("      ssid: ") + current_ssid);
    // DEBUG_V (String ("passphrase: ") + current_passphrase);
    // DEBUG_V (String ("  hostname: ") + Hostname);

    logcon (String(F ("Connecting to '")) +
                      current_ssid +
                      String (F ("' as ")) +
                      Hostname);

    WiFi.setSleep (false);
    WiFi.begin (current_ssid.c_str (), current_passphrase.c_str ());

    // DEBUG_END;

} // connectWifi

//-----------------------------------------------------------------------------
void c_WiFiDriver::Disable ()
{
    // DEBUG_START;
    // AnnounceState ();

    if (pCurrentFsmState != &fsm_WiFi_state_Disabled_imp)
    {
        // DEBUG_V ();
        WiFi.enableSTA (false);
        WiFi.enableAP (false);
        fsm_WiFi_state_Disabled_imp.Init ();
    }

    // DEBUG_END;

} // Disable

//-----------------------------------------------------------------------------
void c_WiFiDriver::Enable ()
{
    // DEBUG_START;
    // AnnounceState ();

    if (pCurrentFsmState == &fsm_WiFi_state_Disabled_imp)
    {
        // DEBUG_V ();
        WiFi.enableSTA (true);
        WiFi.enableAP (false);
        fsm_WiFi_state_ConnectionFailed_imp.Init ();
    }
    else
    {
        // DEBUG_V (String ("WiFi is not disabled"));
    }

    // DEBUG_END;
} // Enable

//-----------------------------------------------------------------------------
void c_WiFiDriver::GetConfig (JsonObject& json)
{
    // DEBUG_START;

    json[CN_ssid] = ssid;
    json[CN_passphrase] = passphrase;

#ifdef ARDUINO_ARCH_ESP8266
    IPAddress Temp = ip;
    json[CN_ip] = Temp.toString ();
    Temp = netmask;
    json[CN_netmask] = Temp.toString ();
    Temp = gateway;
    json[CN_gateway] = Temp.toString ();
#else
    json[CN_ip] = ip.toString ();
    json[CN_netmask] = netmask.toString ();
    json[CN_gateway] = gateway.toString ();
#endif // !def ARDUINO_ARCH_ESP8266

    json[CN_dhcp] = UseDhcp;
    json[CN_sta_timeout] = sta_timeout;
    json[CN_ap_fallback] = ap_fallbackIsEnabled;
    json[CN_ap_timeout] = ap_timeout;
    json[CN_ap_reboot] = RebootOnWiFiFailureToConnect;

    // DEBUG_END;

} // GetConfig

//-----------------------------------------------------------------------------
void c_WiFiDriver::GetHostname (String & name)
{
#ifdef ARDUINO_ARCH_ESP8266
    name = WiFi.hostname ();
#else
    name = WiFi.getHostname ();
#endif // def ARDUINO_ARCH_ESP8266

} // GetWiFiHostName

//-----------------------------------------------------------------------------
void c_WiFiDriver::GetStatus (JsonObject& jsonStatus)
{
    // DEBUG_START;
    String Hostname;
    GetHostname (Hostname);
    jsonStatus[CN_hostname] = Hostname;

    jsonStatus[CN_rssi] = WiFi.RSSI ();
    jsonStatus[CN_ip] = getIpAddress ().toString ();
    jsonStatus[CN_subnet] = getIpSubNetMask ().toString ();
    jsonStatus[CN_mac] = WiFi.macAddress ();
    jsonStatus[CN_ssid] = WiFi.SSID ();
    jsonStatus[CN_connected] = IsWiFiConnected ();

    // DEBUG_END;
} // GetStatus

//-----------------------------------------------------------------------------
#ifdef ARDUINO_ARCH_ESP32

//-----------------------------------------------------------------------------
void c_WiFiDriver::onWiFiStaConn (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
    // DEBUG_V ("ESP has associated with the AP");
} // onWiFiStaConn

//-----------------------------------------------------------------------------
void c_WiFiDriver::onWiFiStaDisc (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
    // DEBUG_V ("ESP has disconnected from the AP");
} // onWiFiStaDisc

#endif // def ARDUINO_ARCH_ESP32

//-----------------------------------------------------------------------------
#ifdef ARDUINO_ARCH_ESP8266
void c_WiFiDriver::onWiFiConnect (const WiFiEventStationModeGotIP& event)
{
#else
void c_WiFiDriver::onWiFiConnect (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
#endif
    // DEBUG_START;
    pCurrentFsmState->OnConnect ();

    // DEBUG_END;
} // onWiFiConnect

//-----------------------------------------------------------------------------
/// WiFi Disconnect Handler
#ifdef ARDUINO_ARCH_ESP8266
/** Attempt to re-connect every 2 seconds */
void c_WiFiDriver::onWiFiDisconnect (const WiFiEventStationModeDisconnected & event)
{
#else
void c_WiFiDriver::onWiFiDisconnect (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
#endif
    // DEBUG_START;

    pCurrentFsmState->OnDisconnect ();

    // DEBUG_END;

} // onWiFiDisconnect

//-----------------------------------------------------------------------------
void c_WiFiDriver::Poll ()
{
    // DEBUG_START;

    if (millis () > NextPollTime)
    {
        // DEBUG_V ("Start Poll");
        NextPollTime += PollInterval;
        // displayFsmState ();
        pCurrentFsmState->Poll ();
        // displayFsmState ();
        // DEBUG_V ("End Poll");
    }

    if (ResetWiFi)
    {
        ResetWiFi = false;
        reset ();
    }

    // DEBUG_END;

} // Poll

//-----------------------------------------------------------------------------
void c_WiFiDriver::reset ()
{
    // DEBUG_START;

    // Reset address in case we're switching from static to dhcp
    WiFi.config (0u, 0u, 0u);

    if (IsWiFiConnected ())
    {
        NetworkMgr.SetWiFiIsConnected (false);
    }

    fsm_WiFi_state_Boot_imp.Init ();

    // DEBUG_END;
} // reset

//-----------------------------------------------------------------------------
bool c_WiFiDriver::SetConfig (JsonObject & json)
{
    // DEBUG_START;

    bool ConfigChanged = false;

    String sIp = ip.toString ();
    String sGateway = gateway.toString ();
    String sNetmask = netmask.toString ();

    ConfigChanged |= setFromJSON (ssid, json, CN_ssid);
    ConfigChanged |= setFromJSON (passphrase, json, CN_passphrase);
    ConfigChanged |= setFromJSON (sIp, json, CN_ip);
    ConfigChanged |= setFromJSON (sNetmask, json, CN_netmask);
    ConfigChanged |= setFromJSON (sGateway, json, CN_gateway);
    ConfigChanged |= setFromJSON (UseDhcp, json, CN_dhcp);
    ConfigChanged |= setFromJSON (sta_timeout, json, CN_sta_timeout);
    ConfigChanged |= setFromJSON (ap_fallbackIsEnabled, json, CN_ap_fallback);
    ConfigChanged |= setFromJSON (ap_timeout, json, CN_ap_timeout);
    ConfigChanged |= setFromJSON (RebootOnWiFiFailureToConnect, json, CN_ap_reboot);

    // DEBUG_V ("     ip: " + ip);
    // DEBUG_V ("gateway: " + gateway);
    // DEBUG_V ("netmask: " + netmask);

    ip.fromString (sIp);
    gateway.fromString (sGateway);
    netmask.fromString (sNetmask);

    // DEBUG_V (String("ConfigChanged: ") + String(ConfigChanged));
    // DEBUG_END;
    return ConfigChanged;

} // SetConfig

//-----------------------------------------------------------------------------
void c_WiFiDriver::SetFsmState (fsm_WiFi_state* NewState)
{
    // DEBUG_START;
    // DEBUG_V (String ("pCurrentFsmState: ") + String (uint32_t (pCurrentFsmState), HEX));
    // DEBUG_V (String ("        NewState: ") + String (uint32_t (NewState), HEX));
    pCurrentFsmState = NewState;
    // displayFsmState ();
    // DEBUG_END;

} // SetFsmState

//-----------------------------------------------------------------------------
void c_WiFiDriver::SetHostname (String & )
{
    // DEBUG_START;

    // Need to reset the WiFi sub system if the host name changes
    reset ();

    // DEBUG_END;
} // SetHostname

//-----------------------------------------------------------------------------
void c_WiFiDriver::SetUpIp ()
{
    // DEBUG_START;

    do // once
    {
        if (true == UseDhcp)
        {
            logcon (F ("Using DHCP"));
            break;
        }

        IPAddress temp = (uint32_t)0;
        // DEBUG_V ("   temp: " + temp.toString ());
        // DEBUG_V ("     ip: " + ip.toString());
        // DEBUG_V ("netmask: " + netmask.toString ());
        // DEBUG_V ("gateway: " + gateway.toString ());

        if (temp == ip)
        {
            logcon (F ("ERROR: STATIC SELECTED WITHOUT IP. Using DHCP assigned address"));
            break;
        }

        if ((ip == WiFi.localIP ()) &&
            (netmask == WiFi.subnetMask ()) &&
            (gateway == WiFi.gatewayIP ()))
        {
            // correct IP is already set
            break;
        }
        // We didn't use DNS, so just set it to our configured gateway
        WiFi.config (ip, gateway, netmask, gateway);

        logcon (F ("Using Static IP"));

    } while (false);

    // DEBUG_END;

} // SetUpIp

//-----------------------------------------------------------------------------
int c_WiFiDriver::ValidateConfig ()
{
    // DEBUG_START;

    int response = 0;

    if (0 == ssid.length ())
    {
        ssid = ssid;
        // DEBUG_V ();
        response++;
    }

    if (0 == passphrase.length ())
    {
        passphrase = passphrase;
        // DEBUG_V ();
        response++;
    }

    if (sta_timeout < 5)
    {
        sta_timeout = CLIENT_TIMEOUT;
        // DEBUG_V ();
        response++;
    }

    if (ap_timeout < 15)
    {
        ap_timeout = AP_TIMEOUT;
        // DEBUG_V ();
        response++;
    }

    // DEBUG_END;

    return response;

} // ValidateConfig

/*****************************************************************************/
//  FSM Code
/*****************************************************************************/
/*****************************************************************************/
// Waiting for polling to start
void fsm_WiFi_state_Boot::Poll ()
{
    // DEBUG_START;

    // Start trying to connect to the AP
    // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
    fsm_WiFi_state_ConnectingUsingConfig_imp.Init ();
    // pWiFiDriver->displayFsmState ();

    // DEBUG_END;
} // fsm_WiFi_state_boot

/*****************************************************************************/
// Waiting for polling to start
void fsm_WiFi_state_Boot::Init ()
{
    // DEBUG_START;

    // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
    pWiFiDriver->SetFsmState (this);

    // This can get called before the system is up and running.
    // No log port available yet
    // pWiFiDriver->AnnounceState ();

    // DEBUG_END;
} // fsm_WiFi_state_Boot::Init

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingConfig::Poll ()
{
    // DEBUG_START;

    // wait for the connection to complete via the callback function
    uint32_t CurrentTimeMS = millis ();

    if (WiFi.status () != WL_CONNECTED)
    {
        if (CurrentTimeMS - pWiFiDriver->GetFsmStartTime() > (1000 * pWiFiDriver->Get_sta_timeout()))
        {
            // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
            logcon (F ("WiFi Failed to connect using Configured Credentials"));
            fsm_WiFi_state_ConnectingUsingDefaults_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingUsingConfig::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingConfig::Init ()
{
    // DEBUG_START;
    String CurrentSsid = pWiFiDriver->GetConfig_ssid ();
    String CurrentPassphrase = pWiFiDriver->GetConfig_passphrase ();
    // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));

    if ((0 == CurrentSsid.length ()) || (String("null") == CurrentSsid))
    {
        // DEBUG_V ();
        fsm_WiFi_state_ConnectingUsingDefaults_imp.Init ();
    }
    else
    {
        pWiFiDriver->SetFsmState (this);
        pWiFiDriver->AnnounceState ();
        pWiFiDriver->SetFsmStartTime (millis ());

        pWiFiDriver->connectWifi (CurrentSsid, CurrentPassphrase);
    }
    // pWiFiDriver->displayFsmState ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingUsingConfig::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingConfig::OnConnect ()
{
    // DEBUG_START;

    fsm_WiFi_state_ConnectedToAP_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingUsingConfig::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingDefaults::Poll ()
{
    // DEBUG_START;

    // wait for the connection to complete via the callback function
    uint32_t CurrentTimeMS = millis ();

    if (WiFi.status () != WL_CONNECTED)
    {
        if (CurrentTimeMS - pWiFiDriver->GetFsmStartTime () > (1000 * pWiFiDriver->Get_sta_timeout ()))
        {
            // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
            logcon (F ("WiFi Failed to connect using default Credentials"));
            fsm_WiFi_state_ConnectingAsAP_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingUsingDefaults::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingDefaults::Init ()
{
    // DEBUG_START;

    // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
    pWiFiDriver->SetFsmState (this);
    pWiFiDriver->AnnounceState ();
    pWiFiDriver->SetFsmStartTime (millis ());

    pWiFiDriver->connectWifi (default_ssid, default_passphrase);
    // pWiFiDriver->displayFsmState ();

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingUsingDefaults::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingDefaults::OnConnect ()
{
    // DEBUG_START;

    // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
    fsm_WiFi_state_ConnectedToAP_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingUsingDefaults::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingAsAP::Poll ()
{
    // DEBUG_START;

    if (0 != WiFi.softAPgetStationNum ())
    {
        fsm_WiFi_state_ConnectedToSta_imp.Init();
    }
    else
    {
        if (millis () - pWiFiDriver->GetFsmStartTime () > (1000 * pWiFiDriver->Get_ap_timeout ()))
        {
            logcon (F ("WiFi STA Failed to connect"));
            fsm_WiFi_state_ConnectionFailed_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingAsAP::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingAsAP::Init ()
{
    // DEBUG_START;

    pWiFiDriver->SetFsmState (this);
    pWiFiDriver->AnnounceState ();

    if (true == pWiFiDriver->Get_ap_fallbackIsEnabled())
    {
        WiFi.mode (WIFI_AP);
        String Hostname;
        NetworkMgr.GetHostname (Hostname);
        String ssid = "ESPixelStick-" + String (Hostname);
        WiFi.softAP (ssid.c_str ());

        pWiFiDriver->setIpAddress (WiFi.localIP ());
        pWiFiDriver->setIpSubNetMask (WiFi.subnetMask ());

        logcon (String (F ("WiFi SOFTAP:       ssid: '")) + ssid);
        logcon (String (F ("WiFi SOFTAP: IP Address: '")) + pWiFiDriver->getIpAddress ().toString ());
    }
    else
    {
        logcon (String (F ("WiFi SOFTAP: Not enabled")));
        fsm_WiFi_state_ConnectionFailed_imp.Init ();
    }

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingAsAP::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingAsAP::OnConnect ()
{
 // DEBUG_START;

    fsm_WiFi_state_ConnectedToSta_imp.Init ();

 // DEBUG_END;

} // fsm_WiFi_state_ConnectingAsAP::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToAP::Poll ()
{
    // DEBUG_START;

    // did we get silently disconnected?
    if (WiFi.status () != WL_CONNECTED)
    {
     // DEBUG_V ("WiFi Handle Silent Disconnect");
        WiFi.reconnect ();
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingAsAP::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToAP::Init ()
{
    // DEBUG_START;

    // DEBUG_V (String ("this: ") + String (uint32_t (this), HEX));
    pWiFiDriver->SetFsmState (this);
    pWiFiDriver->AnnounceState ();

    // WiFiDriver.SetUpIp ();

    pWiFiDriver->setIpAddress( WiFi.localIP () );
    pWiFiDriver->setIpSubNetMask( WiFi.subnetMask () );

    logcon (String (F ("Connected with IP: ")) + pWiFiDriver->getIpAddress ().toString ());

    pWiFiDriver->SetIsWiFiConnected (true);
    NetworkMgr.SetWiFiIsConnected (true);

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingAsAP::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToAP::OnDisconnect ()
{
    // DEBUG_START;

    logcon (F ("WiFi Lost the connection to the AP"));
    fsm_WiFi_state_ConnectionFailed_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectedToAP::OnDisconnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToSta::Poll ()
{
   // DEBUG_START;

    // did we get silently disconnected?
    if (0 == WiFi.softAPgetStationNum ())
    {
        logcon (F ("WiFi Lost the connection to the STA"));
        fsm_WiFi_state_ConnectionFailed_imp.Init ();
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectedToSta::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToSta::Init ()
{
    // DEBUG_START;

    pWiFiDriver->SetFsmState (this);
    pWiFiDriver->AnnounceState ();

    // WiFiDriver.SetUpIp ();

    pWiFiDriver->setIpAddress (WiFi.softAPIP ());
    pWiFiDriver->setIpSubNetMask (IPAddress (255, 255, 255, 0));

    logcon (String (F ("Connected to STA with IP: ")) + pWiFiDriver->getIpAddress ().toString ());

    pWiFiDriver->SetIsWiFiConnected (true);
    NetworkMgr.SetWiFiIsConnected (true);

    // DEBUG_END;
} // fsm_WiFi_state_ConnectedToSta::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToSta::OnDisconnect ()
{
    // DEBUG_START;

    logcon (F ("WiFi STA Disconnected"));
    fsm_WiFi_state_ConnectionFailed_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectedToSta::OnDisconnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectionFailed::Init ()
{
    // DEBUG_START;

    pWiFiDriver->SetFsmState (this);
    pWiFiDriver->AnnounceState ();

    if (pWiFiDriver->IsWiFiConnected())
    {
        pWiFiDriver->SetIsWiFiConnected (false);
        NetworkMgr.SetWiFiIsConnected (false);
    }
    else
    {
        if (true == pWiFiDriver->Get_RebootOnWiFiFailureToConnect())
        {
            extern bool reboot;
            logcon (F ("WiFi Requesting Reboot"));

            reboot = true;
        }
        else
        {
            // DEBUG_V ("WiFi Reboot Disabled.");

            // start over
            fsm_WiFi_state_Boot_imp.Init ();
        }
    }

    // DEBUG_END;

} // fsm_WiFi_state_ConnectionFailed::Init

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_Disabled::Init ()
{
    // DEBUG_START;

    pWiFiDriver->SetFsmState (this);
    pWiFiDriver->AnnounceState ();

    if (pWiFiDriver->IsWiFiConnected ())
    {
        pWiFiDriver->SetIsWiFiConnected (false);
        NetworkMgr.SetWiFiIsConnected (false);
    }

    // DEBUG_END;

} // fsm_WiFi_state_ConnectionFailed::Init

//-----------------------------------------------------------------------------
