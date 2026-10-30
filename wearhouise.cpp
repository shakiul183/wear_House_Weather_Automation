/******************************************************
 * Production Room Weather Automation
 * ESP32 + DHT22 + Blynk IoT
 *
 * V0 = Humidity
 * V1 = Temperature
 * V2 = Humidifier Switch
 * V3 = Exhaust Fan Switch
 * V4 = Humidifier Status
 * V5 = Exhaust Status
 * V6 = Auto / Manual Mode
 *
 * FIXES in this version:
 *  1. Humidifier is a push-button (toggle) device, not a latching
 *     relay load -> the pulse that "presses the button" is now
 *     SHORT (default 250ms) instead of a 2-second hold. Holding
 *     the contact closed for 2s was being read by the humidifier's
 *     own controller as more than one press (it would toggle back
 *     ON right after appearing to turn OFF).
 *  2. WiFi reconnect logic added: the ESP32 radio is explicitly
 *     checked and reconnected if it drops, independent of Blynk's
 *     own reconnect handling.
 ******************************************************/

#define BLYNK_TEMPLATE_ID "TMPL6D-iDTot4"
#define BLYNK_TEMPLATE_NAME "Production Room Weather Automation"
#define BLYNK_AUTH_TOKEN "YlnUALDiIeOUmFndKoLgGcPglt2zYMi6"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

//================ WIFI =================

char ssid[] = "Arham";
char pass[] = "1211263947";

//================ SENSOR ===============

#define DHT_PIN 15
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

//================ OUTPUT ===============

#define HUMIDIFIER_PIN 4
#define EXHAUST_PIN 5

//================ LIMITS ===============

#define HUMI_LOW 60
#define HUMI_HIGH 65

// How long to hold the relay closed to simulate ONE button press.
// Start at 250ms. If it still double-toggles, try lower (150ms).
// If it doesn't register the press at all, try higher (400-500ms).
#define HUMI_PULSE_MS 250

//================ VARIABLES ============

float humidity = 0;
float temperature = 0;

bool autoMode = true;

bool humidifierState = false;
bool exhaustState = false;

bool humidifierPulseRunning = false;
unsigned long pulseStart = 0;

unsigned long lastSensorRead = 0;

//================ WIFI RECONNECT STATE =================

unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000; // check every 5s

//================================================
// Humidifier pulse (short tap, not a hold)
//================================================

void humidifierPulse(bool state)
{
  humidifierState = state;

  digitalWrite(HUMIDIFIER_PIN, HIGH);

  humidifierPulseRunning = true;
  pulseStart = millis();

  Serial.print("Humidifier ");
  Serial.println(state ? "ON" : "OFF");

  if(Blynk.connected())
  {
    Blynk.virtualWrite(V4, state ? "ON" : "OFF");
    Blynk.virtualWrite(V2, state);
  }
}

//================================================
// Exhaust control
//================================================

void setExhaust(bool state)
{
  exhaustState = state;

  digitalWrite(EXHAUST_PIN, state);

  Serial.print("Exhaust ");
  Serial.println(state ? "ON" : "OFF");

  if(Blynk.connected())
  {
    Blynk.virtualWrite(V5, state ? "Running" : "Stopped");
    Blynk.virtualWrite(V3, state);
  }
}

//================================================
// AUTO CONTROL
//================================================

void automaticControl()
{
  if(!autoMode)
    return;

  // Low humidity
  if(humidity < HUMI_LOW)
  {
    if(!humidifierState && !humidifierPulseRunning)
    {
      humidifierPulse(true);
    }

    if(exhaustState)
    {
      setExhaust(false);
    }
  }

  // High humidity
  else if(humidity > HUMI_HIGH)
  {
    if(humidifierState && !humidifierPulseRunning)
    {
      humidifierPulse(false);
    }

    if(!exhaustState)
    {
      setExhaust(true);
    }
  }
}

//================================================
// V2 Humidifier Switch
//================================================

BLYNK_WRITE(V2)
{
  if(autoMode)
  {
    Blynk.virtualWrite(V2, 0);
    return;
  }

  bool command = param.asInt();

  if(command != humidifierState && !humidifierPulseRunning)
  {
    humidifierPulse(command);
  }
}

//================================================
// V3 Exhaust Switch
//================================================

BLYNK_WRITE(V3)
{
  if(autoMode)
  {
    Blynk.virtualWrite(V3, exhaustState);
    return;
  }

  bool command = param.asInt();
  setExhaust(command);
}

//================================================
// V6 Auto Manual Switch
//================================================

BLYNK_WRITE(V6)
{
  autoMode = param.asInt();

  if(autoMode)
  {
    Serial.println("AUTO MODE ENABLED");
    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
  }
  else
  {
    Serial.println("MANUAL MODE ENABLED");

    if(humidifierState && !humidifierPulseRunning)
    {
      humidifierPulse(false);
    }

    if(exhaustState)
    {
      setExhaust(false);
    }

    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
  }
}

//================================================
// Blynk Connected
//================================================

BLYNK_CONNECTED()
{
  Serial.println("Blynk Connected");

  Blynk.syncVirtual(V6);

  if(!autoMode)
  {
    Blynk.syncVirtual(V2);
    Blynk.syncVirtual(V3);
  }

  Blynk.virtualWrite(V4, humidifierState ? "ON" : "OFF");
  Blynk.virtualWrite(V5, exhaustState ? "Running" : "Stopped");
}

//================================================
// WiFi + Blynk connection management
//================================================

void connectWiFi()
{
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);

  unsigned long startAttempt = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000)
  {
    delay(300);
    Serial.print(".");
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected");
  }
  else
  {
    Serial.println("\nWiFi connect failed, will retry in loop()");
  }
}

// Called periodically from loop() to make sure both WiFi and
// Blynk stay connected, and to recover automatically if either drops.
void maintainConnection()
{
  if(millis() - lastWifiCheck < WIFI_CHECK_INTERVAL)
    return;

  lastWifiCheck = millis();

  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
    return; // wait for WiFi before trying Blynk
  }

  if(!Blynk.connected())
  {
    Serial.println("WiFi OK but Blynk disconnected, reconnecting Blynk...");
    Blynk.connect(); // non-blocking attempt
  }
}

//================================================
// SETUP
//================================================

void setup()
{
  Serial.begin(115200);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(EXHAUST_PIN, OUTPUT);

  digitalWrite(HUMIDIFIER_PIN, LOW);
  digitalWrite(EXHAUST_PIN, LOW);

  dht.begin();

  delay(2000);

  connectWiFi();

  Blynk.config(BLYNK_AUTH_TOKEN);
  if(WiFi.status() == WL_CONNECTED)
  {
    Blynk.connect(5000);
  }

  Serial.println("System Ready");
}

//================================================
// LOOP
//================================================

void loop()
{
  maintainConnection();

  if(Blynk.connected())
  {
    Blynk.run();
  }

  // Finish humidifier pulse (short tap release)
  if(humidifierPulseRunning)
  {
    if(millis() - pulseStart >= HUMI_PULSE_MS)
    {
      digitalWrite(HUMIDIFIER_PIN, LOW);
      humidifierPulseRunning = false;
      Serial.println("Humidifier pulse finished");
    }
  }

  // Read DHT22 every 2 seconds
  if(millis() - lastSensorRead >= 2000)
  {
    lastSensorRead = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if(!isnan(h) && !isnan(t))
    {
      humidity = h;
      temperature = t;

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.print("%  Temperature: ");
      Serial.println(temperature);

      if(Blynk.connected())
      {
        Blynk.virtualWrite(V0, humidity);
        Blynk.virtualWrite(V1, temperature);
      }

      automaticControl();
    }
    else
    {
      Serial.println("DHT22 Read Failed");
    }
  }
}
/******************************************************
 * Production Room Weather Automation
 * ESP32 + DHT22 + Blynk IoT
 *
 * V0 = Humidity
 * V1 = Temperature
 * V2 = Humidifier Switch
 * V3 = Exhaust Fan Switch
 * V4 = Humidifier Status
 * V5 = Exhaust Status
 * V6 = Auto / Manual Mode
 *
 * FIXES in this version:
 *  1. Humidifier is a push-button (toggle) device, not a latching
 *     relay load -> the pulse that "presses the button" is now
 *     SHORT (default 250ms) instead of a 2-second hold. Holding
 *     the contact closed for 2s was being read by the humidifier's
 *     own controller as more than one press (it would toggle back
 *     ON right after appearing to turn OFF).
 *  2. WiFi reconnect logic added: the ESP32 radio is explicitly
 *     checked and reconnected if it drops, independent of Blynk's
 *     own reconnect handling.
 ******************************************************/

#define BLYNK_TEMPLATE_ID "TMPL6D-iDTot4"
#define BLYNK_TEMPLATE_NAME "Production Room Weather Automation"
#define BLYNK_AUTH_TOKEN "YlnUALDiIeOUmFndKoLgGcPglt2zYMi6"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

//================ WIFI =================

char ssid[] = "Arham";
char pass[] = "1211263947";

//================ SENSOR ===============

#define DHT_PIN 15
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

//================ OUTPUT ===============

#define HUMIDIFIER_PIN 4
#define EXHAUST_PIN 5

//================ LIMITS ===============

#define HUMI_LOW 60
#define HUMI_HIGH 65

// How long to hold the relay closed to simulate ONE button press.
// Start at 250ms. If it still double-toggles, try lower (150ms).
// If it doesn't register the press at all, try higher (400-500ms).
#define HUMI_PULSE_MS 250

//================ VARIABLES ============

float humidity = 0;
float temperature = 0;

bool autoMode = true;

bool humidifierState = false;
bool exhaustState = false;

bool humidifierPulseRunning = false;
unsigned long pulseStart = 0;

unsigned long lastSensorRead = 0;

//================ WIFI RECONNECT STATE =================

unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000; // check every 5s

//================================================
// Humidifier pulse (short tap, not a hold)
//================================================

void humidifierPulse(bool state)
{
  humidifierState = state;

  digitalWrite(HUMIDIFIER_PIN, HIGH);

  humidifierPulseRunning = true;
  pulseStart = millis();

  Serial.print("Humidifier ");
  Serial.println(state ? "ON" : "OFF");

  if(Blynk.connected())
  {
    Blynk.virtualWrite(V4, state ? "ON" : "OFF");
    Blynk.virtualWrite(V2, state);
  }
}

//================================================
// Exhaust control
//================================================

void setExhaust(bool state)
{
  exhaustState = state;

  digitalWrite(EXHAUST_PIN, state);

  Serial.print("Exhaust ");
  Serial.println(state ? "ON" : "OFF");

  if(Blynk.connected())
  {
    Blynk.virtualWrite(V5, state ? "Running" : "Stopped");
    Blynk.virtualWrite(V3, state);
  }
}

//================================================
// AUTO CONTROL
//================================================

void automaticControl()
{
  if(!autoMode)
    return;

  // Low humidity
  if(humidity < HUMI_LOW)
  {
    if(!humidifierState && !humidifierPulseRunning)
    {
      humidifierPulse(true);
    }

    if(exhaustState)
    {
      setExhaust(false);
    }
  }

  // High humidity
  else if(humidity > HUMI_HIGH)
  {
    if(humidifierState && !humidifierPulseRunning)
    {
      humidifierPulse(false);
    }

    if(!exhaustState)
    {
      setExhaust(true);
    }
  }
}

//================================================
// V2 Humidifier Switch
//================================================

BLYNK_WRITE(V2)
{
  if(autoMode)
  {
    Blynk.virtualWrite(V2, 0);
    return;
  }

  bool command = param.asInt();

  if(command != humidifierState && !humidifierPulseRunning)
  {
    humidifierPulse(command);
  }
}

//================================================
// V3 Exhaust Switch
//================================================

BLYNK_WRITE(V3)
{
  if(autoMode)
  {
    Blynk.virtualWrite(V3, exhaustState);
    return;
  }

  bool command = param.asInt();
  setExhaust(command);
}

//================================================
// V6 Auto Manual Switch
//================================================

BLYNK_WRITE(V6)
{
  autoMode = param.asInt();

  if(autoMode)
  {
    Serial.println("AUTO MODE ENABLED");
    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
  }
  else
  {
    Serial.println("MANUAL MODE ENABLED");

    if(humidifierState && !humidifierPulseRunning)
    {
      humidifierPulse(false);
    }

    if(exhaustState)
    {
      setExhaust(false);
    }

    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
  }
}

//================================================
// Blynk Connected
//================================================

BLYNK_CONNECTED()
{
  Serial.println("Blynk Connected");

  Blynk.syncVirtual(V6);

  if(!autoMode)
  {
    Blynk.syncVirtual(V2);
    Blynk.syncVirtual(V3);
  }

  Blynk.virtualWrite(V4, humidifierState ? "ON" : "OFF");
  Blynk.virtualWrite(V5, exhaustState ? "Running" : "Stopped");
}

//================================================
// WiFi + Blynk connection management
//================================================

void connectWiFi()
{
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);

  unsigned long startAttempt = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000)
  {
    delay(300);
    Serial.print(".");
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected");
  }
  else
  {
    Serial.println("\nWiFi connect failed, will retry in loop()");
  }
}

// Called periodically from loop() to make sure both WiFi and
// Blynk stay connected, and to recover automatically if either drops.
void maintainConnection()
{
  if(millis() - lastWifiCheck < WIFI_CHECK_INTERVAL)
    return;

  lastWifiCheck = millis();

  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
    return; // wait for WiFi before trying Blynk
  }

  if(!Blynk.connected())
  {
    Serial.println("WiFi OK but Blynk disconnected, reconnecting Blynk...");
    Blynk.connect(); // non-blocking attempt
  }
}

//================================================
// SETUP
//================================================

void setup()
{
  Serial.begin(115200);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(EXHAUST_PIN, OUTPUT);

  digitalWrite(HUMIDIFIER_PIN, LOW);
  digitalWrite(EXHAUST_PIN, LOW);

  dht.begin();

  delay(2000);

  connectWiFi();

  Blynk.config(BLYNK_AUTH_TOKEN);
  if(WiFi.status() == WL_CONNECTED)
  {
    Blynk.connect(5000);
  }

  Serial.println("System Ready");
}

//================================================
// LOOP
//================================================

void loop()
{
  maintainConnection();

  if(Blynk.connected())
  {
    Blynk.run();
  }

  // Finish humidifier pulse (short tap release)
  if(humidifierPulseRunning)
  {
    if(millis() - pulseStart >= HUMI_PULSE_MS)
    {
      digitalWrite(HUMIDIFIER_PIN, LOW);
      humidifierPulseRunning = false;
      Serial.println("Humidifier pulse finished");
    }
  }

  // Read DHT22 every 2 seconds
  if(millis() - lastSensorRead >= 2000)
  {
    lastSensorRead = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if(!isnan(h) && !isnan(t))
    {
      humidity = h;
      temperature = t;

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.print("%  Temperature: ");
      Serial.println(temperature);

      if(Blynk.connected())
      {
        Blynk.virtualWrite(V0, humidity);
        Blynk.virtualWrite(V1, temperature);
      }

      automaticControl();
    }
    else
    {
      Serial.println("DHT22 Read Failed");
    }
  }
}
