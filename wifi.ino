/*
 * wifi.ino - WiFi Functions
 * 
 * Contains:
 * - Network scanning
 * - AP mode setup
 * - WiFi connection with retry logic
 * - mDNS setup
 * - NTP time sync
 */

// ==================== WiFi Scanning ====================
String scanNetworks() {
    DEBUG_PRINTLN("[WiFi] Scanning networks...");
    int n = WiFi.scanNetworks();
    
    // Use a simple array to track unique SSIDs and their best RSSI
    // (ESP8266 has limited memory, so we cap at 20 unique networks)
    const int MAX_NETWORKS = 20;
    String uniqueSSIDs[MAX_NETWORKS];
    int bestRSSI[MAX_NETWORKS];
    bool isOpen[MAX_NETWORKS];
    int uniqueCount = 0;
    
    for (int i = 0; i < n && uniqueCount < MAX_NETWORKS; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        bool open = (WiFi.encryptionType(i) == ENC_TYPE_NONE);
        
        if (ssid.length() == 0) continue;  // Skip hidden networks
        
        // Check if we already have this SSID
        bool found = false;
        for (int j = 0; j < uniqueCount; j++) {
            if (uniqueSSIDs[j] == ssid) {
                // Keep the one with better signal
                if (rssi > bestRSSI[j]) {
                    bestRSSI[j] = rssi;
                    isOpen[j] = open;
                }
                found = true;
                break;
            }
        }
        
        if (!found) {
            uniqueSSIDs[uniqueCount] = ssid;
            bestRSSI[uniqueCount] = rssi;
            isOpen[uniqueCount] = open;
            uniqueCount++;
        }
    }
    
    // Build options HTML
    String options = "";
    for (int i = 0; i < uniqueCount; i++) {
        String enc = isOpen[i] ? "" : " *";
        options += "<option value=\"" + uniqueSSIDs[i] + "\">" + uniqueSSIDs[i] + " (" + String(bestRSSI[i]) + " dBm)" + enc + "</option>";
        DEBUG_PRINTF("[WiFi] Found: %s (%d dBm)%s\n", uniqueSSIDs[i].c_str(), bestRSSI[i], isOpen[i] ? " (open)" : "");
    }
    
    DEBUG_PRINTF("[WiFi] Found %d unique networks (from %d total)\n", uniqueCount, n);
    return options;
}

// ==================== AP Mode ====================
void startAPMode() {
    DEBUG_PRINTLN("[WiFi] Starting AP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);  // Open network, no password
    delay(100);  // Give AP time to start
    
    IPAddress ip = WiFi.softAPIP();
    DEBUG_PRINTF("[WiFi] AP started. SSID: %s (open), IP: %s\n", AP_SSID, ip.toString().c_str());
    
    // Start DNS server to redirect all domains to our IP (captive portal)
    dnsServer.start(DNS_PORT, "*", ip);
    DEBUG_PRINTLN("[DNS] Captive portal DNS started");
    
    isAPMode = true;
}

// ==================== WiFi Connection ====================
String getWiFiStatusString(int status) {
    switch (status) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "SSID not found";
        case WL_SCAN_COMPLETED:  return "Scan completed";
        case WL_CONNECTED:       return "Connected";
        case WL_CONNECT_FAILED:  return "Connection failed (wrong password?)";
        case WL_CONNECTION_LOST: return "Connection lost";
        case WL_DISCONNECTED:    return "Disconnected";
        default:                 return "Unknown (" + String(status) + ")";
    }
}

bool connectToWiFi() {
    if (wifiSSID.length() == 0) {
        DEBUG_PRINTLN("[WiFi] No SSID configured");
        return false;
    }
    
    const int MAX_RETRIES = 3;
    const int CONNECT_TIMEOUT = 30;  // seconds per attempt
    
    // Full WiFi reset before attempting connection
    DEBUG_PRINTLN("[WiFi] Resetting WiFi...");
    WiFi.persistent(false);  // Don't save WiFi config to flash
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Disable WiFi sleep for more reliable connection
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    
    // Set auto-reconnect
    WiFi.setAutoReconnect(true);
    
    for (int retry = 1; retry <= MAX_RETRIES; retry++) {
        DEBUG_PRINTF("[WiFi] Attempt %d/%d: Connecting to '%s' (pwd length: %d)...\n", 
                     retry, MAX_RETRIES, wifiSSID.c_str(), wifiPassword.length());
        
        WiFi.disconnect(true);  // Disconnect and clear stored credentials
        delay(500);
        
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
        
        int attempts = 0;
        int lastStatus = -1;
        while (WiFi.status() != WL_CONNECTED && attempts < (CONNECT_TIMEOUT * 2)) {
            delay(500);
            
            int status = WiFi.status();
            if (status != lastStatus) {
                DEBUG_PRINTF("\n[WiFi] Status changed: %s\n", getWiFiStatusString(status).c_str());
                lastStatus = status;
            } else {
                DEBUG_PRINT(".");
            }
            
            attempts++;
            yield();  // Let ESP8266 handle background tasks
            
            // Check for definitive failure states
            if (status == WL_NO_SSID_AVAIL) {
                DEBUG_PRINTLN("\n[WiFi] SSID not found - check network name");
                break;
            }
            if (status == WL_CONNECT_FAILED) {
                DEBUG_PRINTLN("\n[WiFi] Connection failed - check password");
                break;
            }
        }
        DEBUG_PRINTLN();
        
        if (WiFi.status() == WL_CONNECTED) {
            DEBUG_PRINTF("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            DEBUG_PRINTF("[WiFi] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
            DEBUG_PRINTF("[WiFi] Signal strength: %d dBm\n", WiFi.RSSI());
            DEBUG_PRINTF("[WiFi] Channel: %d\n", WiFi.channel());
            isAPMode = false;
            return true;
        }
        
        int status = WiFi.status();
        DEBUG_PRINTF("[WiFi] Attempt %d failed. Final status: %s\n", retry, getWiFiStatusString(status).c_str());
        
        if (retry < MAX_RETRIES) {
            DEBUG_PRINTLN("[WiFi] Waiting 3 seconds before retry...");
            WiFi.disconnect(true);
            delay(3000);
        }
    }
    
    DEBUG_PRINTLN("[WiFi] All connection attempts failed!");
    DEBUG_PRINTLN("[WiFi] Tips: 1) Verify password 2) Move closer to router 3) Check router allows new devices");
    return false;
}

// ==================== mDNS Setup ====================
void setupMDNS() {
    if (MDNS.begin(MDNS_NAME)) {
        DEBUG_PRINTF("[mDNS] Started: http://%s.local\n", MDNS_NAME);
        MDNS.addService("http", "tcp", 80);
    } else {
        DEBUG_PRINTLN("[mDNS] Failed to start!");
    }
}

// ==================== NTP Time Sync ====================
void setupNTP() {
    DEBUG_PRINTLN("[NTP] Configuring time...");
    configTime(gmtOffsetHours * 3600, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    // Wait for time to sync (max 10 seconds)
    int attempts = 0;
    time_t now = time(nullptr);
    while (now < 1700000000 && attempts < 20) {  // 1700000000 = Nov 2023
        delay(500);
        DEBUG_PRINT(".");
        now = time(nullptr);
        attempts++;
    }
    DEBUG_PRINTLN();
    
    if (now > 1700000000) {
        timeSync = true;
        struct tm* timeinfo = localtime(&now);
        DEBUG_PRINTF("[NTP] Time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                     timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                     timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        DEBUG_PRINTLN("[NTP] Time sync failed!");
    }
}

// ==================== Date Helper Functions ====================
String getTodayDate() {
    if (!timeSync) {
        DEBUG_PRINTLN("[Time] Warning: Time not synced, using fallback");
        return "2025-01-31";  // Fallback date
    }
    
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    return String(buffer);
}

String getDateDaysAgo(int daysAgo) {
    if (!timeSync) {
        DEBUG_PRINTLN("[Time] Warning: Time not synced, using fallback");
        return "2025-01-01";  // Fallback date
    }
    
    time_t now = time(nullptr);
    now -= (daysAgo * 24 * 60 * 60);  // Subtract days in seconds
    struct tm* timeinfo = localtime(&now);
    
    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    return String(buffer);
}
