// nodeflash standalone gateway (ESP32-S3): USB serial <-> RS-485 bus.
// Everything is in the nodeflash library; this is all a program needs to
// let the nodeflash PC tool scan and update nodes. See the library header
// (NodeflashHost.h) for adding the same to your own ESP32 program.
#include <Arduino.h>
#include <NodeflashHost.h>

#ifndef GW_TX_PIN
#define GW_TX_PIN 17  // to the RS-485 transceiver's DI
#endif
#ifndef GW_RX_PIN
#define GW_RX_PIN 18  // from RO
#endif
#ifndef GW_DE_PIN
#define GW_DE_PIN 4   // to DE + /RE
#endif

nodeflash::EspUartTransport bus;
nodeflash::Host host(bus);
nodeflash::Console console(host, Serial);

void setup() {
  Serial.setRxBufferSize(16384);  // room for image bytes arriving from the PC
  Serial.begin(460800);
  bus.begin(UART_NUM_1, GW_TX_PIN, GW_RX_PIN, GW_DE_PIN);
  delay(200);
  console.banner();
}

void loop() { console.poll(); }
