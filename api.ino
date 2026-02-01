/*
 * api.ino - Intervals.icu API Functions
 * 
 * Contains:
 * - API authentication
 * - Wellness data fetching
 * - Form calculation (CTL - ATL)
 * - JSON parsing
 */

// ==================== Authentication ====================
String getBasicAuthHeader() {
    String credentials = "API_KEY:" + apiKey;
    String encoded = base64::encode(credentials);
    return "Basic " + encoded;
}

// ==================== Data Fetching ====================
bool fetchWellnessData(String oldest, String newest, String& response) {
    if (!apiConfigured) {
        DEBUG_PRINTLN("[API] API key not configured");
        return false;
    }
    
    DEBUG_PRINTF("[API] Fetching wellness data from %s to %s\n", oldest.c_str(), newest.c_str());
    
    // Configure secure client
    secureClient.setInsecure();  // Skip certificate verification
    secureClient.setTimeout(30000);  // 30 second timeout
    
    HTTPClient http;
    http.setTimeout(30000);  // 30 second timeout
    http.setReuse(false);    // Don't reuse connection
    
    String url = "https://" + String(INTERVALS_API_HOST) + "/api/v1/athlete/" + athleteId + "/wellness?oldest=" + oldest + "&newest=" + newest;
    
    DEBUG_PRINTF("[API] URL: %s\n", url.c_str());
    
    http.begin(secureClient, url);
    http.addHeader("Authorization", getBasicAuthHeader());
    http.addHeader("Accept", "application/json");
    
    int httpCode = http.GET();
    DEBUG_PRINTF("[API] HTTP Response code: %d\n", httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        DEBUG_PRINTF("[API] Content-Length: %d (chunked if -1)\n", contentLength);
        
        // getString() should handle chunked encoding automatically
        response = http.getString();
        
        DEBUG_PRINTF("[API] Response length: %d bytes\n", response.length());
        
        // Debug: show first and last characters
        if (response.length() > 0) {
            DEBUG_PRINTF("[API] First char: '%c' (0x%02X)\n", response[0], (int)response[0]);
            DEBUG_PRINTF("[API] Last char: '%c' (0x%02X)\n", 
                        response[response.length()-1], (int)response[response.length()-1]);
            
            // Check for and remove any chunked encoding artifacts
            // Sometimes the hex chunk size leaks through
            int jsonStart = response.indexOf('[');
            int jsonEnd = response.lastIndexOf(']');
            
            if (jsonStart >= 0 && jsonEnd > jsonStart) {
                if (jsonStart > 0 || jsonEnd < (int)response.length() - 1) {
                    DEBUG_PRINTF("[API] Trimming response: jsonStart=%d, jsonEnd=%d\n", jsonStart, jsonEnd);
                    response = response.substring(jsonStart, jsonEnd + 1);
                    DEBUG_PRINTF("[API] Trimmed length: %d bytes\n", response.length());
                }
            }
        }
        
        http.end();
        
        // Validate it looks like JSON array
        bool valid = response.length() > 2 && response[0] == '[' && response[response.length()-1] == ']';
        if (!valid) {
            DEBUG_PRINTLN("[API] Response is not a valid JSON array!");
            DEBUG_PRINTF("[API] Preview: %.100s\n", response.c_str());
        }
        
        return valid;
    } else {
        DEBUG_PRINTF("[API] Error: %s\n", http.errorToString(httpCode).c_str());
        response = http.getString();
        DEBUG_PRINTLN("[API] Response: " + response);
        http.end();
        return false;
    }
}

// ==================== Get Today's Form ====================
bool getTodayForm() {
    String today = getTodayDate();
    DEBUG_PRINTF("[API] Fetching form for today: %s\n", today.c_str());
    
    String response;
    // Fetch only today's data
    if (!fetchWellnessData(today, today, response)) {
        return false;
    }
    
    // Parse JSON
    DynamicJsonDocument doc(2048);  // Small buffer - only one day of data
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        DEBUG_PRINTF("[API] JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    // The API returns an array of wellness records
    JsonArray arr = doc.as<JsonArray>();
    
    if (arr.size() == 0) {
        DEBUG_PRINTLN("[API] No wellness data for today");
        return false;
    }
    
    // Get today's entry (should be the only one)
    JsonObject latest = arr[arr.size() - 1];
    
    if (latest.containsKey("ctl") && latest.containsKey("atl")) {
        float rawCTL = latest["ctl"].as<float>();
        float rawATL = latest["atl"].as<float>();
        
        // Round to whole numbers before calculating Form (matches Python logic)
        currentCTL = round(rawCTL);
        currentATL = round(rawATL);
        currentForm = currentCTL - currentATL;  // Form = Fitness - Fatigue
        
        String date = latest["id"].as<String>();
        lastUpdateTime = date;
        
        DEBUG_PRINTF("[API] Date: %s, Raw CTL: %.1f, Raw ATL: %.1f\n", 
                     date.c_str(), rawCTL, rawATL);
        DEBUG_PRINTF("[API] Rounded CTL: %.0f, Rounded ATL: %.0f, Form: %.0f\n", 
                     currentCTL, currentATL, currentForm);
        return true;
    }
    
    DEBUG_PRINTLN("[API] No CTL/ATL data in response");
    return false;
}

// ==================== Get 30-Day History ====================
String getLast30DaysForm() {
    String today = getTodayDate();
    String oldest = getDateDaysAgo(29);  // 29 days ago + today = 30 days
    
    DEBUG_PRINTF("[API] Fetching 30 days of form: %s to %s\n", oldest.c_str(), today.c_str());
    
    String response;
    if (!fetchWellnessData(oldest, today, response)) {
        return "{\"error\": \"Failed to fetch data\"}";
    }
    
    DEBUG_PRINTF("[API] Response preview: %.100s...\n", response.c_str());
    
    // Check if response looks complete (should end with ])
    if (response.length() < 2 || response[response.length()-1] != ']') {
        DEBUG_PRINTLN("[API] Response appears truncated!");
        DEBUG_PRINTF("[API] Last chars: %s\n", response.substring(response.length() - 20).c_str());
        return "{\"error\": \"Truncated response\"}";
    }
    
    // Parse and extract form data
    // Buffer needs to be larger than response - ArduinoJson needs ~2x for overhead
    DynamicJsonDocument doc(12288);  // Increased buffer size
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        DEBUG_PRINTF("[API] JSON parse error: %s\n", error.c_str());
        DEBUG_PRINTF("[API] Response length was: %d\n", response.length());
        return "{\"error\": \"JSON parse error\"}";
    }
    
    // Free up the response string memory now that we've parsed it
    response = "";
    
    JsonArray arr = doc.as<JsonArray>();
    DEBUG_PRINTF("[API] Parsed %d wellness entries\n", arr.size());
    
    // Build a simple output string directly instead of using another JSON document
    String result = "[";
    bool first = true;
    
    for (size_t i = 0; i < arr.size(); i++) {
        JsonObject entry = arr[i];
        
        const char* date = entry["id"] | "unknown";
        float rawCtl = entry["ctl"] | 0.0;
        float rawAtl = entry["atl"] | 0.0;
        
        // Round to whole numbers before calculating Form (matches Python logic)
        int ctl = (int)round(rawCtl);
        int atl = (int)round(rawAtl);
        int form = ctl - atl;
        
        if (!first) result += ",";
        first = false;
        
        result += "{\"date\":\"" + String(date) + "\",\"ctl\":" + String(ctl) + 
                  ",\"atl\":" + String(atl) + ",\"form\":" + String(form) + "}";
        
        yield();  // Let ESP8266 breathe during string building
    }
    
    result += "]";
    
    DEBUG_PRINTF("[API] Returning %d days of form data (%d bytes)\n", arr.size(), result.length());
    return result;
}
