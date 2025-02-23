/*
* NetworkMgr.cpp - Input Management class
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
*   This is a factory class used to manage the Input channel. It creates and deletes
*   the Input channel functionality as needed to support any new configurations
*   that get sent from from the WebPage.
*
*/

#include "../ESPixelStick.h"
#include "NetworkMgr.hpp"
#include "../input/InputMgr.hpp"
#include "../service/FPPDiscovery.h"
#include "../WebMgr.hpp"
#include <Int64String.h>

//-----------------------------------------------------------------------------
// Methods
//-----------------------------------------------------------------------------
///< Start up the driver and put it into a safe mode
c_NetworkMgr::c_NetworkMgr ()
{
} // c_NetworkMgr

//-----------------------------------------------------------------------------
///< deallocate any resources and put the Input channels into a safe state
c_NetworkMgr::~c_NetworkMgr ()
{
    // DEBUG_START;

    // DEBUG_END;

} // ~c_NetworkMgr

//-----------------------------------------------------------------------------
void c_NetworkMgr::AdvertiseNewState ()
{
    // DEBUG_START;

    if (PreviousState != IsConnected ())
    {
        // DEBUG_V ("Sending Advertisments");
        PreviousState = IsConnected ();
        InputMgr.NetworkStateChanged (IsConnected ());
        WebMgr.NetworkStateChanged (IsConnected ());
        FPPDiscovery.NetworkStateChanged (IsConnected ());
    }

    // DEBUG_END;
} // AdvertiseNewState

//-----------------------------------------------------------------------------
///< Start the module
void c_NetworkMgr::Begin ()
{
    // DEBUG_START;

    // prevent recalls
    if (true == HasBeenInitialized) { return; }
    HasBeenInitialized = true;

    // Make sure the local config is valid
    Validate ();

    WiFiDriver.Begin ();

#ifdef SUPPORT_ETHERNET
    EthernetDriver.Begin ();
#endif // def SUPPORT_ETHERNET

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_NetworkMgr::GetConfig (JsonObject & json)
{
    // DEBUG_START;

    JsonObject NetworkConfig = json.createNestedObject (CN_network);

    NetworkConfig[CN_hostname] = hostname;

    JsonObject NetworkWiFiConfig = NetworkConfig.createNestedObject (CN_wifi);
    WiFiDriver.GetConfig (NetworkWiFiConfig);

#ifdef SUPPORT_ETHERNET
    NetworkConfig[CN_weus] = AllowWiFiAndEthUpSimultaneously;

    JsonObject NetworkEthConfig = NetworkConfig.createNestedObject (CN_eth);
    EthernetDriver.GetConfig (NetworkEthConfig);

#endif // def SUPPORT_ETHERNET

    // DEBUG_END;
} // GetConfig

//-----------------------------------------------------------------------------
IPAddress c_NetworkMgr::GetlocalIP ()
{
    return WiFi.localIP ();
} // GetlocalIP

//-----------------------------------------------------------------------------
void c_NetworkMgr::GetStatus (JsonObject & json)
{
    // DEBUG_START;

    JsonObject NetworkStatus = json.createNestedObject (CN_network);
    String name;
    GetHostname (name);
    NetworkStatus[CN_hostname] = name;

    JsonObject NetworkWiFiStatus = NetworkStatus.createNestedObject (CN_wifi);
    WiFiDriver.GetStatus (NetworkWiFiStatus);

#ifdef SUPPORT_ETHERNET
    JsonObject NetworkEthStatus = NetworkStatus.createNestedObject (CN_eth);
    EthernetDriver.GetStatus (NetworkEthStatus);
#endif // def SUPPORT_ETHERNET

    // DEBUG_END;
} // GetStatus

//-----------------------------------------------------------------------------
///< Called from loop()
void c_NetworkMgr::Poll ()
{
    // DEBUG_START;

    do // once
    {
        WiFiDriver.Poll ();

#ifdef SUPPORT_ETHERNET
        EthernetDriver.Poll ();
#endif // def SUPPORT_ETHERNET

    } while (false);

    // DEBUG_END;
} // Process

//-----------------------------------------------------------------------------
bool c_NetworkMgr::SetConfig (JsonObject & json)
{
    // DEBUG_START;
    bool ConfigChanged = false;
    bool HostnameChanged = false;

    do // once
    {
        if (!json.containsKey (CN_network))
        {
            logcon (String (F ("No network config found. Use default settings")));
            // request config save
            ConfigSaveNeeded = true;
            break;
        }

        JsonObject network = json[CN_network];

        HostnameChanged = setFromJSON (hostname, network, CN_hostname);

        if (network.containsKey (CN_wifi))
        {
            JsonObject networkWiFi = network[CN_wifi];
            ConfigChanged |= WiFiDriver.SetConfig (networkWiFi);
        }
        else
        {
            // this may be an old style config
            if (network.containsKey (CN_ssid))
            {
                logcon (String (F ("Using old style WiFi Settings")));
                // request config save
                ConfigSaveNeeded = true;
                ConfigChanged |= WiFiDriver.SetConfig (network);
            }
            else
            {
                logcon (String (F ("No network WiFi settings found. Using default WiFi Settings")));
            }
        }

#ifdef SUPPORT_ETHERNET
        ConfigChanged = setFromJSON (AllowWiFiAndEthUpSimultaneously, network, CN_weus);

        if (network.containsKey (CN_eth))
        {
            JsonObject networkEth = network[CN_eth];
            ConfigChanged |= EthernetDriver.SetConfig (networkEth);
        }
        else
        {
            logcon (String (F ("No network Ethernet settings found. Using default Ethernet Settings")));
        }

        // DEBUG_V (String ("            IsEthernetConnected: ") + String (IsEthernetConnected));
        // DEBUG_V (String ("AllowWiFiAndEthUpSimultaneously: ") + String (AllowWiFiAndEthUpSimultaneously));
        SetWiFiEnable ();

#endif // def SUPPORT_ETHERNET

    } while (false);

    HostnameChanged |= Validate ();

    if(HostnameChanged)
    {
        WiFiDriver.SetHostname (hostname);
#ifdef SUPPORT_ETHERNET
        EthernetDriver.SetHostname (hostname);
#endif // def SUPPORT_ETHERNET
    }

    ConfigChanged |= HostnameChanged;

    // DEBUG_V (String ("ConfigChanged: ") + String (ConfigChanged));
    // DEBUG_END;
    return ConfigChanged;

} // SetConfig

//-----------------------------------------------------------------------------
bool c_NetworkMgr:: Validate ()
{
    // DEBUG_START;
    bool Changed = false;

    // DEBUG_V (String ("hostname: \"") + hostname + "\"");
    if (0 == hostname.length ())
    {
#ifdef ARDUINO_ARCH_ESP8266
        String chipId = String (ESP.getChipId (), HEX);
#else
        String chipId = int64String (ESP.getEfuseMac (), HEX);
#endif
        // DEBUG_V ("Setting Hostname default");
        hostname = "esps-" + String (chipId);
        Changed = true;
    }
    // DEBUG_V (String ("hostname: \"") + hostname + "\"");
    // DEBUG_V (String (" Changed: ") + String (Changed));

    // DEBUG_END;
    return Changed;
} // Validate

//-----------------------------------------------------------------------------
void c_NetworkMgr::SetWiFiIsConnected (bool newState)
{
    // DEBUG_START;

    if (IsWiFiConnected != newState)
    {
        IsWiFiConnected = newState;
        AdvertiseNewState ();
    }

    // DEBUG_END;
} // SetWiFiIsConnected

//-----------------------------------------------------------------------------
void c_NetworkMgr::SetWiFiEnable ()
{
    // DEBUG_START;

    if (!AllowWiFiAndEthUpSimultaneously)
    {
        // DEBUG_V ("!AllowWiFiAndEthUpSimultaneously");
        if (true == IsEthernetConnected)
        {
            // DEBUG_V ("Disable");
            WiFiDriver.Disable ();
        }
        else
        {
            // DEBUG_V ("Enable");
            WiFiDriver.Enable ();
        }
    }
    else
    {
        // DEBUG_V ("AllowWiFiAndEthUpSimultaneously");
        WiFiDriver.Enable ();
    }

} // SetWiFiEnabled

//-----------------------------------------------------------------------------
void c_NetworkMgr::SetEthernetIsConnected (bool newState)
{
    // DEBUG_START;
    // DEBUG_V (String ("newState: ") + String (newState));

    if (IsEthernetConnected != newState)
    {
        IsEthernetConnected = newState;
        SetWiFiEnable ();
        AdvertiseNewState ();
    }

    // DEBUG_END;
} // SetEthernetIsConnected

// create a global instance of the network manager
c_NetworkMgr NetworkMgr;
