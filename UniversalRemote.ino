#include <memory>
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Ticker.h>  // Ticker Library

#define DEBUG_ESP_PORT Serial

#include <ESPAsyncWebServer.h>
#include <WebSockets.h>
#include <WebSocketsServer.h>

void debugPrint(const String& str);

#include "adb.h"
#include "persistent.h"
#include "ir_remote.h"

extern "C" {
#include "user_interface.h"
}

#define RECV_PIN D2
#define BAUD_RATE 115200

IRrecv irrecv(RECV_PIN); // 

std::auto_ptr<AsyncWebServer> setupServer;
std::auto_ptr<WebSocketsServer> webSocket;

void debugPrint(const String& str) {
  if (webSocket.get()) {
    String toSend;
    toSend = "{ \"type\": \"log\", \"val\": \"" + str + "\" }";
    webSocket->broadcastTXT(toSend.c_str(), toSend.length());
  }
}

const char* wifiFileName = "wifi.name";
const char* wifiPwdName = "wifi.pwd";
const char* lastChannelNum = "last.channel.name";

int youtubeChannel = 0;

const String typeKey("type");

Ticker initADB;

void setup() {
  SPIFFS.begin();

  // pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D7, OUTPUT);
  Serial.begin(BAUD_RATE);

  Serial.println("Connecting to WiFi");

  webSocket.reset(new WebSocketsServer(8081, "*"));

  String wifiName = persistent::fileToString(wifiFileName);
  String wifiPwd = persistent::fileToString(wifiPwdName);
  String lastChannel = persistent::fileToString(lastChannelNum);
  if (lastChannel.length() > 0) {
    youtubeChannel = lastChannel.toInt();
  }

  if (wifiName.length() > 0 && wifiPwd.length() > 0) {
    WiFi.begin(wifiName.c_str(), wifiPwd.c_str());
    WiFi.waitForConnectResult();
  }

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    Serial.println("Connected to WiFi " + ip.toString());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    String networks("[");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      if (networks.length() > 1) {
        networks += ", ";
      }
      networks += "\"" + WiFi.SSID(i) + "\"";
    }

    WiFi.mode(WIFI_AP);
    String chidIp = String(ESP.getChipId(), HEX);
    String wifiAPName = ("ESP8266_Remote_") + chidIp;
    String wifiPwd = String("pwd") + chidIp;
    WiFi.softAP(wifiAPName.c_str(), wifiPwd.c_str());

    IPAddress accessIP = WiFi.softAPIP();
    Serial.print("ESP AccessPoint IP address: "); Serial.println(accessIP);
  }

  setupServer.reset(new AsyncWebServer(80));
  setupServer->on("/", [](AsyncWebServerRequest *request) {
      String content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 at ";
      content += "<p>";
      content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
      content += "</html>";
      request->send(200, "text/html", content);  
  });
  setupServer->begin();

  webSocket->onEvent([&](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
      case WStype_DISCONNECTED: {
        Serial.printf("[%u] Disconnected!\n", num);
        break;
      }
      case WStype_CONNECTED: {
          IPAddress ip = webSocket->remoteIP(num);
          Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    
          // send message to client
          debugPrint("Connected client " + String(num, DEC));
          break;
      }
      case WStype_TEXT: {
        Serial.printf("[%u] get Text: %s\n", num, payload);
        DynamicJsonBuffer jsonBuffer;

        JsonObject& root = jsonBuffer.parseObject(payload);

        if (!root.success()) {
          Serial.println("parseObject() failed");
          webSocket->sendTXT(num, "{ \"errorMsg\":\"Failed to parse JSON\" }");
          return;
        }

        String type = root[typeKey];
        if (type == "wificredentials") {
          persistent::stringToFile(wifiFileName, root["ssid"]);
          persistent::stringToFile(wifiPwdName, root["pwd"]);         

          webSocket->sendTXT(num, "{ \"result\":\"OK, will reboot\" }");
          ESP.reset();
        }

        // send data to all connected clients
        // webSocket.broadcastTXT("message here");
        break;
      }
      case WStype_BIN: {
        // Serial.printf("[%u] get binary length: %u\n", num, length);
        // hexdump(payload, length);
        debugPrint("Received binary packet of size " + String(length, DEC));

        // send message to client
        // webSocket.sendBIN(num, payload, length);
        break;
      }
    }
  });
  webSocket->begin();

  irrecv.enableIRIn();  // Start the receiver

  delay(10000);
  ADB::initShellConnection(); 
}

decode_results results;

struct Key {
  const char* bin;
  const char* value;

  Key(const char* bin_, const char* value_) : bin(bin_), value(value_) {}
};

struct Remote {
  const char* name;
  const std::vector<Key> keys;

  Remote(const char* _name, const std::vector<Key>& _keys) : name(_name), keys(_keys) {
  }
}; 

const Remote tvtuner( "tvtuner", 
  std::vector<Key> {
    Key("101000000000101010001000101000001000000000000010001010101", "n0"),
    Key("1010000000001010100010001010001000000000000000001010101010", "n1"),
    Key("1010000000001010100010001010001010001000000000000010001010101", "n2"),
    Key("10100000000010101000100010100010100010100000000000100000101010", "n3"),
    Key("10100000000010101000100010100010001000000000000010001010101010", "n4"),
    Key("1010000000001010100010001010001000001000000000001010001010", "n5"),
    Key("1010000000001010100010001010001000100010000000001000100010101", "n6"),
    Key("10100000000010101000100010100000101000000000001000001010101", "n7"),
    Key("1010000000001010100010001010000010001000000000100010001010", "n8"),
    Key("1010000000001010100010001010000010000010000000100010100010101", "n9"),

    Key("1010000000001010100010001010001010000000000000000010101", "tvfm"),
    Key("10100000000010101000100010100010101000000000000000001010101", "source"),
    Key("10100000000010101000100010100000001010100000001010000000101010", "scan"),
    Key("10100000000010101000100010100000101010100000001000000000101010", "power"),
    Key("10100000000010101000100010100010100000100000000000101000101", "recall"),
    Key("1010000000001010100010001010000000000010000000101010100010101", "plus_100"),
    Key("1010000000001010100010001010001010101010000000000000000010101", "channel_up"),
    Key("1010000000001010100010001010001010100010000000000000100010101", "channel_down"),
    Key("1010000000001010100010001010000010100010000000100000100010101", "volume_up"),
    Key("1010000000001010100010001010000000100010000000101000100010101", "volume_down"),
    Key("1010000000001010100010001010000000001010000000101010000010101", "mute"),
    Key("1010000000001010100010001010001000000010000000001010100010101", "play"),
    Key("1010000000001010100010001010000000001000000000101010001010101", "stop"),
    Key("1010000000001010100010001010000000000000000000101010101010101", "record"),
    Key("1010000000001010100010001010000010001010000000100010000010101", "freeze"),
    Key("1010000000001010100010001010001000001010000000001010000010101", "zoom"),
    Key("1010000000001010100010001010000000100000000000101000101010101", "rewind"),
    Key("1010000000001010100010001010000010101000000000100000001010101", "function"),
    Key("1010000000001010100010001010000000101000000000101000001010101", "wind"),
    Key("1010000000001010100010001010001000101000000000001000001010101", "mts"),
    Key("10100000000010101000100010100010001010100000000010000000101010", "reset"),
    Key("10100000000010101000100010100010101010000000000000000010101010", "min")
  }
);

const Remote canonCamera("CanonCamera",
  std::vector<Key> {
    Key("01010000000000010101000000010101010100000000000000000101010101010", "startstop"),
    Key("01010000000000010101000000010101000000000101000001010101000001010", "photo"),
    Key("01010000000000010101000000010101000001010100000001010000000101010", "zoomt"),
    Key("01010000000000010101000000010101010001010100000000010000000101010", "zoomw"),
    Key("01010000000000010101000000010101000101000000010001000001010100010", "func"),
    Key("01010000000000010101000000010101010001000001000000010001010001010", "menu"),
    Key("01010000000000010101000000010101000000010000010001010100010100010", "playlist"),
    Key("01010000000000010101000000010101000000000001000001010101010001010", "up"),
    Key("01010000000000010101000000010101010100000001000000000101010001010", "left"),
    Key("01010000000000010101000000010101000100000001000001000101010001010", "right"),
    Key("01010000000000010101000000010101010000000001000000010101010001010", "down"),
    Key("01010000000000010101000000010101000001000001000001010001010001010", "set"),
    Key("01010000000000010101000000010101000000000100010001010101000100010", "prev"),
    Key("01010000000000010101000000010101000000000100000001010101000101010", "next"),
    Key("01010000000000010101000000010101010100010000010000000100010100010", "rewind"),
    Key("01010000000000010101000000010101010001010000010000010000010100010", "forward"),
    Key("01010000000000010101000000010101010000000000000000010101010101010", "play"),
    Key("01010000000000010101000000010101000001000000000001010001010101010", "pause"),
    Key("01010000000000010101000000010101010101000100000000000001000101010", "stop"),
    Key("01010000000000010101000000010101000101010000010001000000010100010", "disp"),
  }
);

const Remote prologicTV("prologicTV",
  std::vector<Key> {
    // Key("00000000000000000101010101010101010001010000010000010000010100010", "power")
  }
);

const Remote* remotes[] = { 
  &tvtuner, 
  &canonCamera, 
  &prologicTV 
};

String youtubeChannels[] = {
  "http://51.15.78.31:8081/tv/rossia-24/playlist.m3u8", // Russia 24
  "http://ott-cdn.ucom.am/s68/index.m3u8", // Техно 24
  "http://51.15.78.31:8081/tv/nat-geo-wild/playlist.m3u8", // NAT GEO WILD 
  "http://51.15.56.212:8081/tv/discovery/playlist.m3u8", // Discovery
  "http://163.172.180.208:8081/tv/discovery-science/playlist.m3u8", // DISCOVERY SCIENCE
  "http://51.15.53.26:8081/tv/animal-planet/playlist.m3u8", // ANIMAL PLANET
  "http://163.172.24.228/hls/72/index.m3u8", // Моя Планета 
  "http://163.172.24.228/hls/01/index.m3u8", // Первый канал
  "http://ott-cdn.ucom.am/s72/index.m3u8", // Viasat Explorer
  "http://163.172.24.228/hls/26/index.m3u8", // Звезда
  "http://ott-cdn.ucom.am/s70/index.m3u8", // Viasat History

};
int youtubeChannelsCount = sizeof(youtubeChannels)/sizeof(youtubeChannels[0]);

void playCurrYoutubeChannel() {
  debugPrint("CHANNEL: " + String(youtubeChannel, DEC));
  ADB::executeShellCmd("killall org.videolan.vlc", [&](const String& res) {
    ADB::executeShellCmd("am start -n org.videolan.vlc/org.videolan.vlc.gui.video.VideoPlayerActivity -d \"" + youtubeChannels[youtubeChannel] + "\"", [&](const String& res) {
      persistent::stringToFile(lastChannelNum, String(youtubeChannel, DEC));
    });
  });
}

bool started = false;
int lastCanonRemoteCmd = millis();

int loopCnt = 0;
long lastMs = millis();

void loop() {
  loopCnt++;
  if (millis() - lastMs > 1000) {
    lastMs = millis();
    debugPrint("loop " + String(loopCnt, DEC) + " (" + String(system_get_free_heap_size(), DEC) + " bytes free)");
  }

  webSocket->loop();

  if (irrecv.decode(&results)) {
    if (results.rawlen > 30) {
      int decodedLen = 0;
      char decoded[300] = {0};
      String intervals("RAW: ");
      int prevL = 0;
      for (int i = 0; i < results.rawlen && i < sizeof(decoded); ++i) {
        char c = -1;
        int val = results.rawbuf[i];
        
        String valStr = String(val, DEC);
        for (;valStr.length() < 4;) valStr = " " + valStr;
        intervals += valStr;
        intervals += " ";

        if (val > 1000) {
          continue;
        } else if ((prevL + val) > 150 && (prevL + val) < 500) {
          c = '0';
        } else if ((prevL + val) > 600 && (prevL + val) < 900) {
          c = '1';
        } else {
          // Serial.print(".");
          prevL += val;
          continue; // skip!
        }
        decoded[decodedLen++] = c;
        prevL = 0;
      }

      String decodedStr(decoded);
      const Remote* recognizedRemote = NULL;
      const Key* recognized = NULL;
      int kk = 0;
      for (int r = 0; r < (sizeof(remotes)/sizeof(remotes[0])); ++r) {
        const Remote& remote = *(remotes[r]);
        for (int k = 0; k < remote.keys.size(); ++k) {
          if (decodedStr.indexOf(remote.keys[k].bin) != -1) {
            // Key pressed!
            recognized = &(remote.keys[k]);
            recognizedRemote = &remote;
            kk = k;

            String toSend = "{ \"remote\": \"" + 
              String(recognizedRemote->name) + "\", \"key\": "  + String(remote.keys[k].value) + 
              "\" }";

            webSocket->broadcastTXT(toSend.c_str(), toSend.length());
            debugPrint(toSend);
    
            break;
          }
        }
      }

      // webSocket->broadcastTXT(intervals.c_str(), intervals.length());
      // Serial.println(intervals);

      if (recognized == NULL) {
        debugPrint(intervals);
        debugPrint(decoded);
      } else if (recognizedRemote == &canonCamera) {
        if (millis() - lastCanonRemoteCmd > 900) {
          lastCanonRemoteCmd = millis();
          if (recognized->value == "startstop") {
            ADB::executeShellCmd("input keyevent KEYCODE_POWER", [&](const String& res) {
            });
          } else if (recognized->value == "up" || recognized->value == "zoomt") {
            ADB::executeShellCmd("input keyevent KEYCODE_VOLUME_UP", [&](const String& res) {

            });
          } else if (recognized->value == "down" || recognized->value == "zoomw") {
            ADB::executeShellCmd("input keyevent KEYCODE_VOLUME_DOWN", [&](const String& res) {

            });
          } else if (recognized->value == "photo") {
            ADB::executeShellCmd("reboot", [&](const String& res) {
            });
          } else if (recognized->value == "left") {
            youtubeChannel = (youtubeChannelsCount + youtubeChannel - 1) % youtubeChannelsCount;
            playCurrYoutubeChannel();
          } else if (recognized->value == "right") {
            youtubeChannel = (youtubeChannel + 1) % youtubeChannelsCount;
            playCurrYoutubeChannel();
          } else if (recognized->value == "set") {
            playCurrYoutubeChannel();
          } else if (recognized->value == "rewind") {
            youtubeChannel = 0;
            playCurrYoutubeChannel();
          } else if (recognized->value == "forward") {
            youtubeChannel = 1;
            playCurrYoutubeChannel();
          } else if (recognized->value == "play") {
            youtubeChannel = 2;
            playCurrYoutubeChannel();
          } else if (recognized->value == "pause") {
            youtubeChannel = 3;
            playCurrYoutubeChannel();
          } else if (recognized->value == "stop") {
            youtubeChannel = 4;
            playCurrYoutubeChannel();
          } else if (recognized->value == "disp") {
            youtubeChannel = 5;
            playCurrYoutubeChannel();
          }

        }
      }
    }
   
    irrecv.resume();  // Receive the next value
  }
}


