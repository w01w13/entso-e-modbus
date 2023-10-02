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
#include <StringTokenizer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

#define NUM_REGS 72
#define TOKEN_LENGTH 36
#define TAX_LENGTH 2
#define MARGIN_LENGTH 4
#define UNIT_LENGTH 1
#define BAUD_LENGTH 6
#define SLAVE_LENGTH 3

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

bool wm_nonblocking = false; // change to true to use non blocking
int status;
// Modbus
String baudRates[] = { "9600", "19200", "38400", "57600", "115200" };
String baudRate = "9600";
String slaveId = "100";
// Entso-E token
String token = "";
// Tax, defaults to company
String tax = "0.0";
// Margin, defaults to zero
String margin = "0.00";
// Should have prices in KWh / Mwh
// 1 is eur/MWh, 0 is cents/KWh
String selectedPrice = "0";
// mDNS
String mDnsName = "entso-modbus";
// Price data & control params
double* priceData;
int* priceLen;
unsigned long nextUpdate = 0;
unsigned long now = 0;
bool hasValues = false;
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
    free(priceData);
    free(priceLen);
    status = entso_e_refresh(token.c_str(), &priceData, &priceLen);
    Serial.printf("Got status %i\n", status);
    // Reset the status
    // TODO: Should we move this as coil? Hack, figure out proper way to update the HREG
    addToHreg(0, status);
    addToHreg(1, 0);
    // If intial read fails, dont put values in registry.
    // If other requests fail, we've the values
    if (status != 0 && !hasValues) {
        Serial.println("Invalid read");
    } else {
        hasValues = true;
        Serial.println("Read successfull");
        updateRegistry();
    }
}
void addToHreg(int offset, uint16_t value)
{
    rtuSlave.Hreg(offset, value);
    rtuSlave.Hreg(offset, value);
}
void configModeCallback(WiFiManager* myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

int saveToEeprom(String value, unsigned int len, unsigned int offset)
{
    if (value) {
        for (unsigned int i = 0; i < len; ++i) {
            EEPROM.write(i + offset, value[i]);
        }
    }
    return len;
}
String readFromEeprom(unsigned int len, unsigned int offset)
{
    String tmp;
    for (unsigned int i = offset; i < len + offset; ++i) {
        char c = char(EEPROM.read(i));
        if (c != '\0') {
            tmp += c;
        }
    }
    return tmp;
}
void saveParamCallback()
{
    token = getParam("entso");
    tax = getParam("tax");
    margin = getParam("margin");
    Serial.println("Entso-E token is " + token);
    Serial.println("Tax is " + tax);
    Serial.println("Margin is " + margin);
    int offset = 0;
    EEPROM.begin(512);
    offset += saveToEeprom(token, TOKEN_LENGTH, offset);
    offset += saveToEeprom(tax, TAX_LENGTH, offset);
    offset += saveToEeprom(margin, MARGIN_LENGTH, offset);
    offset += saveToEeprom(selectedPrice, UNIT_LENGTH, offset);
    EEPROM.commit();
    EEPROM.end();
    eeprom_read();
}
void eeprom_read()
{
    EEPROM.begin(512);
    int offset = 0;
    token = readFromEeprom(TOKEN_LENGTH, offset);
    offset += TOKEN_LENGTH;
    Serial.print("Token is ");
    Serial.println(token);
    tax = readFromEeprom(TAX_LENGTH, offset);
    offset += TAX_LENGTH;
    if (tax == NULL) {
        tax = "0";
    }
    Serial.print("Tax is ");
    Serial.println(tax.toDouble());
    margin = readFromEeprom(MARGIN_LENGTH, offset);
    offset += MARGIN_LENGTH;
    if (margin == NULL) {
        margin = "0";
    }
    Serial.print("Margin is ");
    Serial.println(margin.toDouble());
    selectedPrice = readFromEeprom(UNIT_LENGTH, offset);
    offset += UNIT_LENGTH;
    Serial.print("Unit is ");
    Serial.println(selectedPrice);
    baudRate = readFromEeprom(BAUD_LENGTH, offset);
    offset += BAUD_LENGTH;
    if (baudRate == NULL) {
        baudRate = "9600";
    }
    Serial.print("Baud is ");
    Serial.println(baudRate);
    slaveId = readFromEeprom(SLAVE_LENGTH, offset);
    offset += SLAVE_LENGTH;
    if (slaveId == NULL) {
        slaveId = "100";
    }
    Serial.print("SlaveId is ");
    Serial.println(slaveId);
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
        = "<!DOCTYPE html><html><head><title>Entso2Modbus</title><style>.h2 {margin: 0} label {margin: 2px;} select { "
          "height: 1.8em; "
          "border: unset; width: 100%; font-size: 18px; line-height: 1.2em;} input::-webkit-outer-spin-button, "
          "input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; } .data-container { display: block; "
          "} input[type=number] { -moz-appearance: textfield; } .form-wrapper { margin: 10px; } .full-input { display: "
          "inline-block; padding: 3px; border: 1px solid black; border-radius: 6px; } input { outline: none; border: "
          "none; display: block; line-height: 1.2em; font-size: 14pt; } label { display: block; font-size: 12px; "
          "color: black; } html { display: grid; } body { display: grid; font-family: Arial, Helvetica, sans-serif; } "
          ".save-container { display: flex; justify-content: end; } .action-form-container { display: grid; "
          "grid-template-columns: auto auto; } .form-container { display: grid; margin: 4em; } .form { display: grid; "
          "grid-template-columns: auto; gap: 1em; } .navigation-item:hover { background-color: #ddd; } .tabcontent { "
          "display: none; padding: 6px 12px; border: 1px solid #ccc; border-top: none; } .navigation-item { "
          "background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; "
          "transition: 0.3s; font-size: 17px; } .action-button:hover { cursor: pointer; filter: brightness(50%); "
          "color: white; } .action-button { transition-duration: 0.4s; border-radius: 10px; border: none; color: "
          "white; padding: 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; "
          "margin: 4px 2px; } .submit { background-color: #ff0000; } .save { background-color: #3cb371 } .actions { "
          "display: flex; align-items: end; justify-content: end; } table { border-collapse: collapse; border-spacing: "
          "0; width: 100%; border: 1px solid #ddd; } th, td { text-align: left; padding: 16px; } tr:nth-child(even) { "
          "background-color: #f2f2f2; } </style></head><body onLoad=\"openTab(event, 'data-container')\"><script> "
          "function openTab(evt, tabName) { var i, "
          "tabcontent, tablinks; tabcontent = document.getElementsByClassName('tabcontent'); for (i = 0; i < "
          "tabcontent.length; i++) { tabcontent[i].style.display = 'none'; } tablinks = "
          "document.getElementsByClassName('tablinks'); for (i = 0; i < tablinks.length; i++) { tablinks[i].className "
          "= tablinks[i].className.replace(' active', ''); } document.getElementById(tabName).style.display = 'block'; "
          "evt.currentTarget.className += ' active'; } </script><div class='navigation'><button "
          "class='navigation-item active' onclick=\"openTab(event, 'data-container')\">Data</button><button "
          "class='navigation-item' onclick=\"openTab(event, 'action-container')\">Configuration</button></div><div "
          "class='tabcontent' id='action-container'><div class='action-form-container'><div "
          "class='form-container'><form action='save' method='POST'><div class='form'>";
    s += "<h2>Entso-E Settings</h2>";
    s += "<div class='full-input'><label for='token'>Entso-E Token</label><input id='token' name='token' maxlength=36 "
         "type='password' value='";
    s += token;
    s += "' /></div>";
    s += "<h2>Price Settings</h2>";
    s += "<div class='full-input'><label for='tax'>VAT</label><input id='tax' name='tax' type='number' value='";
    s += tax;
    s += "' /></div><div "
         "class='full-input'><label for='margin'>Margin</label><input id='margin' name='margin' type='number' "
         "step='.01' value='";
    s += margin;
    s += "' /></div>";
    s += "<div class='full-input'><label for='unit'>Output unit</label><select name='unit' id='unit' "
         "name='unit'>";
    s += "<option value='0'";
    if (selectedPrice == "0") {
        s += "selected";
    }
    s += ">&cent;/KWh</option><option value='1'";
    if (selectedPrice == "1") {
        s += "selected";
    }
    s += ">&euro;/MWh</option></select></div>";
    // Modbus settings
    s += "<h2>Modbus Settings</h2>";

    int numBaudRates = sizeof(baudRates) / sizeof(baudRates[0]);
    s += "<div class='full-input'><label for='baud'>Baud</label><select name='baud' id='baud' "
         "name='baud'>";
    for (int i = 0; i < numBaudRates; i++) {
        String baud_rate = baudRates[i];
        s += "<option value='";
        s += baud_rate;
        s += "'";
        if (baud_rate == baudRate) {
            s += "selected";
        }
        s += ">" + baud_rate + "</option>";
    }
    s += "</select></div>";
    s += "<div class='full-input'><label for='slave'>SlaveID</label><input id='slave' name='slave' type='number' "
         "maxLength='3' value='";
    s += slaveId;
    s += "'/></div>";
    s += "<div class='full-input'><label for='serial'>Serial Information (readonly)</label><input id='serial' "
         "value='8-N-1' readonly></div>";
    s += "<br><div class='save-container'><input type='submit' class='save action-button' "
         "value='Save Settings' /></div></div></form></div><div class='form-container'><div class='actions'>";
    char link[140];
    sprintf(link, "<form action='%s'> <input type='submit' class='submit action-button' value='Reset Wifi' /></form>",
        reset_token.c_str());
    s += link;
    s += "</div></div></div></div><div class='tabcontent' id='data-container'><div "
         "class='data'><table><tr><th>Index</th><th>Value</th></tr>";
    char state[50];
    sprintf(state, "<tr><td>%d</td><td>%d</td></tr>", 0, status);
    s += state;
    for (int i = 0; i < *priceLen; i++) {
        char line[50];
        sprintf(line, "<tr><td>%d</td><td>%f</td></tr>", i + 1, getPrice((float)priceData[i]));
        s += line;
    }
    s += "</table></div></div></body></html>";
    server.send(200, "text/html", s);
}
void handleReset()
{
    reset = true;
    server.send(200, "text/plain", "Settings reset");
}

void handleGet() { server.send(200, "application/json", "Settings reset"); }
void handleSave()
{
    // Check if body received
    if (server.hasArg("plain") == false) {
        server.send(200, "text/plain", "Body not received");
    } else {
        StringTokenizer tokens(server.arg("plain"), "&");
        int offset = 0;
        EEPROM.begin(512);
        bool shouldReset = false;
        while (tokens.hasNext()) {
            String val = tokens.nextToken();
            String key = val.substring(0, val.indexOf("="));
            if (key == "token") {
                token = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(token, TOKEN_LENGTH, offset);
            } else if (key == "tax") {
                tax = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(tax, TAX_LENGTH, offset);
            } else if (key == "margin") {
                margin = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(margin, MARGIN_LENGTH, offset);
            } else if (key == "unit") {
                selectedPrice = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(selectedPrice, UNIT_LENGTH, offset);
            } else if (key == "baud") {
                shouldReset = true;
                baudRate = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(baudRate, BAUD_LENGTH, offset);
            } else if (key == "slave") {
                shouldReset = true;
                slaveId = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(slaveId, SLAVE_LENGTH, offset);
            } else {
                Serial.println("Unknown " + key);
            }
        }
        EEPROM.commit();
        EEPROM.end();
        eeprom_read();
        if (shouldReset) {
            ESP.restart();
        } else {
            handleNotFound();
        }
    }
    return;
}
void handleNotFound()
{
    server.sendHeader("Location", "/");
    server.send(307, "text/plain", "");
}

void resetSettings()
{
    wm.resetSettings();
    handleNotFound();
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
    Serial.println("Creating registry");
    for (int i = 0; i < NUM_REGS; i++) {
        rtuSlave.addHreg(i, 1);
        tcpSlave.addHreg(i, 1);
    }
}

float addMargin(float price)
{
    if (margin) {
        return price + (margin.toFloat() * 10);
    } else {
        return price;
    }
}
float addVat(float price)
{
    if (tax) {
        float tmp = price;
        float vat = tax.toFloat() / 100;
        if (vat > 0 && tmp > 0) {
            tmp += (tmp * vat);
        }
        return tmp;
    } else {
        return price;
    }
}

float convertPrice(float price)
{
    if (selectedPrice && selectedPrice == "0") {
        return price / 10;
    } else {
        return price;
    }
}

float getPrice(float price)
{
    float tmp = addVat(price);
    tmp = addMargin(tmp);
    return convertPrice(tmp);
}
void updateRegistry()
{
    Serial.printf("Prices in memory:\u0020[");
    int count = 2;
    for (int i = 0; i < *priceLen; i++) {
        int offset = (i * 2) + 2;
        float price = getPrice((float)priceData[i]);
        // https://github.com/emelianov/modbus-esp8266/issues/158
        uint16_t high, low;
        float2IEEE754(price, &high, &low);
        addToHreg(offset, high);
        addToHreg(offset + 1, low);
        if (i == (*priceLen - 1)) {
            Serial.printf("%f]\n", price);
        } else {
            Serial.printf("%f,\u0020", price);
        }
        count += 2;
    }
    // Pad the rest so the receiving end will reset the values
    for (int i = count; i <= NUM_REGS; i++) {
        addToHreg(i, 0);
        addToHreg(i + 1, 0);
    }
}

void float2IEEE754(double float_number, uint16_t* high, uint16_t* low)
{ // split the float and return first unsigned integer
    union {
        float f;
        uint32_t u;
    } data;
    data.f = float_number;
    uint32_t ieee754 = data.u;
    *high = (uint16_t)(ieee754 >> 16);
    *low = ieee754;
}

void setup()
{
    generate_reset_token();
    WiFi.mode(WIFI_STA);
    Serial.begin(115200);
    timeClient.begin(); // Start NTP
    delay(3000);
    if (wm_nonblocking) {
        wm.setConfigPortalBlocking(false);
    }
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
        server.on("/save", handleSave);
        server.begin(80);
        if (!MDNS.begin(mDnsName)) {
            Serial.println("Error setting up MDNS responder!");
        }
    }

    //  pinMode(TX_ENABLE, OUTPUT);
    pinMode(RX_ENABLE, OUTPUT);
    // digitalWrite(TX_ENABLE, LOW);
    digitalWrite(RX_ENABLE, LOW);
    MAX_485.begin(baudRate.toInt(), SWSERIAL_8N1);
    rtuSlave.begin(&MAX_485); // Modbus RTU start
    rtuSlave.slave(slaveId.toInt());
    tcpSlave.begin(); // Modbus TCP start
    createRegistry();
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