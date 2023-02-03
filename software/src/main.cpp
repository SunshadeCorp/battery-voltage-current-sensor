#include <Adafruit_ADS1X15.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <lwip/dns.h>
#include <PubSubClient.h>

#define intmax 32767.0

const char* ssid = "mob";
const char* password = "12345678";
const char* mqtt_server = "192.168.216.155";

Adafruit_ADS1115 ads;
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

float ReadVoltage();                //Reads a voltage from an Input Pin of the ADC
float ReadCurrent();                //Reads a current from an Input Pin of the ADC
float ReadTemperature(int adc_pin); //Reads a temperatuer measured by a NTC from the Input Pin of the ADC



// connect to wifi
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("Starte Init vom Chip");
  ads.begin(0x48);
  Serial.println("Chip Init done");
    setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  delay(100);
  // put your main code here, to run repeatedly:
  Serial.println("bin da");
    if (!client.connected()) {
    reconnect();
  }
  client.loop();
  float current;
  float voltage;
  float temp1;
  float temp2;

  current = ReadCurrent();
  voltage = ReadVoltage();
  Serial.print("Ruckgabewert Spannung: ");
 Serial.println(voltage);
  temp1 = ReadTemperature(2);
  temp2 = ReadTemperature(3);
  unsigned long now = millis();



    snprintf (msg, MSG_BUFFER_SIZE, "Voltage %f", voltage);
    Serial.print("Publish Voltage: ");
    Serial.println(msg);
    client.publish("Voltage", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "Current %f", current);
    client.publish("Current", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "temp1 %f", temp1);
    client.publish("temp1", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "temp2 %f", temp2);
    client.publish("temp2", msg);
 

  digitalWrite(BUILTIN_LED, LOW);
  delay(100);
  digitalWrite(BUILTIN_LED, HIGH);
}


float ReadCurrent()
{
  float adc_value;                            //raw value from the ADConverter
  float Uin;                                  //calculated input voltage at the input of ADC
  float current;                              //calculated current 
  float offset = 2.545;                       //Offset of the hall sensor = 2,5V , should configurable over MQTT
  
  
  adc_value = ads.readADC_SingleEnded(1);     //read analog value from ADC
  //Serial.print("Raw Value: ");
  //Serial.println(adc_value);
  Uin = ((adc_value / intmax) * 6.144);
  Serial.print("In Voltage Current Sensor: ");
  Serial.println(Uin);
  current = (Uin-offset) / 0.040;      //40mV/A 
  current = -current; 
  Serial.print("Current: ");
  Serial.println(current);
  return (current);
}


float ReadVoltage()
{
  float adc_value;
  float Uin;
  float HV;
  const float rges = 6800.0+10.0*(100000.0);
  const float r2 = 6800;
  float factor= 145.117;

  int teiler = 10;

  //divider1 = 1.0 / (r2 / rges);
        Serial.print("factor 1: ");
      Serial.println(factor);
  adc_value = ads.readADC_SingleEnded(0);

  Uin = ((adc_value / intmax) * 6.144);
  Serial.print("Spannunng Eingang: ");
  Serial.println(Uin);
    snprintf (msg, MSG_BUFFER_SIZE, "Uin %f", Uin);
    client.publish("Uin", msg);
  HV = Uin  *  factor;
  //    Serial.println(" ");
  //    Serial.print("Rohwert Eingang: ");
  //    Serial.println(adc_value);
      Serial.print("Spannungswert HV: ");
      Serial.println(HV);
  return (HV);
}

float ReadTemperature(int adc_pin)
{
  //https://www.mymakerstuff.de/2018/05/18/arduino-tutorial-der-temperatursensor/
  float rt;
  float rtnano;
  float rt1 = 10000.0;
  float uges = 5.0;
  float adc_value;
  
  float untc;
  float b = 3434;
  float TKelvin;
  float T;
  float kelvintemp = 273.15;
  float Tn = kelvintemp + 25;               //Nenntemperatur in Kelvin
  float temperature;
  float TKelvin2;
  float Uin;
  adc_value = ads.readADC_SingleEnded(adc_pin);      // lese Analogwert aus ADC chip

  Uin = ((adc_value / intmax) * 6.144);
  rt = ((rt1) * (Uin / uges) / (1 - (Uin / uges)));
  TKelvin2 = 1 / ((1 / Tn) + ((float)1 / b) * log((float)rt / rt1));                             // ermittle die Temperatur in Kelvin
  temperature = TKelvin2 - kelvintemp;
  return temperature;
}