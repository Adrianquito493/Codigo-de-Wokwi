#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ======================== CONFIGURACIÓN WIFI (WOKWI) ========================
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ======================== CONFIGURACIÓN MQTT ========================
const char* mqtt_server = "broker.hivemq.com";  
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_Parking_DualSpace_2025";

// Topics MQTT
const char* topic_status = "estacionamiento/status";
const char* topic_espacio1 = "estacionamiento/espacio1";
const char* topic_espacio2 = "estacionamiento/espacio2";
const char* topic_iluminacion = "estacionamiento/iluminacion";
const char* topic_barrera = "estacionamiento/barrera";
const char* topic_comando = "estacionamiento/comando";

// ======================== PINES ESP32 ========================
// Espacio 1
#define TRIG_PIN_1 26
#define ECHO_PIN_1 27
#define LED_GREEN_1 33
#define LED_RED_1 32

// Espacio 2
#define TRIG_PIN_2 16
#define ECHO_PIN_2 17
#define LED_GREEN_2 19
#define LED_RED_2 23

// Sistemas generales
#define SERVO_PIN 18
#define BUTTON_PIN 4
#define LDR_PIN 34

// Iluminación
#define LIGHT_LED_1 25
#define LIGHT_LED_2 15

// ======================== CONFIGURACIÓN OLED ========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ======================== OBJETOS ========================
WiFiClient espClient;
PubSubClient client(espClient);
Servo barrierServo;

// ======================== VARIABLES DEL SISTEMA ========================
int totalSpaces = 2;
int occupiedSpaces = 0;
int freeSpaces = 2;
bool space1Occupied = false;
bool space2Occupied = false;
bool lastButtonState = HIGH;
bool barrierOpen = false;
bool lightsOn = false;
bool manualLightControl = false;

// Variables para control de cambios
bool lastSpace1Occupied = false;
bool lastSpace2Occupied = false;
bool lastBarrierOpen = false;
bool lastLightsOn = false;
bool lastManualLightControl = false;
int lastLightLevel = -1;

unsigned long lastSensorRead = 0;
unsigned long lastMQTTSend = 0;
unsigned long lastLDRRead = 0;
unsigned long lastThingSpeakSend = 0;  // NUEVO: Control específico para ThingSpeak

// Intervalos optimizados
const unsigned long sensorInterval = 300;      // Leer sensores cada 300ms (más rápido)
const unsigned long mqttInterval = 2000;       // MQTT cada 2s para respuesta rápida
const unsigned long ldrInterval = 1000;        // LDR cada 1s
const unsigned long thingSpeakInterval = 20000; // ThingSpeak cada 20s (MÍNIMO PARA THINGSPEAK)

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  Serial.println("🚗🚗 Sistema de Estacionamiento Dual OPTIMIZADO - ESP32 + Node-RED");
  
  // Configurar pines - Espacio 1
  pinMode(TRIG_PIN_1, OUTPUT);
  pinMode(ECHO_PIN_1, INPUT);
  pinMode(LED_GREEN_1, OUTPUT);
  pinMode(LED_RED_1, OUTPUT);
  
  // Configurar pines - Espacio 2
  pinMode(TRIG_PIN_2, OUTPUT);
  pinMode(ECHO_PIN_2, INPUT);
  pinMode(LED_GREEN_2, OUTPUT);
  pinMode(LED_RED_2, OUTPUT);
  
  // Pines generales
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LDR_PIN, INPUT);
  pinMode(LIGHT_LED_1, OUTPUT);
  pinMode(LIGHT_LED_2, OUTPUT);
  
  // Inicializar servo
  barrierServo.attach(SERVO_PIN);
  barrierServo.write(0);
  
  // Inicializar OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("❌ Error al inicializar OLED"));
  } else {
    Serial.println("✅ OLED inicializado");
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Conectar WiFi
  setup_wifi();
  
  // Configurar MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  // Estado inicial
  updateDisplay();
  updateLEDs();
  updateLights();
  
  // Inicializar variables de cambio
  lastSpace1Occupied = space1Occupied;
  lastSpace2Occupied = space2Occupied;
  lastBarrierOpen = barrierOpen;
  lastLightsOn = lightsOn;
  lastManualLightControl = manualLightControl;
  
  Serial.println("✅ Sistema optimizado inicializado");
  Serial.println("📡 MQTT Dashboard: cada 2s");
  Serial.println("📊 ThingSpeak: cada 20s o al cambiar");
}

// ======================== CONFIGURACIÓN WIFI ========================
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("✅ WiFi conectado");
  Serial.print("📡 IP address: ");
  Serial.println(WiFi.localIP());
}

// ======================== CALLBACK MQTT ========================
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("📨 Comando recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  if (String(topic) == topic_comando) {
    if (message == "ABRIR_BARRERA") {
      openBarrier();
      Serial.println("🎛️ Barrera abierta desde Node-RED");
      sendDataToNodeRED();  // Enviar inmediatamente
    } 
    else if (message == "CERRAR_BARRERA") {
      closeBarrier();
      Serial.println("🎛️ Barrera cerrada desde Node-RED");
      sendDataToNodeRED();
    } 
    else if (message == "TOGGLE_BARRERA") {
      if (barrierOpen) {
        closeBarrier();
      } else {
        openBarrier();
      }
      Serial.println("🎛️ Barrera conmutada desde Node-RED");
      sendDataToNodeRED();
    }
    else if (message == "RESET_SISTEMA") {
      space1Occupied = false;
      space2Occupied = false;
      occupiedSpaces = 0;
      freeSpaces = totalSpaces;
      closeBarrier();
      manualLightControl = false;
      updateDisplay();
      updateLEDs();
      updateLights();
      Serial.println("🔄 Sistema reseteado desde Node-RED");
      sendDataToNodeRED();
    }
    else if (message == "ENCENDER_LUCES") {
      manualLightControl = true;
      lightsOn = true;
      digitalWrite(LIGHT_LED_1, HIGH);
      digitalWrite(LIGHT_LED_2, HIGH);
      Serial.println("💡 Luces ENCENDIDAS manualmente desde Node-RED");
      sendDataToNodeRED();
    }
    else if (message == "APAGAR_LUCES") {
      manualLightControl = true;
      lightsOn = false;
      digitalWrite(LIGHT_LED_1, LOW);
      digitalWrite(LIGHT_LED_2, LOW);
      Serial.println("💡 Luces APAGADAS manualmente desde Node-RED");
      sendDataToNodeRED();
    }
    else if (message == "MODO_AUTOMATICO") {
      manualLightControl = false;
      updateLights();
      Serial.println("🤖 Luces en MODO AUTOMÁTICO desde Node-RED");
      sendDataToNodeRED();
    }
    else if (message == "TOGGLE_LUCES") {
      manualLightControl = true;
      lightsOn = !lightsOn;
      if (lightsOn) {
        digitalWrite(LIGHT_LED_1, HIGH);
        digitalWrite(LIGHT_LED_2, HIGH);
        Serial.println("💡 Luces ENCENDIDAS (toggle) desde Node-RED");
      } else {
        digitalWrite(LIGHT_LED_1, LOW);
        digitalWrite(LIGHT_LED_2, LOW);
        Serial.println("💡 Luces APAGADAS (toggle) desde Node-RED");
      }
      sendDataToNodeRED();
    }
  }
}

// ======================== RECONEXIÓN MQTT ========================
void reconnect() {
  while (!client.connected()) {
    Serial.print("🔄 Conectando a MQTT...");
    
    if (client.connect(mqtt_client_id)) {
      Serial.println(" ✅ MQTT conectado!");
      client.subscribe(topic_comando);
      Serial.println("🎧 Suscrito a comandos desde Node-RED");
    } else {
      Serial.print(" ❌ Error MQTT: ");
      Serial.print(client.state());
      Serial.println(" - Reintentando en 5 segundos");
      delay(5000);
    }
  }
}

// ======================== SENSOR ULTRASÓNICO CON FILTRO ========================
long readUltrasonicDistance(int trigPin, int echoPin) {
  // Leer múltiples veces para mayor estabilidad
  long total = 0;
  int validReadings = 0;
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    
    long duration = pulseIn(echoPin, HIGH, 30000); // Timeout 30ms
    if (duration > 0) {
      long distance = duration * 0.034 / 2;
      if (distance > 0 && distance < 400) { // Filtrar lecturas válidas
        total += distance;
        validReadings++;
      }
    }
    delay(10);
  }
  
  if (validReadings > 0) {
    return total / validReadings; // Promedio
  } else {
    return -1; // Error de lectura
  }
}

// ======================== DETECTAR CAMBIOS ========================
bool hasDataChanged() {
  return (space1Occupied != lastSpace1Occupied ||
          space2Occupied != lastSpace2Occupied ||
          barrierOpen != lastBarrierOpen ||
          lightsOn != lastLightsOn ||
          manualLightControl != lastManualLightControl);
}

void updateLastStates() {
  lastSpace1Occupied = space1Occupied;
  lastSpace2Occupied = space2Occupied;
  lastBarrierOpen = barrierOpen;
  lastLightsOn = lightsOn;
  lastManualLightControl = manualLightControl;
}

// ======================== ACTUALIZAR LEDs ========================
void updateLEDs() {
  // Espacio 1
  if (space1Occupied) {
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, HIGH);
  } else {
    digitalWrite(LED_GREEN_1, HIGH);
    digitalWrite(LED_RED_1, LOW);
  }
  
  // Espacio 2
  if (space2Occupied) {
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, HIGH);
  } else {
    digitalWrite(LED_GREEN_2, HIGH);
    digitalWrite(LED_RED_2, LOW);
  }
}

// ======================== ACTUALIZAR ILUMINACIÓN ========================
void updateLights() {
  int lightLevel = analogRead(LDR_PIN);
  
  if (manualLightControl) {
    if (lightsOn) {
      digitalWrite(LIGHT_LED_1, HIGH);
      digitalWrite(LIGHT_LED_2, HIGH);
    } else {
      digitalWrite(LIGHT_LED_1, LOW);
      digitalWrite(LIGHT_LED_2, LOW);
    }
  } else {
    if (lightLevel < 1000) {
      digitalWrite(LIGHT_LED_1, HIGH);
      digitalWrite(LIGHT_LED_2, HIGH);
      lightsOn = true;
    } else {
      digitalWrite(LIGHT_LED_1, LOW);
      digitalWrite(LIGHT_LED_2, LOW);
      lightsOn = false;
    }
  }
}

// ======================== ACTUALIZAR DISPLAY ========================
void updateDisplay() {
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("PARKING");
  
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Libres: ");
  display.print(freeSpaces);
  display.print("/");
  display.println(totalSpaces);
  
  display.setCursor(0, 30);
  display.print("E1: ");
  display.print(space1Occupied ? "OCUPADO" : "LIBRE");
  
  display.setCursor(0, 40);
  display.print("E2: ");
  display.print(space2Occupied ? "OCUPADO" : "LIBRE");
  
  display.setCursor(0, 50);
  display.print("Barrera: ");
  display.println(barrierOpen ? "ABIERTA" : "CERRADA");
  
  display.display();
}

// ======================== CONTROL DE BARRERA ========================
void openBarrier() {
  barrierServo.write(90);
  barrierOpen = true;
  Serial.println("🚪 Barrera ABIERTA");
  updateDisplay();
}

void closeBarrier() {
  barrierServo.write(0);
  barrierOpen = false;
  Serial.println("🚪 Barrera CERRADA");
  updateDisplay();
}

// ======================== ENVIAR DATOS A NODE-RED ========================
void sendDataToNodeRED() {
  if (client.connected()) {
    int lightLevel = analogRead(LDR_PIN);
    
    // 1. Estado general - SIEMPRE con valores válidos
    StaticJsonDocument<300> statusDoc;
    statusDoc["espacios_libres"] = freeSpaces;
    statusDoc["espacios_ocupados"] = occupiedSpaces;
    statusDoc["total_espacios"] = totalSpaces;
    statusDoc["porcentaje_ocupacion"] = (totalSpaces > 0) ? ((float)occupiedSpaces / totalSpaces * 100.0) : 0.0;
    statusDoc["barrera_abierta"] = barrierOpen;
    statusDoc["nivel_luz"] = (lightLevel >= 0) ? lightLevel : 0;
    statusDoc["luces_encendidas"] = (digitalRead(LIGHT_LED_1) == HIGH);
    statusDoc["timestamp"] = millis();
    
    String statusJson;
    serializeJson(statusDoc, statusJson);
    client.publish(topic_status, statusJson.c_str());
    
    // 2. Estado Espacio 1
    StaticJsonDocument<150> space1Doc;
    space1Doc["espacio"] = 1;
    space1Doc["ocupado"] = space1Occupied;
    space1Doc["estado"] = space1Occupied ? "OCUPADO" : "LIBRE";
    
    String space1Json;
    serializeJson(space1Doc, space1Json);
    client.publish(topic_espacio1, space1Json.c_str());
    
    // 3. Estado Espacio 2
    StaticJsonDocument<150> space2Doc;
    space2Doc["espacio"] = 2;
    space2Doc["ocupado"] = space2Occupied;
    space2Doc["estado"] = space2Occupied ? "OCUPADO" : "LIBRE";
    
    String space2Json;
    serializeJson(space2Doc, space2Json);
    client.publish(topic_espacio2, space2Json.c_str());
    
    // 4. Estado de iluminación
    StaticJsonDocument<150> lightDoc;
    lightDoc["nivel_luz"] = (lightLevel >= 0) ? lightLevel : 0;
    lightDoc["es_de_noche"] = (lightLevel < 1000);
    lightDoc["luces_encendidas"] = (digitalRead(LIGHT_LED_1) == HIGH);
    lightDoc["modo_manual"] = manualLightControl;
    lightDoc["modo"] = manualLightControl ? "MANUAL" : "AUTOMATICO";
    
    String lightJson;
    serializeJson(lightDoc, lightJson);
    client.publish(topic_iluminacion, lightJson.c_str());
    
    // 5. Estado de barrera
    StaticJsonDocument<100> barrierDoc;  
    barrierDoc["abierta"] = barrierOpen;
    barrierDoc["estado"] = barrierOpen ? "ABIERTA" : "CERRADA";
    barrierDoc["angulo"] = barrierOpen ? 90 : 0;
    
    String barrierJson;
    serializeJson(barrierDoc, barrierJson);
    client.publish(topic_barrera, barrierJson.c_str());
    
    Serial.println("📤 Datos enviados a Node-RED (Dashboard)");
  }
}

// ======================== LOOP PRINCIPAL OPTIMIZADO ========================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  unsigned long currentTime = millis();
  bool dataChanged = false;
  
  // ======================== LEER SENSORES ULTRASÓNICOS ========================
  if (currentTime - lastSensorRead >= sensorInterval) {
    long distance1 = readUltrasonicDistance(TRIG_PIN_1, ECHO_PIN_1);
    long distance2 = readUltrasonicDistance(TRIG_PIN_2, ECHO_PIN_2);
    
    if (distance1 > 0 && distance2 > 0) { // Solo procesar si las lecturas son válidas
      Serial.println("📏 Distancias - E1: " + String(distance1) + "cm, E2: " + String(distance2) + "cm");
      
      // Detectar cambios en Espacio 1
      bool newSpace1State = (distance1 < 10);
      if (newSpace1State != space1Occupied) {
        space1Occupied = newSpace1State;
        Serial.println("🚗 CAMBIO Espacio 1: " + String(space1Occupied ? "OCUPADO" : "LIBRE"));
        dataChanged = true;
      }
      
      // Detectar cambios en Espacio 2  
      bool newSpace2State = (distance2 < 10);
      if (newSpace2State != space2Occupied) {
        space2Occupied = newSpace2State;
        Serial.println("🚗 CAMBIO Espacio 2: " + String(space2Occupied ? "OCUPADO" : "LIBRE"));
        dataChanged = true;
      }
      
      // Actualizar contadores si hubo cambios
      if (dataChanged) {
        occupiedSpaces = 0;
        if (space1Occupied) occupiedSpaces++;
        if (space2Occupied) occupiedSpaces++;
        freeSpaces = totalSpaces - occupiedSpaces;
        
        updateDisplay();
        updateLEDs();
        sendDataToNodeRED(); // Enviar inmediatamente al cambiar
      }
    }
    
    lastSensorRead = currentTime;
  }
  
  // ======================== LEER BOTÓN FÍSICO ========================
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    delay(50); // Debounce
    
    if (barrierOpen) {
      closeBarrier();
    } else {
      openBarrier();
    }
    
    dataChanged = true;
    sendDataToNodeRED();
  }
  lastButtonState = currentButtonState;
  
  // ======================== LEER SENSOR DE LUZ ========================
  if (currentTime - lastLDRRead >= ldrInterval) {
    int currentLightLevel = analogRead(LDR_PIN);
    
    // Solo actualizar si hay cambio significativo (más de 50 unidades)
    if (abs(currentLightLevel - lastLightLevel) > 50 || lastLightLevel == -1) {
      updateLights();
      lastLightLevel = currentLightLevel;
      if (hasDataChanged()) {
        dataChanged = true;
      }
    }
    
    lastLDRRead = currentTime;
  }
  
  // ======================== ENVIAR DATOS PERIÓDICAMENTE (DASHBOARD) ========================
  if (currentTime - lastMQTTSend >= mqttInterval) {
    sendDataToNodeRED(); // Para mantener dashboard actualizado
    lastMQTTSend = currentTime;
  }
  
  // ======================== ENVIAR A THINGSPEAK (SOLO CADA 20s O AL CAMBIAR) ========================
  if (dataChanged || (currentTime - lastThingSpeakSend >= thingSpeakInterval)) {
    if (currentTime - lastThingSpeakSend >= 15000) { // Mínimo 15s entre envíos
      // Publicar un mensaje especial para ThingSpeak
      sendDataToNodeRED(); // Esto activará el flujo a ThingSpeak
      lastThingSpeakSend = currentTime;
      Serial.println("📊 Enviando a ThingSpeak...");
    }
  }
  
  // Actualizar estados anteriores
  if (dataChanged) {
    updateLastStates();
  }
  
  delay(50); // Reducir delay para mayor responsividad
}