
namespace ADB {
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

    static void waitToReceive(Client& client, std::function<void(const Message& msg, const uint8_t* payload, int payloadL)> handler) {
      // Serial.println("Waiting for " + String(sizeof(ADB::Message), DEC) + " bytes");
      for (;client.available() < sizeof(ADB::Message);) {
        delay(1);
      }

      // Serial.println("Reading");

      Message msg;
      client.read((uint8_t*)&msg, sizeof(Message));

      // Serial.println("Read");

      char ww[5] = {0};
      memcpy(ww, &msg.command, 4);
      // Serial.println("RECEIVED MSG: (" + String(ww) + ", " + msg.arg0 + ", " + msg.arg1 + " + " + msg.data_length + " bytes of payload)");

      if (msg.data_length > 0) {
        while(client.available() < msg.data_length) {
          delay(10);
        }
      }

      uint8_t* payload = (uint8_t*)malloc(msg.data_length + 1);
      payload[msg.data_length] = 0; // Let us to treat it as char*
      client.read(payload, msg.data_length);

      String trace;
      // Serial.print("PAYLOAD: ");
      for (int i = 0; i < msg.data_length; ++i) {
        // Serial.print(charToPrint(payload[i]));
        trace += String((char) payload[i]);
        // Serial.print(" ");
      }
      // Serial.println("");
      // Serial.println(trace);

      handler(msg, payload, msg.data_length);
      free(payload);        
    }

    void send(Client& client, const uint8_t* payload = NULL, int payloadL = 0) {
      this->data_length = payloadL;
      this->data_check = 0;
      for (int j = 0; j < payloadL; ++j) {
        this->data_check += payload[j];
      }

      // Serial.print("SENT: ");
      client.write((const uint8_t *)this, sizeof(ADB::Message));
      for (int i = 0; i < sizeof(ADB::Message); ++i) {
        // Serial.print(charToPrint(((const uint8_t *)this)[i]));
        // Serial.print(" ");
      }

      if (payloadL > 0) {
        client.write(payload, payloadL);
        for (int i = 0; i < payloadL; ++i) {
          // Serial.print(charToPrint(payload[i]));
          // Serial.print(" ");
        }
      }
      client.flush();

      // Serial.println();
    }
  };

  static void executeShellCmd(Client& fireClient, const String& cmd, std::function<void(const String& result)> handler) {
    fireClient.stop();
    fireClient.connect("192.168.121.166", 5556);

    // Serial.println("Sending startstop");
    String str = "host::esp8266";
    ADB::Message msg(ADB::CNXN, 
      0x01000000, // A_VERSION 
      4096 // MAX_PAYLOAD
    );
    msg.send(fireClient, (const uint8_t *)str.c_str(), str.length());  
    ADB::Message::waitToReceive(fireClient, [&](const ADB::Message& msg, const uint8_t* payload, int payloadL) {
      // Serial.print("Sending CMD: ");
        // String str = "shell:input keyevent 24 \x00";
        // String str = "shell:am start -a android.intent.action.VIEW -d \"http://www.youtube.com/watch?v=K59KKnIbIaM\" \x00";
        // String str = "shell:am start -n org.videolan.vlc/org.videolan.vlc.gui.video.VideoPlayerActivity -d \"https://www.youtube.com/watch?v=K59KKnIbIaM&fmt=18\" \x00";
        String str = "shell: " + cmd + " \x00";
        
        ADB::Message msgOpen(ADB::OPEN, 2, msg.arg0);
        msgOpen.send(fireClient, (uint8_t*)str.c_str(), str.length());

        ADB::Message::waitToReceive(fireClient, [&](const ADB::Message& msg, const uint8_t* payload, int payloadL) {
          if (msg.command == ADB::OKAY) {
            String res;
            for (bool close = false; !close; ) {
              ADB::Message::waitToReceive(fireClient, [&](const ADB::Message& msg, const uint8_t* payload, int payloadL) {
                if (msg.command == ADB::WRTE) {
                  res += String((char*)payload);
                } else if (msg.command == ADB::CLSE) {
                  ADB::Message msgOpen(ADB::CLSE, 2, msg.arg0);
                  msgOpen.send(fireClient);

                  close = true;    
                }
              });
            }
            fireClient.stop();

            // Serial.println("=====================");
            // Serial.println(res);
            // Serial.println("=====================");
            handler(res);
          }
        });
    });
  }
}
