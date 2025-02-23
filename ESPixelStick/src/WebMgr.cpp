/*
* WebMgr.cpp - Output Management class
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

#include "ESPixelStick.h"

#include "output/OutputMgr.hpp"
#include "input/InputMgr.hpp"
#include "service/FPPDiscovery.h"
#include "network/NetworkMgr.hpp"

#include "WebMgr.hpp"
#include "FileMgr.hpp"
#include <Int64String.h>

#include <FS.h>
#include <LittleFS.h>

#include <time.h>
#include <sys/time.h>
#include <functional>

// #define ESPALEXA_DEBUG
#define ESPALEXA_MAXDEVICES 2
#define ESPALEXA_ASYNC //it is important to define this before #include <Espalexa.h>!
#include <Espalexa.h>

const uint8_t HTTP_PORT = 80;      ///< Default web server port

static Espalexa         espalexa;
static EFUpdate         efupdate; /// EFU Update Handler
static AsyncWebServer   webServer (HTTP_PORT);  // Web Server
static AsyncWebSocket   webSocket ("/ws");      // Web Socket Plugin
static DynamicJsonDocument webJsonDoc (3 * WebSocketFrameCollectionBufferSize);

//-----------------------------------------------------------------------------
void PrettyPrint (JsonArray& jsonStuff, String Name)
{
    // DEBUG_V ("---------------------------------------------");
    LOG_PORT.println (String (F ("---- Pretty Print: '")) + Name + "'");
    serializeJson (jsonStuff, LOG_PORT);
    LOG_PORT.println ("");
    // DEBUG_V ("---------------------------------------------");

} // PrettyPrint

//-----------------------------------------------------------------------------
void PrettyPrint (JsonObject& jsonStuff, String Name)
{
    // DEBUG_V ("---------------------------------------------");
    LOG_PORT.println (String (F ("---- Pretty Print: '")) + Name + "'");
    serializeJson (jsonStuff, LOG_PORT);
    LOG_PORT.println ("");
    // DEBUG_V ("---------------------------------------------");

} // PrettyPrint

//-----------------------------------------------------------------------------
///< Start up the driver and put it into a safe mode
c_WebMgr::c_WebMgr ()
{
    // this gets called pre-setup so there is nothing we can do here.
} // c_WebMgr

//-----------------------------------------------------------------------------
///< deallocate any resources and put the output channels into a safe state
c_WebMgr::~c_WebMgr ()
{
    // DEBUG_START;

    // DEBUG_END;

} // ~c_WebMgr

//-----------------------------------------------------------------------------
///< Start the module
void c_WebMgr::Begin (config_t* /* NewConfig */)
{
    // DEBUG_START;

    if (NetworkMgr.IsConnected ())
    {
        init ();
    }

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
// Configure and start the web server
void c_WebMgr::NetworkStateChanged (bool NewNetworkState)
{
    // DEBUG_START;

    if (true == NewNetworkState)
    {
        init ();
    }

    // DEBUG_END;
} // NetworkStateChanged

//-----------------------------------------------------------------------------
// Configure and start the web server
void c_WebMgr::init ()
{
    // DEBUG_START;
    // Add header for SVG plot support?
    DefaultHeaders::Instance ().addHeader (F ("Access-Control-Allow-Origin"),  "*");
    DefaultHeaders::Instance ().addHeader (F ("Access-Control-Allow-Headers"), "append, delete, entries, foreach, get, has, keys, set, values, Authorization, Content-Type, Content-Range, Content-Disposition, Content-Description, cache-control, x-requested-with");
    DefaultHeaders::Instance ().addHeader (F ("Access-Control-Allow-Methods"), "GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH");

    // Setup WebSockets
    // webSocket.onEvent ([this](AsyncWebSocket* server, AsyncWebSocketClient * client, AwsEventType type, void* arg, uint8_t * data, size_t len)
    //     {
    //         this->onWsEvent (server, client, type, arg, data, len);
    //     });
    using namespace std::placeholders;
    webSocket.onEvent (std::bind (&c_WebMgr::onWsEvent, this, _1, _2, _3, _4, _5, _6));
    webServer.addHandler (&webSocket);

    // Heap status handler
    webServer.on ("/heap", HTTP_GET, [](AsyncWebServerRequest* request)
        {
            request->send (200, CN_textSLASHplain, String (ESP.getFreeHeap ()).c_str());
        });

    // JSON Config Handler
//TODO: This is only being used by FPP to get the hostname.  Will submit PR to change FPP and remove this
//      https://github.com/FalconChristmas/fpp/blob/ae10a0b6fb1e32d1982c2296afac9af92e4da908/src/NetworkController.cpp#L248
    webServer.on ("/conf", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            // DEBUG_V (CN_Heap_colon + String (ESP.getFreeHeap ()));
            this->GetConfiguration ();
            request->send (200, "text/json", WebSocketFrameCollectionBuffer);
            // DEBUG_V (CN_Heap_colon + String (ESP.getFreeHeap ()));
        });

    // Firmware upload handler
    webServer.on ("/updatefw", HTTP_POST, [](AsyncWebServerRequest* request)
        {
            webSocket.textAll ("X6");
        }, [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {WebMgr.FirmwareUpload (request, filename, index, data, len,  final); }).setFilter (ON_STA_FILTER);

    // URL's needed for FPP Connect fseq uploading and querying
    webServer.on ("/fpp", HTTP_GET,
        [](AsyncWebServerRequest* request)
        {
        FPPDiscovery.ProcessGET(request);
        });

    webServer.on ("/fpp", HTTP_POST | HTTP_PUT,
        [](AsyncWebServerRequest* request)
        {
            FPPDiscovery.ProcessPOST(request);
        },

        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
        {
            FPPDiscovery.ProcessFile(request, filename, index, data, len, final);
        },

        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            FPPDiscovery.ProcessBody(request, data, len, index, total);
        });

    // URL that FPP's status pages use to grab JSON about the current status, what's playing, etc...
    // This can be used to mimic the behavior of actual FPP remotes
    webServer.on ("/fppjson.php", HTTP_GET, [](AsyncWebServerRequest* request)
        {
            FPPDiscovery.ProcessFPPJson(request);
        });

    // Static Handlers
    webServer.serveStatic ("/UpdRecipe", LittleFS, "/UpdRecipe.json");
    // webServer.serveStatic ("/static", LittleFS, "/www/static").setCacheControl ("max-age=31536000");
    webServer.serveStatic ("/", LittleFS, "/www/").setDefaultFile ("index.html");

    // FS Debugging Handler
    // webServer.serveStatic ("/fs", LittleFS, "/" );

    // if the client posts to the upload page
    webServer.on ("/upload", HTTP_POST | HTTP_PUT | HTTP_OPTIONS,
        [](AsyncWebServerRequest * request)
        {
            // DEBUG_V ("Got upload post request");
            if (true == FileMgr.SdCardIsInstalled())
            {
                // Send status 200 (OK) to tell the client we are ready to receive
                request->send (200);
            }
            else
            {
                request->send (404, CN_textSLASHplain, "Page Not found");
            }
        },

        [this](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final)
        {
            // DEBUG_V ("Got process FIle request");
            // DEBUG_V (String ("Got process File request: name: ")  + filename);
            // DEBUG_V (String ("Got process File request: index: ") + String (index));
            // DEBUG_V (String ("Got process File request: len:   ") + String (len));
            // DEBUG_V (String ("Got process File request: final: ") + String (final));
            if (true == FileMgr.SdCardIsInstalled())
            {
                this->handleFileUpload (request, filename, index, data, len, final); // Receive and save the file
            }
            else
            {
                request->send (404, CN_textSLASHplain, "Page Not found");
            }
        },

        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
        {
            // DEBUG_V (String ("Got process Body request: index: ") + String (index));
            // DEBUG_V (String ("Got process Body request: len:   ") + String (len));
            // DEBUG_V (String ("Got process Body request: total: ") + String (total));
            request->send (404, CN_textSLASHplain, "Page Not found");
        }
    );

    webServer.on ("/download", HTTP_GET, [](AsyncWebServerRequest* request)
    {
        // DEBUG_V (String ("url: ") + String (request->url ()));
        String filename = request->url ().substring (String ("/download").length ());
        // DEBUG_V (String ("filename: ") + String (filename));

        AsyncWebServerResponse* response = new AsyncFileResponse (SDFS, filename, "application/octet-stream", true);
        request->send (response);

        // DEBUG_V ("Send File Done");
    });

    webServer.onNotFound ([this](AsyncWebServerRequest* request)
    {
        // DEBUG_V ("onNotFound");
        if (true == this->IsAlexaCallbackValid())
        {
            // DEBUG_V ("IsAlexaCallbackValid == true");
            if (!espalexa.handleAlexaApiCall (request)) //if you don't know the URI, ask espalexa whether it is an Alexa control request
            {
                // DEBUG_V ("Alexa Callback could not resolve the request");
                request->send (404, CN_textSLASHplain, "Page Not found");
            }
        }
        else
        {
            // DEBUG_V ("IsAlexaCallbackValid == false");
            request->send (404, CN_textSLASHplain, "Page Not found");
        }
    });

    //give espalexa a pointer to the server object so it can use your server instead of creating its own
    espalexa.begin (&webServer);

    // webServer.begin ();

    pAlexaDevice = new EspalexaDevice (String (F ("ESP")), [this](EspalexaDevice* pDevice)
        {
            this->onAlexaMessage (pDevice);

        }, EspalexaDeviceType::extendedcolor);

    pAlexaDevice->setName (config.id);
    espalexa.addDevice (pAlexaDevice);
    espalexa.setDiscoverable ((nullptr != pAlexaCallback) ? true : false);

    logcon (String (F ("Web server listening on port ")) + HTTP_PORT);

    // DEBUG_END;
}

//-----------------------------------------------------------------------------
void c_WebMgr::handleFileUpload (AsyncWebServerRequest* request,
                                 String filename,
                                 size_t index,
                                 uint8_t * data,
                                 size_t len,
                                 bool final)
{
    // DEBUG_START;

    FileMgr.handleFileUpload (filename, index, data, len, final);

    // DEBUG_END;
} // handleFileUpload

//-----------------------------------------------------------------------------
void c_WebMgr::RegisterAlexaCallback (DeviceCallbackFunction cb)
{
    // DEBUG_START;

    pAlexaCallback = cb;
    espalexa.setDiscoverable (IsAlexaCallbackValid());

    // DEBUG_END;
} // RegisterAlexaCallback

//-----------------------------------------------------------------------------
void c_WebMgr::onAlexaMessage (EspalexaDevice* dev)
{
    // DEBUG_START;
    if (true == IsAlexaCallbackValid())
    {
        // DEBUG_V ("");

        pAlexaCallback (dev);
    }
    // DEBUG_END;

} // onAlexaMessage

//-----------------------------------------------------------------------------
/*
    Gather config data from the various config sources and send it to the web page.
*/
void c_WebMgr::GetConfiguration ()
{
    extern void GetConfig (JsonObject & json);
    // DEBUG_START;

    webJsonDoc.clear ();
    JsonObject JsonSystemConfig = webJsonDoc.createNestedObject (CN_system);
    GetConfig (JsonSystemConfig);
    // DEBUG_V ("");

    // JsonObject JsonOutputConfig = webJsonDoc.createNestedObject (F ("outputs"));
    // OutputMgr.GetConfig (JsonOutputConfig);
    // DEBUG_V ("");

    // JsonObject JsonInputConfig = webJsonDoc.createNestedObject (F ("inputs"));
    // InputMgr.GetConfig (JsonInputConfig);

    // now make it something we can transmit
    serializeJson (webJsonDoc, WebSocketFrameCollectionBuffer);

    // DEBUG_END;

} // GetConfiguration

//-----------------------------------------------------------------------------
void c_WebMgr::GetDeviceOptions ()
{
    // DEBUG_START;
#ifdef SUPPORT_DEVICE_OPTION_LIST
    // set up a framework to get the option data
    webJsonDoc.clear ();

    if (0 == webJsonDoc.capacity ())
    {
        logcon (F ("ERROR: Failed to allocate memory for the GetDeviceOptions web request response."));
    }

    // DEBUG_V ("");
    JsonObject WebOptions = webJsonDoc.createNestedObject (F ("options"));
    JsonObject JsonDeviceOptions = WebOptions.createNestedObject (CN_device);
    // DEBUG_V("");

    // PrettyPrint (WebOptions);

    // PrettyPrint (WebOptions);

    // now make it something we can transmit
    size_t msgOffset = strlen (WebSocketFrameCollectionBuffer);
    serializeJson (WebOptions, &WebSocketFrameCollectionBuffer[msgOffset], (sizeof (WebSocketFrameCollectionBuffer) - msgOffset));
#endif // def SUPPORT_DEVICE_OPTION_LIST

    // DEBUG_END;

} // GetDeviceOptions

//-----------------------------------------------------------------------------
/// Handle Web Service events
/** Handles all Web Service event types and performs initial parsing of Web Service event data.
 * Text messages that start with 'X' are treated as "Simple" format messages, else they're parsed as JSON.
 */
void c_WebMgr::onWsEvent (AsyncWebSocket* server, AsyncWebSocketClient * client,
    AwsEventType type, void * arg, uint8_t * data, size_t len)
{
    // DEBUG_START;
    // DEBUG_V (CN_Heap_colon + String (ESP.getFreeHeap ()));

    switch (type)
    {
        case WS_EVT_DATA:
        {
            // DEBUG_V ("");

            AwsFrameInfo* MessageInfo = static_cast<AwsFrameInfo*>(arg);
            // DEBUG_V (String (F ("               len: ")) + len);
            // DEBUG_V (String (F ("MessageInfo->index: ")) + int64String (MessageInfo->index));
            // DEBUG_V (String (F ("  MessageInfo->len: ")) + int64String (MessageInfo->len));
            // DEBUG_V (String (F ("MessageInfo->final: ")) + String (MessageInfo->final));

            // only process text messages
            if (MessageInfo->opcode != WS_TEXT)
            {
                logcon (F ("-- Ignore binary message --"));
                break;
            }
            // DEBUG_V ("");

            // is this a message start?
            if (0 == MessageInfo->index)
            {
                // clear the string we are building
                memset (WebSocketFrameCollectionBuffer, 0x0, sizeof (WebSocketFrameCollectionBuffer));
                // DEBUG_V ("");
            }
            // DEBUG_V ("");

            // will the message fit into our buffer?
            if (WebSocketFrameCollectionBufferSize < (MessageInfo->index + len))
            {
                // message wont fit. Dont save any of it
                logcon (String (F ("*** onWsEvent() error: Incoming message is too long.")));
                break;
            }

            // add the current data to the aggregate message
            memcpy (&WebSocketFrameCollectionBuffer[MessageInfo->index], data, len);

            // is the message complete?
            if ((MessageInfo->index + len) != MessageInfo->len)
            {
                // DEBUG_V ("The message is not yet complete");
                break;
            }
            // DEBUG_V ("");

            // did we get the final part of the message?
            if (!MessageInfo->final)
            {
                // DEBUG_V ("message is not terminated yet");
                break;
            }

            // DEBUG_V (WebSocketFrameCollectionBuffer);
            // message is all here. Process it

            FeedWDT ();

            if (WebSocketFrameCollectionBuffer[0] == 'X')
            {
                // DEBUG_V ("");
                ProcessXseriesRequests (client);
                break;
            }

            if (WebSocketFrameCollectionBuffer[0] == 'V')
            {
                // DEBUG_V ("");
                ProcessVseriesRequests (client);
                break;
            }

            if (WebSocketFrameCollectionBuffer[0] == 'G')
            {
                // DEBUG_V ("");
                ProcessGseriesRequests (client);
                break;
            }

            OutputMgr.PauseOutputs ();

            // convert the input data into a json structure (use json read only mode)
            webJsonDoc.clear ();
            DeserializationError error = deserializeJson (webJsonDoc, (const char *)(&WebSocketFrameCollectionBuffer[0]));

            // DEBUG_V ("");
            if (error)
            {
                logcon (CN_stars + String (F (" WebIO::onWsEvent(): Parse Error: ")) + error.c_str ());
                logcon (WebSocketFrameCollectionBuffer);
                break;
            }
            // DEBUG_V ("");

            ProcessReceivedJsonMessage (webJsonDoc, client);
            // DEBUG_V ("");

            break;
        } // case WS_EVT_DATA:

        case WS_EVT_CONNECT:
        {
            webSocket.cleanupClients ();
            logcon (String (F ("WS client connect - ")) + client->id ());
            break;
        } // case WS_EVT_CONNECT:

        case WS_EVT_DISCONNECT:
        {
            logcon (String (F ("WS client disconnect - ")) + client->id ());
            break;
        } // case WS_EVT_DISCONNECT:

        case WS_EVT_PONG:
        {
            logcon (F ("* WS PONG *"));
            break;
        } // case WS_EVT_PONG:

        case WS_EVT_ERROR:
        default:
        {
            webSocket.cleanupClients ();
            logcon (F ("** WS ERROR **"));
            break;
        }
    } // end switch (type)

    FeedWDT ();

    // DEBUG_V (CN_Heap_colon + String (ESP.getFreeHeap ()));

    // DEBUG_END;

} // onEvent

//-----------------------------------------------------------------------------
/// Process simple format 'X' messages
/// XA and XJ messages are used by FPP
void c_WebMgr::ProcessXseriesRequests (AsyncWebSocketClient * client)
{
    // DEBUG_START;

    switch (WebSocketFrameCollectionBuffer[1])
    {
        case SimpleMessage::GET_STATUS:
        {
            // DEBUG_V ("");
            ProcessXJRequest (client);
            break;
        } // end case SimpleMessage::GET_STATUS:

        case SimpleMessage::PING:
        {
            client->text (CN_XP);
            break;
        }

        case SimpleMessage::GET_ADMIN:
        {
            // DEBUG_V ("");
            ProcessXARequest (client);
            break;
        } // end case SimpleMessage::GET_ADMIN:

        case SimpleMessage::DO_RESET:
        {
            // DEBUG_V ("");
            extern bool reboot;
            reboot = true;
            break;
        } // end case SimpleMessage::DO_RESET:

        case SimpleMessage::DO_FACTORYRESET:
        {
            extern void DeleteConfig ();
            // DEBUG_V ("");
            InputMgr.DeleteConfig ();
            OutputMgr.DeleteConfig ();
            DeleteConfig ();

            // DEBUG_V ("");
            extern bool reboot;
            reboot = true;
            break;
        } // end case SimpleMessage::DO_RESET:

        default:
        {
            logcon (String (F ("ERROR: Unhandled request: ")) + WebSocketFrameCollectionBuffer);
            client->text ((String (F ("{\"Error\":Error"))).c_str());
            break;
        }

    } // end switch (data[1])

    // DEBUG_END;

} // ProcessXseriesRequests

//-----------------------------------------------------------------------------
void c_WebMgr::ProcessXARequest (AsyncWebSocketClient* client)
{
    // DEBUG_START;

    webJsonDoc.clear ();
    JsonObject jsonAdmin = webJsonDoc.createNestedObject (F ("admin"));

    jsonAdmin[CN_version] = VERSION;
    jsonAdmin["built"] = BUILD_DATE;
    jsonAdmin["realflashsize"] = String (ESP.getFlashChipSize ());
#ifdef ARDUINO_ARCH_ESP8266
    jsonAdmin["arch"] = CN_ESP8266;
    jsonAdmin["flashchipid"] = String (ESP.getChipId (), HEX);
#elif defined (ARDUINO_ARCH_ESP32)
    jsonAdmin["arch"] = CN_ESP32;
    jsonAdmin["flashchipid"] = int64String (ESP.getEfuseMac (), HEX);
#endif

    memset (&WebSocketFrameCollectionBuffer[0], 0x00, sizeof (WebSocketFrameCollectionBuffer));
    strcpy (WebSocketFrameCollectionBuffer, "XA");
    size_t msgOffset = strlen (WebSocketFrameCollectionBuffer);
    serializeJson (webJsonDoc, &WebSocketFrameCollectionBuffer[msgOffset], (sizeof (WebSocketFrameCollectionBuffer) - msgOffset));
    // DEBUG_V (String(WebSocketFrameCollectionBuffer));

    client->text (WebSocketFrameCollectionBuffer);

    // DEBUG_END;

} // ProcessXARequest

//-----------------------------------------------------------------------------
void c_WebMgr::ProcessXJRequest (AsyncWebSocketClient* client)
{
    // DEBUG_START;

    webJsonDoc.clear ();
    JsonObject status = webJsonDoc.createNestedObject (CN_status);
    JsonObject system = status.createNestedObject (CN_system);

    system[F ("freeheap")] = ESP.getFreeHeap ();
    system[F ("uptime")] = millis ();
    system[F ("SDinstalled")] = FileMgr.SdCardIsInstalled ();

    // DEBUG_V ("");

    // Ask WiFi Stats
    NetworkMgr.GetStatus (system);
    // DEBUG_V ("");

    FPPDiscovery.GetStatus (system);
    // DEBUG_V ("");

    // Ask Input Stats
    InputMgr.GetStatus (status);
    // DEBUG_V ("");

    // Ask Output Stats
    OutputMgr.GetStatus (status);
    // DEBUG_V ("");

    memset (&WebSocketFrameCollectionBuffer[0], 0x00, sizeof (WebSocketFrameCollectionBuffer));
    strcpy (WebSocketFrameCollectionBuffer, "XJ");
    size_t msgOffset = strlen (WebSocketFrameCollectionBuffer);
    serializeJson (webJsonDoc, &WebSocketFrameCollectionBuffer[msgOffset], (sizeof (WebSocketFrameCollectionBuffer) - msgOffset));

    // DEBUG_V (response);

    client->text (WebSocketFrameCollectionBuffer);
    // client->text ((F ("XJ{\"status\":{\"system\":{\"freeheap\":\"18504\",\"uptime\":14089,\"SDinstalled\":true,\"rssi\":-69,\"ip\":\"192.168.10.237\",\"subnet\":\"255.255.255.0\",\"mac\":\"24:A1:60 : 2E : 09 : 5D\",\"hostname\":\"esps - 2e095d\",\"ssid\":\"MaRtInG\",\"FPPDiscovery\":{\"FppRemoteIp\":\"(IP unset)\",\"SyncCount\":0,\"SyncAdjustmentCount\":0,\"current_sequence\":\"\",\"playlist\":\"\",\"seconds_elapsed\":\"0\",\"seconds_played\":\"0\",\"seconds_remaining\":\"0\",\"sequence_filename\":\"\",\"time_elapsed\":\"00 : 00\",\"time_remaining\":\"00 : 00\",\"errors\":\"\"}},\"inputbutton\":{\"id\":0,\"state\":\"off\"},\"input\":[{\"e131\":{\"id\":0,\"unifirst\":1,\"unilast\":5,\"unichanlim\":512,\"num_packets\":0,\"last_clientIP\":0,\"channels\":[{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0},{\"errors\":0}],\"packet_errors\":0}},{\"LocalPlayer\":{\"id\":1,\"active\":false}}],\"output\":[{\"id\":0,\"framerefreshrate\":41,\"FrameCount\":528},{\"id\":1,\"framerefreshrate\":0,\"FrameCount\":0}]}}")));

    // DEBUG_END;

} // ProcessXJRequest

//-----------------------------------------------------------------------------
/// Process simple format 'V' messages
void c_WebMgr::ProcessVseriesRequests (AsyncWebSocketClient* client)
{
    // DEBUG_START;

    // String Response;
    // serializeJson (webJsonDoc, response);
    switch (WebSocketFrameCollectionBuffer[1])
    {
        case '1':
        {
            // Diag screen is asking for real time output data
            if (OutputMgr.GetBufferUsedSize ())
            {
                client->binary (OutputMgr.GetBufferAddress (), OutputMgr.GetBufferUsedSize ());
            } else {
                // Diagnostics tab needs something or it'll clog up the websocket queue with timeouts
                client->binary ("0");
            }
            break;
        }

        default:
        {
            client->text (F ("V Error"));
            logcon (String(CN_stars) + F ("ERROR: Unsupported Web command V") + WebSocketFrameCollectionBuffer[1] + CN_stars);
            break;
        }
    } // end switch

    // DEBUG_END;

} // ProcessVseriesRequests

//-----------------------------------------------------------------------------
/// Process simple format 'G' messages
/// G2 messages are used by xLights and FPP
void c_WebMgr::ProcessGseriesRequests (AsyncWebSocketClient* client)
{
    // DEBUG_START;

    // String Response;
    // serializeJson (webJsonDoc, response);
    switch (WebSocketFrameCollectionBuffer[1])
    {
        case '2':
        {
            // xLights asking the "version"
            client->text ((String (F ("G2{\"version\": \"")) + VERSION + "\"}").c_str());
            break;
        }

        default:
        {
            client->text (F ("G Error"));
            logcon (String(CN_stars) + F ("ERROR: Unsupported Web command V") + WebSocketFrameCollectionBuffer[1] + CN_stars);
            break;
        }
    } // end switch

    // DEBUG_END;

} // ProcessVseriesRequests

//-----------------------------------------------------------------------------
// Process JSON messages
void c_WebMgr::ProcessReceivedJsonMessage (DynamicJsonDocument & webJsonDoc, AsyncWebSocketClient * client)
{
    // DEBUG_START;
    // LOG_PORT.printf_P( PSTR("ProcessReceivedJsonMessage heap / stack Stats: %u:%u:%u:%u\n"), ESP.getFreeHeap(), ESP.getHeapFragmentation(), ESP.getMaxFreeBlockSize(), ESP.getFreeContStack());

    do // once
    {
        // DEBUG_V ("");

        /** Following commands are supported:
         * - get: returns requested configuration
         * - set: receive and applies configuration
         * - opt: returns select option lists
         */
        if (webJsonDoc.containsKey (CN_cmd))
        {
            // DEBUG_V ("cmd");
            {
                // JsonObject jsonCmd = webJsonDoc.as<JsonObject> ();
                // PrettyPrint (jsonCmd);
            }
            JsonObject jsonCmd = webJsonDoc[CN_cmd];
            processCmd (client, jsonCmd);
            break;
        } // webJsonDoc.containsKey ("cmd")

        // DEBUG_V ("");

    } while (false);

    // DEBUG_END;

} // ProcessReceivedJsonMessage

//-----------------------------------------------------------------------------
/// Process JSON command messages
/// These messages are used by xLights and FPP
void c_WebMgr::processCmd (AsyncWebSocketClient * client, JsonObject & jsonCmd)
{
    // DEBUG_START;

    // PrettyPrint (jsonCmd);

    do // once
    {
        // Process get command - return requested configuration as JSON
        if (jsonCmd.containsKey (CN_get))
        {
            // DEBUG_V (CN_get);
            strcpy(WebSocketFrameCollectionBuffer, "{\"get\":");
            // DEBUG_V ("");
            processCmdGet (jsonCmd);
            strcat (WebSocketFrameCollectionBuffer, "}");
            // DEBUG_V ("");
            break;
        }

        // Process "SET" command - return requested configuration as JSON
        if (jsonCmd.containsKey ("set"))
        {
            // DEBUG_V ("set");
            // strcpy(WebSocketFrameCollectionBuffer, "{\"set\":");
            JsonObject jsonCmdSet = jsonCmd["set"];
            // DEBUG_V ("");
//TODO:  This will get called when time is set as well. We need something we can return
//       to identify configration was saved and there were no errors. for now, we return
//       that time was set.  In future, should return if configuration saved was valid.
//       'OK' will trigger snackSave in UI.
            if (processCmdSet (jsonCmdSet)) {
                strcpy (WebSocketFrameCollectionBuffer, "{\"cmd\":\"OK\"}");
            } else {
                strcpy (WebSocketFrameCollectionBuffer, "{\"cmd\":\"TIME_SET\"}");
            }
            // DEBUG_V ("");
            break;
        }

        // Generate select option list data
        if (jsonCmd.containsKey ("opt"))
        {
            // DEBUG_V ("opt");
            strcpy(WebSocketFrameCollectionBuffer, "{\"opt\":");
            // DEBUG_V ("");
            processCmdOpt (jsonCmd);
            strcat (WebSocketFrameCollectionBuffer, "}");

            // DEBUG_V ("");
            break;
        }

        if (jsonCmd.containsKey ("delete"))
        {
            // DEBUG_V ("opt");
            JsonObject temp = jsonCmd["delete"];
            processCmdDelete (temp);
            strcpy (WebSocketFrameCollectionBuffer, "{\"cmd\":\"OK\"}");
            // DEBUG_V ("");
            break;
        }

        // log an error
        PrettyPrint (jsonCmd, String (F ("ERROR: Unhandled cmd")));
        strcpy (WebSocketFrameCollectionBuffer, "{\"cmd\":\"Error\"}");

    } while (false);

    client->text (WebSocketFrameCollectionBuffer);

    // DEBUG_END;

} // processCmd

//-----------------------------------------------------------------------------
void c_WebMgr::processCmdGet (JsonObject & jsonCmd)
{
    // DEBUG_START;
    // PrettyPrint (jsonCmd);

    // DEBUG_V ("");

    do // once
    {
        size_t bufferoffset = strlen(WebSocketFrameCollectionBuffer);
        size_t BufferFreeSize = sizeof (WebSocketFrameCollectionBuffer) - bufferoffset;

        if ((jsonCmd[CN_get] == CN_system) || (jsonCmd[CN_get] == CN_device))
        {
            // DEBUG_V ("system");
            FileMgr.ReadConfigFile (ConfigFileName,
                                    (byte*)&WebSocketFrameCollectionBuffer[bufferoffset],
                                    BufferFreeSize);
            // DEBUG_V ("");
            break;
        }

        if (jsonCmd[CN_get] == CN_output)
        {
            // DEBUG_V (CN_output);
            OutputMgr.GetConfig ((byte*)&WebSocketFrameCollectionBuffer[bufferoffset],
                                 BufferFreeSize);
            // DEBUG_V ("");
            break;
        }

        if (jsonCmd[CN_get] == CN_input)
        {
            // DEBUG_V ("input");
            InputMgr.GetConfig ((byte*)&WebSocketFrameCollectionBuffer[bufferoffset],
                                BufferFreeSize);
            // DEBUG_V ("");
            break;
        }

        if (jsonCmd[CN_get] == CN_files)
        {
            // DEBUG_V ("input");
            String Temp;
            FileMgr.GetListOfSdFiles (Temp);
            if (Temp.length () >= BufferFreeSize )
            {
                strcat (WebSocketFrameCollectionBuffer, "\"ERROR\": \"File List Too Long\"");
            }
            else
            {
                strcat (WebSocketFrameCollectionBuffer, Temp.c_str ());
            }
            // DEBUG_V ("");
            break;
        }

        // log an error
        PrettyPrint (jsonCmd, String (F ("ERROR: Unhandled Get Request")));
        strcat (WebSocketFrameCollectionBuffer, "\"ERROR\": \"Request Not Supported\"");

    } while (false);

    // DEBUG_V (WebSocketFrameCollectionBuffer);

    // DEBUG_END;

} // processCmdGet

//-----------------------------------------------------------------------------
bool c_WebMgr::processCmdSet (JsonObject & jsonCmd)
{
    // DEBUG_START;
    // PrettyPrint (jsonCmd);

//TODO: For now, we return if we need to send an ack / 'OK'.  Should be if config succeeded or failed
//      to push proper state to UI.  For now, we just ignore time sets so user gets feedback that
//      config was saved even though UI may be in invalid state.  To be fixed in UI rework.
    bool retval = true;

    do // once
    {
        if (jsonCmd.containsKey (CN_device))
        {
            // DEBUG_V ("device/network");
            extern void SetConfig (const char* DataString);
            serializeJson (jsonCmd, WebSocketFrameCollectionBuffer, sizeof (WebSocketFrameCollectionBuffer) - 1);
            SetConfig (WebSocketFrameCollectionBuffer);
            pAlexaDevice->setName (config.id);

            // DEBUG_V ("device/network: Done");
            break;
        }

        if (jsonCmd.containsKey (CN_input))
        {
            // DEBUG_V ("input");
            JsonObject imConfig = jsonCmd[CN_input];
            serializeJson (imConfig, WebSocketFrameCollectionBuffer, sizeof (WebSocketFrameCollectionBuffer) - 1);
            InputMgr.SetConfig (WebSocketFrameCollectionBuffer);
            // DEBUG_V ("input: Done");
            break;
        }

        if (jsonCmd.containsKey (CN_output))
        {
            // DEBUG_V (CN_output);
            JsonObject omConfig = jsonCmd[CN_output];
            serializeJson (omConfig, WebSocketFrameCollectionBuffer, sizeof(WebSocketFrameCollectionBuffer) - 1);
            OutputMgr.SetConfig (WebSocketFrameCollectionBuffer);
            // DEBUG_V ("output: Done");
            break;
        }

        if (jsonCmd.containsKey (CN_time))
        {
//TODO:  Send proper retval once upper logic is in place
            retval = false;
            // PrettyPrint (jsonCmd, String ("processCmdSet"));

            // DEBUG_V ("time");
            JsonObject otConfig = jsonCmd[CN_time];
            processCmdSetTime (otConfig);
            // DEBUG_V ("output: Done");
            break;
        }

        // logcon (" ");
        PrettyPrint (jsonCmd, String(CN_stars) + F (" ERROR: Undhandled Set request type. ") + CN_stars );
        strcat (WebSocketFrameCollectionBuffer, "ERROR");

    } while (false);

    return retval;

    // DEBUG_END;

} // processCmdSet

//-----------------------------------------------------------------------------
void c_WebMgr::processCmdSetTime (JsonObject& jsonCmd)
{
    // DEBUG_START;

    // PrettyPrint (jsonCmd, String("processCmdSetTime"));

    time_t TimeToSet = 0;
    setFromJSON (TimeToSet, jsonCmd, F ("time_t"));

    struct timeval now = { .tv_sec = TimeToSet };

    settimeofday (&now, NULL);

    // DEBUG_V (String ("TimeToSet: ") + String (TimeToSet));
    // DEBUG_V (String ("TimeToSet: ") + String (ctime(&TimeToSet)));

    strcat (WebSocketFrameCollectionBuffer, "{\"OK\" : true}");

    // DEBUG_END;

} // ProcessXTRequest

//-----------------------------------------------------------------------------
void c_WebMgr::processCmdOpt (JsonObject & jsonCmd)
{
    // DEBUG_START;
    // PrettyPrint (jsonCmd);

    do // once
    {
        // DEBUG_V ("");
        if (jsonCmd[F ("opt")] == CN_device)
        {
            // DEBUG_V (CN_device);
            GetDeviceOptions ();
            break;
        }

        // log error
        PrettyPrint (jsonCmd, String (F ("ERROR: Unhandled 'opt' Request: ")));

    } while (false);

    // DEBUG_END;

} // processCmdOpt

//-----------------------------------------------------------------------------
void c_WebMgr::processCmdDelete (JsonObject& jsonCmd)
{
    // DEBUG_START;

    do // once
    {
        // DEBUG_V ("");

        if (jsonCmd.containsKey (CN_files))
        {
            // DEBUG_V ("");

            JsonArray JsonFilesToDelete = jsonCmd[CN_files];

            // DEBUG_V ("");
            for (JsonObject JsonFile : JsonFilesToDelete)
            {
                String FileToDelete = JsonFile[CN_name];

                // DEBUG_V ("FileToDelete: " + FileToDelete);
                FileMgr.DeleteSdFile (FileToDelete);
            }

            String Temp;
            FileMgr.GetListOfSdFiles (Temp);
            Temp += "}";
            strcpy (WebSocketFrameCollectionBuffer, "{\"cmd\": { \"delete\": ");
            strcat (WebSocketFrameCollectionBuffer, Temp.c_str ());

            break;
        }

        PrettyPrint (jsonCmd, String (F ("* Unsupported Delete command: ")));
        strcat (WebSocketFrameCollectionBuffer, "Page Not found");

    } while (false);

    // DEBUG_END;

} // processCmdDelete

//-----------------------------------------------------------------------------
void c_WebMgr::FirmwareUpload (AsyncWebServerRequest* request,
                               String filename,
                               size_t index,
                               uint8_t * data,
                               size_t len,
                               bool final)
{
    // DEBUG_START;

    do // once
    {
        // make sure we are in AP mode
        if (0 == WiFi.softAPgetStationNum ())
        {
            // DEBUG_V("Not in AP Mode");

            // we are not talking to a station so we are not in AP mode
            // break;
        }
        // DEBUG_V ("In AP Mode");

        // is the first message in the upload?
        if (0 == index)
        {
#ifdef ARDUINO_ARCH_ESP8266
            WiFiUDP::stopAll ();
#else
            // this is not supported for ESP32
#endif
            logcon (String(F ("Upload Started: ")) + filename);
            efupdate.begin ();
        }

        // DEBUG_V ("Sending data to efupdate");
        // DEBUG_V (String ("data: 0x") + String (uint32(data), HEX));
        // DEBUG_V (String (" len: ") + String (len));

        if (!efupdate.process (data, len))
        {
            logcon (String(CN_stars) + F (" UPDATE ERROR: ") + String (efupdate.getError ()));
        }
        // DEBUG_V ("Packet has been processed");

        if (efupdate.hasError ())
        {
            // DEBUG_V ("efupdate.hasError");
            request->send (200, CN_textSLASHplain, (String (F ("Update Error: ")) + String (efupdate.getError ()).c_str()));
            break;
        }
        // DEBUG_V ("No Error");

        if (final)
        {
            request->send (200, CN_textSLASHplain, (String ( F ("Update Finished: ")) + String (efupdate.getError ())).c_str());
            logcon (F ("Upload Finished."));
            efupdate.end ();
            LittleFS.begin ();

            extern bool reboot;
            reboot = true;
        }

    } while (false);

    // DEBUG_END;

} // onEvent

//-----------------------------------------------------------------------------
/*
 * This function is called as part of the Arduino "loop" and does things that need
 * periodic poking.
 *
 */
void c_WebMgr::Process ()
{
   if (true == IsAlexaCallbackValid())
    {
        espalexa.loop ();
    }

    webSocket.cleanupClients();

} // Process


//-----------------------------------------------------------------------------
// create a global instance of the WEB UI manager
c_WebMgr WebMgr;
