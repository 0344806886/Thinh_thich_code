#include <WiFi.h>
#include <esp_now.h>

#define UART_ACS_RX 16
#define UART_ACS_TX 17
#define BAUD_RATE 9600

// MAC ESP A
uint8_t espA_mac[] = {0xF0, 0x24, 0xF9, 0x45, 0xF0, 0xC4};

char acsBuffer[13];   // 12 ký tự + '\0'
uint8_t acsIndex = 0;

typedef struct {
  char message[64];
} AGVMessage;

AGVMessage msgToSend;

// Xoá dấu cách trong chuỗi
void removeSpaces(const char* input, char* output) {
  int j = 0;
  for (int i = 0; input[i] != '\0'; ++i) {
    if (input[i] != ' ') output[j++] = input[i];
  }
  output[j] = '\0';
}

// Nhận dữ liệu từ ESP A (QR / phản hồi AGV)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.print("[ESP A → ESP B] Dữ liệu: ");
  Serial.write(data, len);
  Serial.println();

  // Chỉ lấy tối đa 12 ký tự
  int copyLen = min(len, 12);

  char cleaned[13];  // 12 ký tự + null
  memcpy(cleaned, data, copyLen);
  cleaned[copyLen] = '\0';

  // Loại bỏ dấu cách nếu có
  char noSpace[13];
  removeSpaces(cleaned, noSpace);

  Serial2.print(noSpace);
  Serial2.print('\n');
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(BAUD_RATE, SERIAL_8N1, UART_ACS_RX, UART_ACS_TX);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init lỗi");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, espA_mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Thêm peer ESP A thất bại");
  }

  Serial.println("ESP B đã sẵn sàng");
  Serial.println("Gõ lệnh ACS qua Serial Monitor (ví dụ: O 03 1234, C 01 1234)");
}

void loop() {
  // Nhận lệnh từ Serial Monitor (ACS giả)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("O ")) {
      int sp1 = cmd.indexOf(' ');
      int sp2 = cmd.indexOf(' ', sp1 + 1);
      String subcode = cmd.substring(sp1 + 1, sp2 == -1 ? cmd.length() : sp2);
      String code = (sp2 != -1) ? cmd.substring(sp2 + 1) : "0000";
      snprintf(msgToSend.message, sizeof(msgToSend.message), "001AO%s%sx", subcode.c_str(), code.c_str());
    }
    else if (cmd.startsWith("C ")) {
      int sp1 = cmd.indexOf(' ');
      int sp2 = cmd.indexOf(' ', sp1 + 1);
      if (sp2 != -1) {
        String subcode = cmd.substring(sp1 + 1, sp2);
        String code = cmd.substring(sp2 + 1);
        snprintf(msgToSend.message, sizeof(msgToSend.message), "001AC%s%sx", subcode.c_str(), code.c_str());
      } else {
        Serial.println("⚠️ Cần nhập C <subcode> <code>");
        return;
      }
    }
    else {
      Serial.println("⚠️ Lệnh không hợp lệ. Dùng O <subcode> <code> hoặc C <subcode> <code>");
      return;
    }

    Serial.print("[ACS giả → ESP A] Gửi: ");
    Serial.println(msgToSend.message);
    esp_now_send(espA_mac, (uint8_t*)msgToSend.message, 12);  // ✅ Gửi đúng 12 ký tự
  }

  // Giao tiếp từ ACS thật (UART2)
  if (Serial2.available() >= 12) {
    int bytesRead = Serial2.readBytes(acsBuffer, 12);
    acsBuffer[12] = '\0';

    if (acsBuffer[11] == 'x') {
      Serial.print("[ACS thực → ESP A] Gửi: ");
      Serial.println(acsBuffer);
      esp_now_send(espA_mac, (uint8_t*)acsBuffer, 12);
    } else {
      Serial.print("⚠️ Bỏ lệnh: Không kết thúc bằng 'x' → ");
      for (int i = 0; i < 12; i++) {
        if (isPrintable(acsBuffer[i]))
          Serial.print(acsBuffer[i]);
        else {
          Serial.print("\\x");
          Serial.print((uint8_t)acsBuffer[i], HEX);
        }
      }
      Serial.println();
    }

    // Flush buffer
    while (Serial2.available()) Serial2.read();
  }

  delay(1);
}
