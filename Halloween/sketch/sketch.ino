#include <WiFi.h>
#include <config.h>
#include "configuration/config.h"

#pragma region Configuration Variables
const int sonarTriggerPin = 33;
const int sonarEchoPin = 27;
const int relayPin = 15;
const int relayTriggerTime = 250;
const int minDistanceInch = 12;
const int maxDistanceInch = 240;
const int gracePeriod = 10 * 1000; // 10 seconds
#pragma endregion

#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701
#define LOG_ELASTIC_RESULT false
#define LOG_ELASTIC_REQUEST true

WiFiClient client;
bool isTriggered;
uint32_t triggeredAt;
bool triggeredByLoop;
long duration;
float distanceCm;
float distanceInch;
uint32_t dataLastSent;

void setup()
{
  configurePins();
  Serial.begin(115200); // Starts the serial communication
  configureWiFi();
}

void loop()
{
  digitalWrite(sonarTriggerPin, LOW);
  delayMicroseconds(2);
  digitalWrite(sonarTriggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(sonarTriggerPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(sonarEchoPin, HIGH);

  // Calculate the distance
  distanceCm = duration * SOUND_SPEED / 2;

  // Convert to inches
  distanceInch = distanceCm * CM_TO_INCH;

  if ((distanceInch < minDistanceInch || distanceInch > maxDistanceInch) && (millis() - triggeredAt > gracePeriod))
  {
    trigger(distanceInch, distanceCm);
    triggeredAt = millis();
  }
  else
  {
    sendData(false, distanceInch, distanceCm);
  }
  delay(50);
}

void configurePins()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(sonarTriggerPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(sonarEchoPin, INPUT);     // Sets the echoPin as an Input
}

void configureWiFi()
{
  WiFi.hostname(hostname);
  WiFi.begin(wifiSSID, wifiPassword);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("dnsIP: ");
  Serial.println(WiFi.dnsIP());
  Serial.print("gatewayIP: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("hostname: ");
  Serial.println(WiFi.getHostname());
  Serial.print("macAddress: ");
  Serial.println(WiFi.macAddress());
}

void sendData(bool fromTrigger, float distanceInch, float distanceCentimeter)
{
  uint32_t now = millis();
  if (fromTrigger || now - dataLastSent > elasticSearchReportInterval)
  {
    String state;
    String body = "{ \"state\": ";
    body += toString(isTriggered);
    body += ", \"isStateChange\": ";
    body += toString(fromTrigger);
    body += ", \"sensor\": \"" + (String)hostname + "\" ";
    body += ", \"distance.inch\": ";
    body += distanceInch;
    body += ", \"distance.centimeter\": ";
    body += distanceCentimeter;
    body += "}";
    int length = body.length();


    if (!client.connected())
    {
      Serial.println("Client is disconnected");
      Serial.println("Stopping the client");
      client.stop();
      Serial.println("Reconnecting");
      if (!client.connect(elasticSearchHost, elasticSearchPort))
      {
        Serial.println("connection failed");
        return;
      }
      Serial.println("Connected");
    }

    String headers = String("POST ") + "/" + elasticSearchIndex + "/_doc HTTP/1.1\r\n" +
                 "Host: " + elasticSearchHost + "\r\n" +
                 "Authorization: ApiKey " + elasticSearchApiKey + "\r\n" +
                 "Content-Type: application/json\r\n" +
                 "Content-Length: " + length + "\r\n\r\n";
    String message = headers + body;
#if LOG_ELASTIC_REQUEST
    Serial.println(message);
#endif
    size_t size = client.print(message);
#if LOG_ELASTIC_REQUEST
    Serial.print("** Bytes Sent: ");
    Serial.println(size);
#endif

    // wait for something to come back from elastic.
    while (!client.available())
    {
      delay(1);
    }

#if LOG_ELASTIC_RESULT
    while (client.available())
    {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }
    Serial.println();
#else
    // empty the bytes received buffer
    char buffer[1];
    while (client.available()) {
      client.readBytes(buffer, 1);
    }
#endif

    dataLastSent = millis();
  }
}

void trigger(float distanceInch, float distanceCentimeter)
{
  Serial.println("****** triggered");
  isTriggered = true;
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(relayPin, HIGH);
  sendData(true, distanceInch, distanceCentimeter);
  delay(relayTriggerTime);
  untrigger(distanceInch, distanceCentimeter);
}

void untrigger(float distanceInch, float distanceCentimeter)
{
  Serial.println("------ no longer triggered");
  isTriggered = false;
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(relayPin, LOW);
  sendData(true, distanceInch, distanceCentimeter);
}

String toString(boolean value)
{
  if (value == true)
  {
    return "true";
  }
  return "false";
}