#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

// ======================================================
// DFPLAYER UART
//
// ESP32 GPIO10 <- DFPlayer TX
// ESP32 GPIO9  -> DFPlayer RX
// ======================================================
#define DFPLAYER_RX 10
#define DFPLAYER_TX 9

// ======================================================
// xG26 UART
//
// Day that su dang noi vao 2 chan "TX"/"RX" rieng (goc board, canh dau
// nap USB-C) = UART0 vat ly cua ESP32-S3 = GPIO43 (TX) / GPIO44 (RX).
// Truoc day "Serial" (debug console) cung dung chinh UART0 nay khi build
// voi CDCOnBoot=Disabled - do la ly do co nhieu/log lan vao duong day nay.
// Da chuyen "Serial" sang USB natif (CDCOnBoot=cdc) nen UART0 gio rang
// hoan toan cho xg26Serial dung rieng, khong con xung dot.
//
// xG26 TX / PA04 -> ESP32 RX (GPIO44)
// xG26 RX / PA05 <- ESP32 TX (GPIO43)
// ======================================================
#define XG26_RX 44
#define XG26_TX 43

// ======================================================
// CONFIG
// ======================================================

// Khoang nghi giua cac chu so
#define GAP_BETWEEN_DIGITS 0

// Toi da 10 chu so
#define MAX_NUMBER_LENGTH 10

// ======================================================
// UART
// ======================================================

// UART1 -> DFPlayer
HardwareSerial dfSerial(1);

// UART2 -> xG26
HardwareSerial xg26Serial(2);

DFRobotDFPlayerMini dfPlayer;

// DFPlayer that failed to init should not take down the xG26 UART bridge
// (used for the two-way UART debug/diagnostic phase).
bool dfPlayerReady = false;

// ======================================================
// BIEN DOC SO
// ======================================================

String numberQueue = "";

int currentIndex = 0;

bool isPlayingSequence = false;

// Buffer UART xG26
String xg26Buffer = "";

// Buffer raw byte hex dump UART xG26 (bao gom ca \n ket thuc dong)
String xg26RawBuffer = "";

// Buffer USB Serial
String usbBuffer = "";

// ======================================================
// DEBUG: gui READY dinh ky (non-blocking) de xG26 chac chan
// nhan duoc du boot truoc hay sau xG26
// ======================================================
#define READY_INTERVAL_MS 2000
unsigned long lastReadyMs = 0;


// ======================================================
// GUI DU LIEU VE xG26
// ======================================================
void sendToXG26(const char *message) {

  xg26Serial.print(message);
  xg26Serial.print('\n');

  Serial.print("[ESP32 -> xG26] ");
  Serial.println(message);
}


// ======================================================
// CHUYEN CHU SO -> FILE
//
// /01/001.wav = 1
// /01/002.wav = 2
// ...
// /01/009.wav = 9
// /01/010.wav = 0
// ======================================================
int digitToFileNumber(char digit) {

  if (digit >= '1' && digit <= '9') {
    return digit - '0';
  }

  if (digit == '0') {
    return 10;
  }

  return -1;
}


// ======================================================
// KIEM TRA CHUOI SO
// ======================================================
bool isAllDigits(const String &text) {

  if (text.length() == 0) {
    return false;
  }

  for (int i = 0; i < text.length(); i++) {

    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }

  return true;
}


// ======================================================
// PHAT 1 CHU SO
// ======================================================
void playDigit(char digit) {

  if (!dfPlayerReady) {

    Serial.println("[WARN] DFPlayer chua san sang, bo qua phat am");

    sendToXG26("ERROR");

    return;
  }

  int fileNumber = digitToFileNumber(digit);

  if (fileNumber < 0) {

    Serial.print("Ky tu khong hop le: ");
    Serial.println(digit);

    sendToXG26("ERROR");

    return;
  }

  Serial.print("Dang doc so: ");
  Serial.println(digit);

  Serial.print("File: /01/");

  if (fileNumber < 10) {
    Serial.print("00");
  }
  else {
    Serial.print("0");
  }

  Serial.print(fileNumber);
  Serial.println(".wav");

  dfPlayer.playFolder(1, fileNumber);
}


// ======================================================
// PHAT SO TIEP THEO
// ======================================================
void playNextDigit() {

  if (currentIndex >= numberQueue.length()) {

    Serial.println();
    Serial.println("==========================");
    Serial.println("DA DOC XONG");
    Serial.println("==========================");
    Serial.println();

    numberQueue = "";
    currentIndex = 0;
    isPlayingSequence = false;

    // Bao xG26 da doc xong
    sendToXG26("DONE");

    return;
  }

  char digit = numberQueue.charAt(currentIndex);

  currentIndex++;

  playDigit(digit);
}


// ======================================================
// BAT DAU DOC CHUOI
// ======================================================
bool startNumberSequence(String input, bool fromXG26) {

  input.trim();

  // Rong
  if (input.length() == 0) {

    if (fromXG26) {
      sendToXG26("ERROR");
    }

    return false;
  }

  // Qua 10 so
  if (input.length() > MAX_NUMBER_LENGTH) {

    Serial.println("LOI: Toi da 10 chu so");

    if (fromXG26) {
      sendToXG26("ERROR");
    }

    return false;
  }

  // Khong phai chuoi so
  if (!isAllDigits(input)) {

    Serial.println("LOI: Chi duoc nhap 0-9");

    if (fromXG26) {
      sendToXG26("ERROR");
    }

    return false;
  }

  // Neu dang doc chuoi cu
  if (isPlayingSequence) {

    Serial.println("Dung chuoi cu...");

    dfPlayer.stop();

    numberQueue = "";
    currentIndex = 0;
    isPlayingSequence = false;
  }

  numberQueue = input;

  currentIndex = 0;

  isPlayingSequence = true;

  Serial.println();
  Serial.println("==========================");

  if (fromXG26) {
    Serial.print("[xG26] NHAN SO: ");
  }
  else {
    Serial.print("[USB] NHAN SO: ");
  }

  Serial.println(numberQueue);

  Serial.println("==========================");

  // Bao da nhan duoc lenh
  if (fromXG26) {
    sendToXG26("ACK");
  }

  // Bat dau phat
  playNextDigit();

  return true;
}


// ======================================================
// DFPLAYER ERROR
// ======================================================
void printDFPlayerError(int value) {

  Serial.print("DFPlayer ERROR: ");

  switch (value) {

    case Busy:
      Serial.println("The SD khong san sang");
      break;

    case Sleeping:
      Serial.println("DFPlayer dang sleep");
      break;

    case SerialWrongStack:
      Serial.println("Loi giao tiep UART");
      break;

    case CheckSumNotMatch:
      Serial.println("Sai checksum");
      break;

    case FileIndexOut:
      Serial.println("File vuot pham vi");
      break;

    case FileMismatch:
      Serial.println("Khong tim thay file");
      break;

    case Advertise:
      Serial.println("Advertise");
      break;

    default:
      Serial.println(value);
      break;
  }
}


// ======================================================
// NHAN LENH USB
//
// Van cho phep go 123 truc tiep tren Serial Monitor
// de test ESP32 + DFPlayer rieng.
// ======================================================
void handleUsbSerial() {

  while (Serial.available()) {

    char c = (char)Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {

      if (usbBuffer.length() > 0) {

        String input = usbBuffer;

        usbBuffer = "";

        startNumberSequence(input, false);
      }

      continue;
    }

    if (usbBuffer.length() < 32) {
      usbBuffer += c;
    }
    else {

      usbBuffer = "";

      Serial.println("USB buffer overflow");
    }
  }
}


// ======================================================
// LOC RAC UART (chong nhieu EMI tu Zigbee/HX711 tren board xG26)
//
// Day PA04/PA05 doi khi bi nhieu dinh vao dau dong (vai byte la),
// khien ca dong bi tu choi du so that van nguyen ven o cuoi. Ham nay
// chi giu lai ky tu so 0-9, bo qua moi byte khac truoc khi kiem tra
// hop le - khong lam thay doi logic UART/parity/framing, chi loc noi
// dung sau khi da nhan day du 1 dong.
// ======================================================
String filterDigitsOnly(const String &raw) {

  String digitsOnly = "";

  for (unsigned int i = 0; i < raw.length(); i++) {

    char c = raw.charAt(i);

    if (c >= '0' && c <= '9') {
      digitsOnly += c;
    }
  }

  return digitsOnly;
}


// ======================================================
// NHAN UART TU xG26
//
// xG26 gui:
// 123\n
//
// ESP32 se:
// ACK
// doc 1 2 3
// DONE
// ======================================================
void handleXG26Serial() {

  while (xg26Serial.available()) {

    char c = (char)xg26Serial.read();

    // Luu moi byte tho (ke ca \n) de debug hex, khong phu thuoc \r/\n
    if (xg26RawBuffer.length() < 64) {
      xg26RawBuffer += c;
    }

    // Bo CR
    if (c == '\r') {
      continue;
    }

    // Het dong
    if (c == '\n') {

      if (xg26Buffer.length() > 0) {

        String rawInput = xg26Buffer;

        xg26Buffer = "";

        Serial.println();
        Serial.print("[xG26 RX ASCII] ");
        Serial.println(rawInput);

        Serial.print("[xG26 RX HEX] ");
        for (unsigned int i = 0; i < xg26RawBuffer.length(); i++) {
          uint8_t b = (uint8_t)xg26RawBuffer[i];
          if (b < 0x10) {
            Serial.print("0");
          }
          Serial.print(b, HEX);
          Serial.print(" ");
        }
        Serial.println();

        // Loc bo byte rac (nhieu EMI) truoc khi xu ly, chi giu lai 0-9
        String input = filterDigitsOnly(rawInput);

        if (input != rawInput) {
          Serial.print("[xG26] Da loc rac, con lai: ");
          Serial.println(input);
        }

        startNumberSequence(input, true);
      }

      xg26RawBuffer = "";
      continue;
    }

    // Them vao buffer
    if (xg26Buffer.length() < 32) {

      xg26Buffer += c;
    }
    else {

      Serial.println("[xG26] Buffer overflow");

      xg26Buffer = "";
      xg26RawBuffer = "";

      sendToXG26("ERROR");
    }
  }
}


// ======================================================
// XU LY DFPLAYER
// ======================================================
void handleDFPlayer() {

  if (!dfPlayerReady) {
    return;
  }

  if (!dfPlayer.available()) {
    return;
  }

  uint8_t type = dfPlayer.readType();

  int value = dfPlayer.read();

  // File da phat xong
  if (type == DFPlayerPlayFinished) {

    Serial.print("Phat xong file: ");
    Serial.println(value);

    if (isPlayingSequence) {

      if (GAP_BETWEEN_DIGITS > 0) {
        delay(GAP_BETWEEN_DIGITS);
      }

      playNextDigit();
    }
  }

  // Loi
  else if (type == DFPlayerError) {

    printDFPlayerError(value);

    numberQueue = "";
    currentIndex = 0;
    isPlayingSequence = false;

    sendToXG26("ERROR");
  }
}


// ======================================================
// SETUP
// ======================================================
void setup() {

  // ==================================================
  // USB SERIAL
  // ==================================================
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("==========================");
  Serial.println("ESP32 SMART IV AUDIO");
  Serial.println("==========================");


  // ==================================================
  // DFPLAYER UART
  //
  // GPIO10 <- DFPlayer TX
  // GPIO9  -> DFPlayer RX
  // ==================================================
  dfSerial.begin(
    9600,
    SERIAL_8N1,
    DFPLAYER_RX,
    DFPLAYER_TX
  );

  delay(1000);


  // ==================================================
  // xG26 UART
  //
  // GPIO44 (RX) <- xG26 TX
  // GPIO43 (TX) -> xG26 RX
  // ==================================================
  xg26Serial.begin(
    115200,
    SERIAL_8N1,
    XG26_RX,
    XG26_TX
  );


  Serial.println();
  Serial.println("UART xG26:");

  Serial.print("RX GPIO = ");
  Serial.println(XG26_RX);

  Serial.print("TX GPIO = ");
  Serial.println(XG26_TX);

  Serial.println("Baud = 115200");


  // ==================================================
  // KHOI DONG DFPLAYER
  // ==================================================
  Serial.println();
  Serial.println("==========================");
  Serial.println("KHOI DONG DFPLAYER");
  Serial.println("==========================");


  if (!dfPlayer.begin(dfSerial)) {

    Serial.println("Khong ket noi duoc DFPlayer!");

    Serial.println();
    Serial.println("Kiem tra:");

    Serial.println("GPIO10 <- DFPlayer TX");
    Serial.println("GPIO9  -> DFPlayer RX");

    Serial.println("GND chung");
    Serial.println("Nguon DFPlayer");
    Serial.println("The SD");

    Serial.println();
    Serial.println("DFPlayer KHONG san sang - van tiep tuc chay de");
    Serial.println("giu UART xG26 <-> ESP32 hoat dong (che do debug).");

    sendToXG26("ERROR");

    dfPlayerReady = false;

  } else {

    dfPlayerReady = true;

    Serial.println("DFPlayer OK!");

    // SD
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);

    delay(500);

    // Volume MAX
    dfPlayer.volume(30);

    delay(1500);
  }


  // ==================================================
  // READY
  // ==================================================
  Serial.println();
  Serial.println("==========================");
  Serial.println("HE THONG SAN SANG");
  Serial.println("==========================");

  Serial.println();
  Serial.println("DFPlayer:");
  Serial.println("GPIO10 <- TX");
  Serial.println("GPIO9  -> RX");

  Serial.println();
  Serial.println("xG26:");
  Serial.println("GPIO44 (RX) <- xG26 TX / PA04");
  Serial.println("GPIO43 (TX) -> xG26 RX / PA05");

  Serial.println();
  Serial.println("Test USB:");
  Serial.println("123");

  Serial.println();
  Serial.println("Test xG26:");
  Serial.println("voice 123");

  Serial.println();

  // Bao xG26 da san sang
  sendToXG26("READY");

  lastReadyMs = millis();
}


// ======================================================
// LOOP
// ======================================================
void loop() {

  // USB -> ESP32
  handleUsbSerial();

  // xG26 <-> ESP32
  handleXG26Serial();

  // DFPlayer
  handleDFPlayer();

  // DEBUG: gui READY dinh ky de xac nhan duong ESP32 -> xG26 song,
  // khong phu thuoc thu tu boot cua hai board. Non-blocking (millis()).
  unsigned long now = millis();
  if (now - lastReadyMs >= READY_INTERVAL_MS) {
    lastReadyMs = now;
    sendToXG26("READY");
  }
}