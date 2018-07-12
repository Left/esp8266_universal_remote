#include <Arduino.h>

namespace ADB {
  class Message;

  const uint32_t SYNC = 0x434e5953;
  const uint32_t CNXN = 0x4e584e43;
  const uint32_t AUTH = 0x48545541;
  const uint32_t OPEN = 0x4e45504f;
  const uint32_t OKAY = 0x59414b4f;
  const uint32_t CLSE = 0x45534c43;
  const uint32_t WRTE = 0x45545257;
  const uint32_t COMMANDS[] = { SYNC, CNXN, AUTH, OPEN, OKAY, CLSE, WRTE };

  String charToPrint(uint8_t b) {
    String byt(b, HEX);
    if (byt.length() == 1) {
      byt = "0" + byt;
    }
    byt.toUpperCase();
    return byt;
  }

  String msgToStr(Message* msg);

  String payloadToStr(const char* ptr, int sz) {
    String res = "";
    for (int i = 0; i < sz; ++i) {
      if (i > 0) {
        res += " ";
      }
      res += charToPrint(ptr[i]);
    }
    res += " -- ";

    for (int i = 0; i < sz; ++i) {
      char c = ptr[i];
      if (c >= ' ' && c < 128) {
        res += c;
      } else {
        res += ".";
      }
    }
    return res;
  }

  struct Message {
    uint32_t command;       /* command identifier constant, A_SYNC etc. */
    uint32_t arg0;          /* first argument                   */
    uint32_t arg1;          /* second argument                  */
    uint32_t data_length;   /* length of payload (0 is allowed) */
    uint32_t data_check;    /* checksum of data payload, crc32  */
    uint32_t magic;         /* command ^ 0xffffffff             */

    Message() {
    }

    Message(uint32_t cmd, uint32_t arg0, uint32_t arg1) {
      // memset(msg, 0, totalL);
      this->command = cmd;
      this->arg0 = arg0;
      this->arg1 = arg1;
      this->magic = this->command ^ 0xffffffff;
    };

    void send(AsyncClient* client, const uint8_t* payload = NULL, int payloadL = 0) {
      this->data_length = payloadL;
      this->data_check = 0;
      for (int j = 0; j < payloadL; ++j) {
        this->data_check += payload[j];
      }

      String sent("SENT: ");
      // Serial.print("SENT: ");
      client->write((const char*)this, sizeof(ADB::Message));
      sent += msgToStr(this);

      if (payloadL > 0) {
        sent += " ";
        client->write((const char*) payload, payloadL);
        // sent += payloadToStr((const char*) payload, payloadL);
        sent += String(payloadL, DEC) + " bytes of payload";
      }

      debugPrint(sent);
    }
  };

  std::auto_ptr<AsyncClient> adbTcpClient;
  std::vector<char> receiveBuf;
  std::vector<char> response;

  void initShellConnection() {
  }

  static void onDisconnect(std::function<void(const String& result)> handler) {
        Serial.println("onDisconnect");
        debugPrint("Disconnected");

        receiveBuf.resize(0);
        adbTcpClient.reset(NULL);

        // We've done
        response.push_back(0);
        handler(String((const char*) &(response[0])));
        response.resize(0);
        Serial.println("onDisconnect OK!");
  }

  static void executeShellCmd(String shellCmd, std::function<void(const String& result)> handler) {
    if (adbTcpClient.get()) {
      debugPrint("executeShellCmd IS ALREADY IN PROGRESS");
      return;
    }

    debugPrint("executeShellCmd");

    adbTcpClient.reset(new AsyncClient());
    adbTcpClient->onError([=](void * arg, AsyncClient* client, int error) {
      debugPrint("Connection Error");
      adbTcpClient.reset(NULL);
    });

    adbTcpClient->onConnect([=](void * arg, AsyncClient* client) {
      debugPrint("Connected");
      adbTcpClient->onDisconnect([=](void * arg, AsyncClient * c){
        onDisconnect(handler);
      });

      adbTcpClient->onData([=](void* arg, AsyncClient* c, void* data, size_t len) {
        debugPrint("GOT WS " + String(len, DEC) + " bytes");

        if (len > 0) {
          receiveBuf.insert(receiveBuf.end(), (const char*)data, (const char*)data + len);
          if (receiveBuf.size() >= sizeof(Message)) {
            Message* msg = (Message*) (&receiveBuf[0]);
            const char* payload = (&receiveBuf[sizeof(Message)]);

            debugPrint("RECEIVED " + msgToStr(msg)); // + " " + 
                // payloadToStr(payload, msg->data_length));

            receiveBuf.erase(receiveBuf.begin(), receiveBuf.begin() + sizeof(Message) + msg->data_length);

            if (msg->command == ADB::CNXN) {
              String str = String("shell: ") + shellCmd + " \x00";

              ADB::Message msgOpen(ADB::OPEN, 2, msg->arg0);
              msgOpen.send(adbTcpClient.get(), (const uint8_t*)str.c_str(), str.length());

            } else if (msg->command == ADB::OKAY) {
            } else if (msg->command == ADB::WRTE) {
              response.insert(response.end(), payload, payload + msg->data_length);

              ADB::Message msgOpen(ADB::OKAY, 2, msg->arg0);
              msgOpen.send(adbTcpClient.get());
            } else if (msg->command == ADB::CLSE) {
              response.push_back(0);
              debugPrint(String((const char*) &(response[0])));
              adbTcpClient->close();
            }
          }
        }
      });

      String str = "host::esp8266";
      ADB::Message msg(ADB::CNXN, 
        0x01000000, // A_VERSION 
        4096 // MAX_PAYLOAD
      );
      msg.send(adbTcpClient.get(), (const uint8_t*)str.c_str(), str.length());  
    });
    adbTcpClient->connect("192.168.121.166", 5556);
  }

  String msgToStr(Message* msg) {
    char ww[5] = {0};
    memcpy(ww, &msg->command, 4);

    return "MSG: " + 
                String(ww) + ", arg0=" + 
                msg->arg0 + ", arg1=" + 
                msg->arg1;
  }
}
