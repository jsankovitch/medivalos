/*
 * MedivalOS - ESP8266 Intervals.icu Form Display
 * 
 * Features:
 * - WiFi Manager (AP mode for configuration)
 * - mDNS at medivalos.local
 * - Intervals.icu API integration
 * - Form calculation and display
 * - Serial debugging
 * 
 * Hardware: ESP8266 (NodeMCU, Wemos D1, etc.)
 * 
 * File Structure:
 * - medivalos.ino  : Main file (config, globals, setup, loop)
 * - wifi.ino       : WiFi connection, AP mode, scanning
 * - api.ino        : Intervals.icu API calls, JSON parsing
 * - web.ino        : Web server handlers, HTML pages
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <time.h>

// ==================== Configuration ====================
#define AP_SSID "MedivalOS-Setup"
#define MDNS_NAME "medivalos"
#define EEPROM_SIZE 512

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (-6 * 3600)  // CST (UTC-6) - adjust for your timezone
#define DAYLIGHT_OFFSET_SEC 0        // Set to 3600 if DST is active

// EEPROM Memory Layout
#define EEPROM_WIFI_SSID_ADDR 0
#define EEPROM_WIFI_SSID_LEN 32
#define EEPROM_WIFI_PASS_ADDR 32
#define EEPROM_WIFI_PASS_LEN 64
#define EEPROM_API_KEY_ADDR 96
#define EEPROM_API_KEY_LEN 64
#define EEPROM_ATHLETE_ID_ADDR 160
#define EEPROM_ATHLETE_ID_LEN 32
#define EEPROM_CONFIGURED_FLAG_ADDR 192

// Intervals.icu API
#define INTERVALS_API_HOST "intervals.icu"
#define INTERVALS_API_PORT 443

// ==================== Global Variables ====================
ESP8266WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure secureClient;

const byte DNS_PORT = 53;

// Stored credentials
String wifiSSID = "";
String wifiPassword = "";
String apiKey = "";
String athleteId = "0";  // "0" means use the authenticated user

// State
bool wifiConfigured = false;
bool apiConfigured = false;
bool isAPMode = false;
bool timeSync = false;

// Latest form data
float currentForm = 0.0;
float currentCTL = 0.0;  // Fitness
float currentATL = 0.0;  // Fatigue
String lastUpdateTime = "Never";

// ==================== Debug Macros ====================
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)

// ==================== EEPROM Functions ====================
void setupEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    DEBUG_PRINTLN("[EEPROM] Initialized");
}

void writeStringToEEPROM(int addr, String data, int maxLen) {
    int len = min((int)data.length(), maxLen - 1);
    for (int i = 0; i < len; i++) {
        EEPROM.write(addr + i, data[i]);
    }
    EEPROM.write(addr + len, '\0');
    EEPROM.commit();
    DEBUG_PRINTF("[EEPROM] Wrote %d bytes at addr %d\n", len, addr);
}

String readStringFromEEPROM(int addr, int maxLen) {
    String data = "";
    char c;
    for (int i = 0; i < maxLen; i++) {
        c = EEPROM.read(addr + i);
        if (c == '\0' || c == 255) break;
        data += c;
    }
    return data;
}

void saveWiFiCredentials(String ssid, String password) {
    writeStringToEEPROM(EEPROM_WIFI_SSID_ADDR, ssid, EEPROM_WIFI_SSID_LEN);
    writeStringToEEPROM(EEPROM_WIFI_PASS_ADDR, password, EEPROM_WIFI_PASS_LEN);
    EEPROM.write(EEPROM_CONFIGURED_FLAG_ADDR, 1);
    EEPROM.commit();
    DEBUG_PRINTLN("[EEPROM] WiFi credentials saved");
}

void saveAPIKey(String key, String athId) {
    writeStringToEEPROM(EEPROM_API_KEY_ADDR, key, EEPROM_API_KEY_LEN);
    writeStringToEEPROM(EEPROM_ATHLETE_ID_ADDR, athId, EEPROM_ATHLETE_ID_LEN);
    EEPROM.commit();
    DEBUG_PRINTLN("[EEPROM] API key saved");
}

void loadCredentials() {
    if (EEPROM.read(EEPROM_CONFIGURED_FLAG_ADDR) == 1) {
        wifiSSID = readStringFromEEPROM(EEPROM_WIFI_SSID_ADDR, EEPROM_WIFI_SSID_LEN);
        wifiPassword = readStringFromEEPROM(EEPROM_WIFI_PASS_ADDR, EEPROM_WIFI_PASS_LEN);
        wifiConfigured = (wifiSSID.length() > 0);
        DEBUG_PRINTF("[EEPROM] Loaded WiFi SSID: %s\n", wifiSSID.c_str());
    }
    
    apiKey = readStringFromEEPROM(EEPROM_API_KEY_ADDR, EEPROM_API_KEY_LEN);
    athleteId = readStringFromEEPROM(EEPROM_ATHLETE_ID_ADDR, EEPROM_ATHLETE_ID_LEN);
    if (athleteId.length() == 0) athleteId = "0";
    apiConfigured = (apiKey.length() > 0);
    DEBUG_PRINTF("[EEPROM] API configured: %s\n", apiConfigured ? "yes" : "no");
}

void clearAllCredentials() {
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    wifiSSID = "";
    wifiPassword = "";
    apiKey = "";
    athleteId = "0";
    wifiConfigured = false;
    apiConfigured = false;
    DEBUG_PRINTLN("[EEPROM] All credentials cleared");
}

// ==================== Setup ====================
void setup() {
    Serial.begin(115200);
    delay(100);
    
    DEBUG_PRINTLN("\n\n");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("  MedivalOS - Intervals.icu Form Tracker");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN();
    
    // Initialize EEPROM and load credentials
    setupEEPROM();
    loadCredentials();
    
    // Try to connect to WiFi
    if (wifiConfigured && connectToWiFi()) {
        // Connected - start in station mode
        setupNTP();  // Sync time first
        setupMDNS();
        
        // Setup web server routes for station mode
        server.on("/", HTTP_GET, handleRoot);
        server.on("/saveapi", HTTP_POST, handleSaveAPI);
        server.on("/reset", HTTP_POST, handleReset);
        server.on("/api/form", HTTP_GET, handleAPIForm);
        server.on("/api/history", HTTP_GET, handleAPIHistory);
        
    } else {
        // Not connected - start AP mode
        startAPMode();
        
        // Setup web server routes for AP mode
        server.on("/", HTTP_GET, handleAPRoot);
        server.on("/connect", HTTP_POST, handleConnect);
        
        // Captive portal detection URLs - redirect to setup page
        // Android
        server.on("/generate_204", HTTP_GET, handleAPRoot);
        server.on("/gen_204", HTTP_GET, handleAPRoot);
        // iOS / macOS
        server.on("/hotspot-detect.html", HTTP_GET, handleAPRoot);
        server.on("/library/test/success.html", HTTP_GET, handleAPRoot);
        // Windows
        server.on("/ncsi.txt", HTTP_GET, handleAPRoot);
        server.on("/connecttest.txt", HTTP_GET, handleAPRoot);
        server.on("/fwlink", HTTP_GET, handleAPRoot);
        // Generic
        server.on("/favicon.ico", HTTP_GET, []() {
            server.send(204, "text/plain", "");
        });
    }
    
    server.onNotFound(handleNotFound);
    server.begin();
    
    DEBUG_PRINTLN("[Web] Server started");
    DEBUG_PRINTLN();
    
    if (isAPMode) {
        DEBUG_PRINTLN("========================================");
        DEBUG_PRINTF("  Connect to WiFi: %s\n", AP_SSID);
        DEBUG_PRINTLN("  (Open network - no password)");
        DEBUG_PRINTLN("  Then open: http://192.168.4.1");
        DEBUG_PRINTLN("========================================");
    } else {
        DEBUG_PRINTLN("========================================");
        DEBUG_PRINTF("  Access: http://%s.local\n", MDNS_NAME);
        DEBUG_PRINTF("  Or: http://%s\n", WiFi.localIP().toString().c_str());
        DEBUG_PRINTLN("========================================");
    }
}

// ==================== Loop ====================
void loop() {
    // Process DNS requests in AP mode (for captive portal)
    if (isAPMode) {
        dnsServer.processNextRequest();
    }
    
    server.handleClient();
    
    if (!isAPMode) {
        MDNS.update();
    }
    
    // Add a small delay to prevent watchdog issues
    delay(1);
}
