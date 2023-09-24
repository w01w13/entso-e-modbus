#include "ModbusIP_ESP8266.h"
#include "ModbusRTU.h"
#include "entso-e.h"
#include <EEPROM.h>
#include <ESP8266TrueRandom.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <SoftwareSerial.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

#define BAUD_RATE 9600
#define SLAVE_ID 100
#define NUM_REGS 49

ESP8266WebServer server;

// D1: Driver Input (TX pin), DI blue
#define TX_PIN 5 // D1
// D2: Driver Enable (LOW-Enable), DE green
#define TX_ENABLE 4 // D2
// D3 Receiver Enable (HIGH-Enable), RE yellow
#define RX_ENABLE 0
// D4 Receiver Output (RX pin), RO orange
#define RX_PIN 2
// Wifi
WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field; // global param ( for non blocking w params )
bool wm_nonblocking = false; // change to true to use non blocking
int status;
// Entso-E token
String token = "";
// mDNS
String mDnsName = "entso-modbus";
// Price data & control params
double priceData[NUM_REGS];
unsigned long nextUpdate = 0;
unsigned long now = 0;
// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
// Modbus
ModbusRTU rtuSlave;
ModbusIP tcpSlave;
// SoftwareSerial S(int8_t rxPin, int8_t txPin = -1, bool invert = false);
SoftwareSerial MAX_485(RX_PIN, TX_PIN);
// Reset token
bool reset = false;
String reset_token;
void refresh()
{
    Serial.printf("START: Retrieving values, update is %lu\n", nextUpdate);
    status = entso_e_refresh(token.c_str(), priceData);
    Serial.printf("Got status %i\n", status);
    // Reset the status
    // TODO: Should we move this as coil? Hack, figure out proper way to update the HREG
    rtuSlave.removeHreg(0, 1);
    rtuSlave.addHreg(0, status, 1);
    if (status != 0) {
        Serial.println("Invalid read");
    } else {
        resetRegistry(); // Clear existing values. Hack, figure out proper way to update the HREG.
        Serial.println("Read successfull");
        updateRegistry();
    }
}
void configModeCallback(WiFiManager* myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}
void saveParamCallback()
{
    EEPROM.begin(512);
    token = getParam("customfieldid");
    if (token) {
        Serial.println("Entso-E token is " + token);
        Serial.println("Entso-E token length is " + token.length());
        for (unsigned int i = 0; i < token.length(); ++i) {
            EEPROM.write(i, token[i]);
        }
        EEPROM.commit();
        EEPROM.end();
    } else {
        Serial.println("Entso-E token i null");
        wm.resetSettings();
    }
}
void eeprom_read()
{
    EEPROM.begin(512);
    token = "";
    if (EEPROM.read(0) != 0) {
        for (int i = 0; i < 40; ++i) {
            token += char(EEPROM.read(i));
        }
        Serial.print("Token: ");
        Serial.println(token);
    } else {
        Serial.println("Token not found.");
    }
    EEPROM.end();
}
String getParam(String name)
{
    // read parameter from server, for customhmtl input
    String value;
    if (wm.server->hasArg(name)) {
        value = wm.server->arg(name);
    }
    return value;
}
void handleRoot()
{
    String s
        = "<html><head><title>Entso2Modbus</title><style>body {display: flex; flex-direction: column; overflow: "
          "hidden; max-height: 100vh; font-family: Arial, sans-serif;} .submit:hover {cursor: pointer; "
          "background-color: "
          "#cc0000; color: white;}.submit {transition-duration: 0.4s; border-radius: 10px; background-color: "
          "#ff0000;  border: none;  color: "
          "white;  padding: 20px;  text-align: center;  text-decoration: none;  display: inline-block;  "
          "font-size: 16px;  margin: 4px 2px;} .container {display: flex; flex-direction: column; overflow: scroll;} "
          ".actions {display: flex; justify-content: right;}.data { flex: 1;"
          "}table {  border-collapse: collapse;  border-spacing: 0;  width: 100%;  border: 1px "
          "solid #ddd;}th, td {  text-align: left;  padding: 16px;}tr:nth-child(even) {  background-color: "
          "#f2f2f2;}</style></head><body>";
    char link[123];
    sprintf(link,
        "<div class='actions'><form action='%s'> <input type='submit' class='submit' value='Reset Settings' "
        "/></form></div>",
        reset_token.c_str());
    s += link;
    s += "<div class='container'><div class='data'><table><tr><th>Index</th><th>Value</th></tr>";
    char state[50];
    sprintf(state, "<tr><td>%d</td><td>%d</td></tr>", 0, status);
    s += state;
    for (unsigned int i = 0; i < sizeof(priceData) / sizeof(double); i++) {
        char line[50];
        sprintf(line, "<tr><td>%d</td><td>%f</td></tr>", i + 1, priceData[i]);
        s += line;
    }
    s += "</div></table>";

    s += "</body></html>";
    server.send(200, "text/html", s);
}
void handleReset()
{
    reset = true;
    server.send(200, "text/plain", "Settings reset");
}

void handleNotFound()
{
    server.sendHeader("Location", "/");
    server.send(307, "text/plain", "");
}

void resetSettings()
{
    wm.resetSettings();
    EEPROM.begin(512);
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.end();
    ESP.restart();
}
void generate_reset_token()
{
    reset_token = ESP8266TrueRandom.random(10000, 20000);
    Serial.println("Generated reset token:");
    Serial.println(reset_token);
}
void createRegistry()
{
    for (unsigned int i = 0; i < sizeof(priceData) / sizeof(double); i++) {
        rtuSlave.addHreg(i + 1, 0, 1);
        tcpSlave.addHreg(i + 1, 0, 1);
    }
}

void resetRegistry()
{
    for (unsigned int i = 0; i < sizeof(priceData) / sizeof(double); i++) {
        rtuSlave.removeHreg(i + 1, 1);
        tcpSlave.removeHreg(i + 1, 1);
    }
}

void updateRegistry()
{
    for (unsigned int i = 0; i < sizeof(priceData) / sizeof(double); i++) {
        double price = priceData[i];
        Serial.printf("Price %d in memory is: %f\n", i, price);
        rtuSlave.addHreg(i + 1, price, 1);
        tcpSlave.addHreg(i + 1, price, 1);
    }
}

void setup()
{
    generate_reset_token();
    WiFi.mode(WIFI_STA);
    Serial.begin(115200);
    timeClient.begin(); // Start NTP
    delay(3000);
    if (wm_nonblocking)
        wm.setConfigPortalBlocking(false);
    int customFieldLength = 40;

    new (&custom_field) WiFiManagerParameter("customfieldid", "Entso-E token", "", customFieldLength);
    ; // custom html type
    wm.addParameter(&custom_field);
    wm.setSaveParamsCallback(saveParamCallback);
    bool res = wm.autoConnect(mDnsName.c_str(), "password"); // password protected ap
    if (!res) {
        Serial.println("Failed to connect or hit timeout");
        // ESP.restart();
    } else {
        // if you get here you have connected to the WiFi
        Serial.println("Connected to WiFi");
        eeprom_read();
        server.onNotFound(handleNotFound);
        server.on("/", handleRoot);
        server.on("/" + reset_token, handleReset);
        server.begin(80);
        if (!MDNS.begin(mDnsName)) {
            Serial.println("Error setting up MDNS responder!");
        }
    }

    //  pinMode(TX_ENABLE, OUTPUT);
    pinMode(RX_ENABLE, OUTPUT);
    // digitalWrite(TX_ENABLE, LOW);
    digitalWrite(RX_ENABLE, LOW);
    MAX_485.begin(BAUD_RATE, SWSERIAL_8N1);
    rtuSlave.begin(&MAX_485); // Modbus RTU start
    rtuSlave.slave(SLAVE_ID);
    tcpSlave.begin(); // Modbus TCP start
    createRegistry(); // Create HREG
}

void loop()
{
    MDNS.update();
    wm.process();
    server.handleClient();
    timeClient.update(); // Update NTP and current time
    now = timeClient.getEpochTime();
    // If we should update the data
    if (WiFi.status() == WL_CONNECTED && nextUpdate <= now) {
        nextUpdate = (now - (now % 60)) + 60; // Polls every minute, values will be updated hourly
        refresh();
    } else if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No wifi connection");
    }
    rtuSlave.task();
    tcpSlave.task();
    yield();
    if (reset) {
        resetSettings();
    }
}