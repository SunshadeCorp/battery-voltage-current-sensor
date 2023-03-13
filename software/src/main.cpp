#include <Adafruit_ADS1X15.h>
#include <Arduino.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WiFi.h>
#include <lwip/dns.h>
#include <PubSubClient.h>

#include "config.h"
#include "version.h"

#define intmax 32767.0f

Adafruit_ADS1115 ads;
WiFiClient espClient;
PubSubClient client(mqtt_server, mqtt_port, espClient);

constexpr unsigned long MEASURE_INTERVAL = 500;
constexpr unsigned long BLINK_TIME = 5000;

String hostname;
String sensor_topic("esp-total");

unsigned long last_blink = 0;
unsigned long last_connection = 0;
unsigned long last_measure = 0;

#define MSG_BUFFER_SIZE 50
char msg[MSG_BUFFER_SIZE];
int value = 0;
float current_offset = 0.0f;

float readVoltage();                // Reads a voltage from an Input Pin of the ADC
float readCurrent();                // Reads a current from an Input Pin of the ADC
float readTemperature(int adc_pin); // Reads a temperatuer measured by a NTC from the Input Pin of the ADC

void connectWifi() {
    Serial.println();
    Serial.print("connecting to ");
    Serial.println(ssid);
    ESP8266WiFiClass::persistent(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("DNS1: ");
    Serial.println(IPAddress(dns_getserver(0)));
    Serial.print("DNS2: ");
    Serial.println(IPAddress(dns_getserver(1)));
}

void callback(char *topic, byte *payload, unsigned int length) {
    String topic_string = String(topic);
    String payload_string = String();
    payload_string.concat((char *) payload, length);
    if (topic_string == sensor_topic + "/blink") {
        last_blink = millis();
    } else if (topic_string == sensor_topic + "/restart") {
        if (payload_string == "1") {
            EspClass::restart();
        }
    } else if (topic_string == sensor_topic + "/ota") {
        client.publish((sensor_topic + "/ota_start").c_str(),
                       (String("ota started [") + payload_string + "] (" + millis() + ")").c_str());
        client.publish((sensor_topic + "/ota_url").c_str(),
                       (String("https://") + ota_server + payload_string).c_str());
        WiFiClientSecure client_secure;
        client_secure.setTrustAnchors(&cert);
        client_secure.setTimeout(60);
        ESPhttpUpdate.setLedPin(LED_BUILTIN, HIGH);
        switch (ESPhttpUpdate.update(client_secure, String("https://") + ota_server + payload_string)) {
            case HTTP_UPDATE_FAILED: {
                String error_string = String("HTTP_UPDATE_FAILED Error (");
                error_string += ESPhttpUpdate.getLastError();
                error_string += "): ";
                error_string += ESPhttpUpdate.getLastErrorString();
                error_string += "\n";
                Serial.println(error_string);
                client.publish((sensor_topic + "/ota_ret").c_str(), error_string.c_str());
            }
                break;
            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("HTTP_UPDATE_NO_UPDATES");
                client.publish((sensor_topic + "/ota_ret").c_str(), "HTTP_UPDATE_NO_UPDATES");
                break;
            case HTTP_UPDATE_OK:
                Serial.println("HTTP_UPDATE_OK");
                client.publish((sensor_topic + "/ota_ret").c_str(), "HTTP_UPDATE_OK");
                break;
        }
    }
}

void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(hostname.c_str(), mqtt_username, mqtt_password, (sensor_topic + "/available").c_str(), 0,
                           true, "offline")) {
            Serial.println("connected");
            // Once connected, publish an announcement...
            client.publish((sensor_topic + "/available").c_str(), "online", true);
            client.publish((sensor_topic + "/version").c_str(), VERSION, true);
            client.publish((sensor_topic + "/build_timestamp").c_str(), BUILD_TIMESTAMP, true);
            // ... and resubscribe
            client.subscribe((sensor_topic + "/blink").c_str());
            client.subscribe((sensor_topic + "/restart").c_str());
            client.subscribe((sensor_topic + "/ota").c_str());
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            //   DEBUG_PRINTLN(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            // delay(5000);
            if (last_connection != 0 && millis() - last_connection >= 30000) {
                EspClass::restart();
            }
            delay(1000);
        }
    }
}

void setup() {
    Serial.begin(74880);
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.println("Starte Init vom Chip");
    ads.begin(0x48);
    Serial.println("Chip Init done");
    current_offset = readCurrent();

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char mac_string[6 * 2 + 1] = {0};
    snprintf(mac_string, 6 * 2 + 1, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    hostname = String("bvc-sensor-") + mac_string;
    Serial.println(hostname);

    connectWifi();
    randomSeed(micros());
    client.setCallback(callback);
}

void loop() {
    /*
     * MQTT reconnect if needed
     */
    if (!client.connected()) {
        reconnect();
    }
    last_connection = millis();
    client.loop();

    if (millis() - last_measure > MEASURE_INTERVAL) {
        last_measure = millis();

        float current;
        float voltage;
        float temp1;
        float temp2;

        current = readCurrent() - current_offset;
        voltage = readVoltage();
        temp1 = readTemperature(2);
        temp2 = readTemperature(3);

        client.publish((sensor_topic + "/uptime").c_str(), String(millis()).c_str());
        digitalWrite(LED_BUILTIN, LOW);
        snprintf(msg, MSG_BUFFER_SIZE, "%f", voltage);
        client.publish((sensor_topic + "/total_voltage").c_str(), msg);
        snprintf(msg, MSG_BUFFER_SIZE, "%f", current);
        client.publish((sensor_topic + "/total_current").c_str(), msg);
        snprintf(msg, MSG_BUFFER_SIZE, "%f", temp1);
        client.publish((sensor_topic + "/temp1").c_str(), msg);
        snprintf(msg, MSG_BUFFER_SIZE, "%f", temp2);
        client.publish((sensor_topic + "/temp2").c_str(), msg);
        digitalWrite(LED_BUILTIN, HIGH);
    }
    if (millis() - last_blink < BLINK_TIME) {
        if ((millis() - last_blink) % 100 < 50) {
            digitalWrite(LED_BUILTIN, HIGH);
        } else {
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
}

float readCurrent() {
    float adc_value;      // raw value from the ADConverter
    float Uin;            // calculated input voltage at the input of ADC
    float current;        // calculated current
    float offset = 2.545; // Offset of the hall sensor = 2,5V , should configurable over MQTT
    adc_value = ads.readADC_SingleEnded(1); // read analog value from ADC
    delay(10);
    adc_value = adc_value + ads.readADC_SingleEnded(1); // read analog value from ADC
    delay(10);
    adc_value = adc_value + ads.readADC_SingleEnded(1); // read analog value from ADC

    Uin = (((adc_value / 3.0f) / intmax) * 6.144f);
    current = (Uin - offset) / 0.040f; // 40mV/A
    current = -current;
    return (current);
}

float readVoltage() {
    float adc_value;
    float Uin;
    float HV;
    //const float rges = 6800.0 + 10.0 * (100000.0);
    //const float r2 = 6800;
    //manual voltage factor between input voltage and HV voltage
    float factor = 148.52f;


    adc_value = ads.readADC_SingleEnded(0);
    delay(10);
    adc_value = adc_value + ads.readADC_SingleEnded(0);
    delay(10);
    adc_value = adc_value + ads.readADC_SingleEnded(0);

    Uin = (((adc_value / 3.0f) / intmax) * 6.144f);
    //Send raw input voltage
    snprintf(msg, MSG_BUFFER_SIZE, "%f", Uin);
    client.publish("esp-total/Uin", msg);
    HV = Uin * factor;

    return (HV);
}

float readTemperature(int adc_pin) {
    // https://www.mymakerstuff.de/2018/05/18/arduino-tutorial-der-temperatursensor/
    float rt;     //restistance of the thermal resistor
    float rt1 = 10000.0;
    float uges = 5.0;
    float adc_value;


    float b = 3434;
    float TKelvin;
    float kelvintemp = 273.15;
    float Tn = kelvintemp + 25; // Nenntemperatur in Kelvin
    float temperature;
    float Uin;
    adc_value = ads.readADC_SingleEnded(adc_pin); // lese Analogwert aus ADC chip

    Uin = ((adc_value / intmax) * 6.144f);
    rt = ((rt1) * (Uin / uges) / (1 - (Uin / uges)));
    TKelvin = 1 / ((1 / Tn) + ((float) 1 / b) * log((float) rt / rt1)); // ermittle die Temperatur in Kelvin
    temperature = TKelvin - kelvintemp;
    return temperature;
}
