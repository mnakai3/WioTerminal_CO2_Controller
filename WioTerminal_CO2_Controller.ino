#include <GroveDriverPack.h>
GroveBoard board;
GroveSCD30 scd30(&board.GroveI2C1);

#include <rpcWiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
WiFiClient client;
WiFiUDP    udp;

const char* ssid = "<YOUR NETWORK SSID>";
const char* password = "<YOUR NETWORK PASSWORD>";

#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprCO2 = TFT_eSprite(&tft);
TFT_eSprite sprTemperature = TFT_eSprite(&tft);
TFT_eSprite sprHumidity = TFT_eSprite(&tft);
TFT_eSprite sprSwitch = TFT_eSprite(&tft);

#include "TPLinkSmartPlug.h"
TPLinkSmartPlug smartplug;
#define TARGET "<YOUR SMART PLUG>" // IP Address or Name of Smart Plug

#define POWER_ON_THRESHOLD (1000)
#define POWER_OFF_THRESHOLD (800)

void displayCO2(float val) {
  if (!isnan(val)) {
    sprCO2.fillSprite(TFT_BLACK);
    sprCO2.setTextColor(TFT_GREEN);
    sprCO2.setTextDatum(TR_DATUM);
    sprCO2.setFreeFont(&FreeMonoBold24pt7b);
    sprCO2.drawNumber(static_cast<int16_t>(val), 123, 0);
    sprCO2.pushSprite(100, 50);
  }
}

void displayTemperature(float val) {
  if (!isnan(val)) {
    sprTemperature.fillSprite(TFT_BLACK);
    sprTemperature.setTextColor(TFT_GREEN);
    sprTemperature.setTextDatum(TR_DATUM);
    sprTemperature.setFreeFont(&FreeSansBold12pt7b);
    sprTemperature.drawFloat(val, 1, 46, 0);
    sprTemperature.pushSprite(80, 107);
  }
}

void displayHumidity(float val) {
  if (!isnan(val)) {
    sprHumidity.fillSprite(TFT_BLACK);
    sprHumidity.setTextColor(TFT_GREEN);
    sprHumidity.setTextDatum(TR_DATUM);
    sprHumidity.setFreeFont(&FreeSansBold12pt7b);
    sprHumidity.drawFloat(val, 1, 46, 0);
    sprHumidity.pushSprite(235, 107);
  }
}

void displaySwitch(bool on) {
  sprSwitch.fillRoundRect(0, 0, 120, 40, 20, TFT_LIGHTGREY);
  sprSwitch.drawRoundRect(0, 0, 120, 40, 20, TFT_WHITE);
  sprSwitch.setTextDatum(MC_DATUM);
  sprSwitch.setFreeFont(&FreeSansBold12pt7b);
  if (on) {
    sprSwitch.fillRoundRect(3, 3, 50, 34, 17, TFT_BLACK);
    sprSwitch.setTextColor(TFT_BLUE);
    sprSwitch.drawString("ON", 80, 20);
  } else {
    sprSwitch.fillRoundRect(67, 3, 50, 34, 17, TFT_BLACK);
    sprSwitch.setTextColor(TFT_RED);
    sprSwitch.drawString("OFF", 35, 20);
  }
  sprSwitch.pushSprite(170, 160);
}

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.drawRoundRect(5, 20, 310, 120, 10, TFT_WHITE);
  tft.drawRoundRect(6, 21, 308, 118, 9, TFT_WHITE);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("CO2 Concentration", 30, 10);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("ppm", 240, 60);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.drawString("Temp.", 20, 110);
  tft.drawString("C", 130, 110);
  tft.drawString("Humi.", 175, 110);
  tft.drawString("%", 285, 110);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("Threshold", 45, 150);
  tft.drawString("ON", 40, 175);
  tft.drawString("OFF", 40, 190);
  tft.setTextDatum(TR_DATUM);
  tft.drawNumber(POWER_ON_THRESHOLD, 130, 175);
  tft.drawNumber(POWER_OFF_THRESHOLD, 130, 190);

  tft.drawLine(20, 95, 300, 95, TFT_WHITE);
  tft.drawLine(35, 170, 135, 170, TFT_WHITE);


  tft.setTextDatum(TL_DATUM);
  tft.drawString("IP Address", 10, 220);
  tft.drawString("Connecting...", 120, 220);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  tft.fillRect(120, 220, 200, 20, TFT_BLACK);
  tft.drawString(WiFi.localIP().toString().c_str(), 120, 220);

  sprCO2.createSprite(128, 42);
  sprTemperature.createSprite(45, 20);
  sprHumidity.createSprite(45, 20);
  sprSwitch.createSprite(120, 40);

  board.GroveI2C1.Enable();
  scd30.Init();

  smartplug.begin(client, udp);
  smartplug.setTarget(TARGET);
  smartplug.setRelayState(false);
  displaySwitch(false);
}

void loop() {
  static float preCo2 = 0.0;

  if (scd30.ReadyToRead()) {
    scd30.Read();
    displayCO2(scd30.Co2Concentration);
    displayTemperature(scd30.Temperature);
    displayHumidity(scd30.Humidity);

    if (!isnan(scd30.Co2Concentration)) {
      if (preCo2 < POWER_ON_THRESHOLD && POWER_ON_THRESHOLD <= scd30.Co2Concentration) {
        smartplug.setRelayState(true);
        displaySwitch(true);
      } else if (scd30.Co2Concentration <= POWER_OFF_THRESHOLD && POWER_OFF_THRESHOLD < preCo2) {
        smartplug.setRelayState(false);
        displaySwitch(false);
      }
      preCo2 = scd30.Co2Concentration;
    }
  }
}
