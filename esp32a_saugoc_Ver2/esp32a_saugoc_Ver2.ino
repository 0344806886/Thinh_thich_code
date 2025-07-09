// ===============================
// ESP32 A - Giao tiếp ESP B & STM32 với lệnh 12 ký tự cố định
// - Đọc QR từ CF26 qua UART2 (GPIO16,17)
// - Gửi QR lên ESP B
// - Nhận lệnh từ ESP B và gửi điều khiển xuống STM32 qua UART1
// ===============================

#include <WiFi.h>
#include <esp_now.h>
#include <HardwareSerial.h>

char qrCode[32];                // Dữ liệu đọc từ QR
char lastQRcode[13] = "";       // Lưu QR cuối cùng đã đọc (tối đa 12 ký tự)
uint8_t qrIndex = 0;

uint8_t espB_mac[] = {0xEC, 0xE3, 0x34, 0xDB, 0xB5, 0xDC};  // MAC ESP B

typedef struct {
  char message[13];  // Lệnh 12 ký tự + null để an toàn khi in chuỗi
} AGVMessage;

AGVMessage msgToSend, msgReceived;

// Gửi dữ liệu sang ESP B
void sendQRtoESP_B(const char* data12) {
  memcpy(msgToSend.message, data12, 12);
  esp_now_send(espB_mac, (uint8_t*)msgToSend.message, 12);
  Serial.print("[ESP->B] Gửi: ");
  for (int i = 0; i < 12; i++) Serial.print(msgToSend.message[i]);
  Serial.println();
}

// Gửi phản hồi L/U về ESP B dựa trên lastQRcode
void sendAGVStatusToACS(const char status) {
  char buffer[13];
  snprintf(buffer, sizeof(buffer), "001A%c00%sx", status, lastQRcode);
  sendQRtoESP_B(buffer);

  Serial.print("[ESP->B] Gửi phản hồi AGV: ");
  Serial.println(buffer);  // ← THÊM DÒNG NÀY
}


// Đọc QR từ CF26
void readQRfromCF26() {
  while (Serial1.available()) {
    char c = Serial1.read();

    if (c == '\r' || c == '\n') {
      if (qrIndex > 0) {
        qrCode[qrIndex] = '\0';

        strncpy(lastQRcode, qrCode, 12);  // lưu lại để gửi L/U
        lastQRcode[12] = '\0';

        // Gửi QR lên ESP B
        char qrmsg[13];
        snprintf(qrmsg, sizeof(qrmsg), "001AT00%sx", qrCode);
        sendQRtoESP_B(qrmsg);

        // Gửi QR xuống STM32
        Serial2.println(qrCode);
        Serial.print("[ESP->STM32] QR: "); Serial.println(qrCode);

        qrIndex = 0;
      }
    } else if (isPrintable(c) && qrIndex < sizeof(qrCode) - 1) {
      qrCode[qrIndex++] = c;
    }
  }
}

// Nhận lệnh từ ESP B
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != 12) return;

  memcpy(&msgReceived, data, 12);
  msgReceived.message[12] = '\0';  // Đảm bảo an toàn khi in chuỗi

  Serial.print("[ESP<-B] Nhận lệnh: ");
  Serial.println(msgReceived.message);

  char cmd = msgReceived.message[4];
  char subcode[3] = {msgReceived.message[5], msgReceived.message[6], '\0'};
  char code[5] = {msgReceived.message[7], msgReceived.message[8], msgReceived.message[9], msgReceived.message[10], '\0'};

  delay(50);  // tránh nhiễu

  if (cmd == 'O') {
    if (strcmp(subcode, "00") == 0) Serial2.write('S');
    else if (strcmp(subcode, "01") == 0) Serial2.write('L');
    else if (strcmp(subcode, "02") == 0) Serial2.write('R');
    else if (strcmp(subcode, "03") == 0) Serial2.write('F');
    else if (strcmp(subcode, "04") == 0) Serial2.write('C');
    else if (strcmp(subcode, "07") == 0) Serial2.write('B');
    else Serial2.write('X');

    Serial2.print(code);
    Serial2.write('\n');

    Serial.print("[ESP->STM32] CMD: "); Serial.print(cmd);
    Serial.print(" "); Serial.println(code);

  } else if (cmd == 'C') {
    if (strcmp(subcode, "01") == 0) {
      Serial2.write('h'); // get goods
      Serial2.print(code);
      Serial2.write('\n');
    } else if (strcmp(subcode, "02") == 0) {
      Serial2.write('k');  // tra goods
      Serial2.print(code);
      Serial2.write('\n');
    } else {
      Serial2.write('X');
    }
  }
}

// Nhận phản hồi L/U từ STM32
void checkSTM32Response() {
  static String line = "";

  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\r' || c == '\n') {
      if (line == "L" || line == "U") {
        Serial.print("[STM32->ESP] Phản hồi: ");
        Serial.println(line);
        sendAGVStatusToACS(line.charAt(0));
      } else {
        Serial.print("[STM32->ESP] Dữ liệu không hợp lệ: ");
        Serial.println(line);
      }
      line = "";  // Xoá buffer sau khi xử lý xong
    }
    else if (isPrintable(c)) {
      line += c;
    }
  }
}


void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 22, 23);  // UART1 - CF26 QR Reader
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // UART2 - STM32

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init lỗi");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, espB_mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ESP A sẵn sàng");
}

void loop() {
  readQRfromCF26();
  checkSTM32Response();
  delay(1);
}
