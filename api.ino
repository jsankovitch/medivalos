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

bool fetchWellnessJson(const String& oldest, const String& newest, DynamicJsonDocument& doc) {
    if (!apiConfigured) {
        DEBUG_PRINTLN("[API] API key not configured");
        return false;
    }

    DEBUG_PRINTF("[API] Fetching wellness data from %s to %s\n", oldest.c_str(), newest.c_str());

    secureClient.setInsecure();
    secureClient.setTimeout(30000);

    HTTPClient http;
    http.setTimeout(30000);
    http.setReuse(false);

    String url = "https://" + String(INTERVALS_API_HOST) + "/api/v1/athlete/" + athleteId + "/wellness?oldest=" + oldest + "&newest=" + newest;
    DEBUG_PRINTF("[API] URL: %s\n", url.c_str());

    http.begin(secureClient, url);
    http.addHeader("Authorization", getBasicAuthHeader());
    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();
    DEBUG_PRINTF("[API] HTTP Response code: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        DEBUG_PRINTF("[API] Error: %s\n", http.errorToString(httpCode).c_str());
        String response = http.getString();
        DEBUG_PRINTLN("[API] Response: " + response);
        http.end();
        return false;
    }

    StaticJsonDocument<128> filter;
    filter[0]["id"] = true;
    filter[0]["ctl"] = true;
    filter[0]["atl"] = true;

    String response = http.getString();
    if (response.length() > 0) {
        int jsonStart = response.indexOf('[');
        int jsonEnd = response.lastIndexOf(']');
        if (jsonStart >= 0 && jsonEnd > jsonStart) {
            if (jsonStart > 0 || jsonEnd < (int)response.length() - 1) {
                DEBUG_PRINTF("[API] Trimming response: jsonStart=%d, jsonEnd=%d\n", jsonStart, jsonEnd);
                response = response.substring(jsonStart, jsonEnd + 1);
            }
        }
    }

    DeserializationError error = deserializeJson(doc, response, DeserializationOption::Filter(filter));
    if (error) {
        DEBUG_PRINTF("[API] JSON parse error: %s\n", error.c_str());
        http.end();
        return false;
    }

    http.end();
    return true;
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
    return refreshFormHistory();
}

// ==================== Get 30-Day History ====================
bool refreshFormHistory() {
    String today = getTodayDate();
    String oldest = getDateDaysAgo(29);  // 29 days ago + today = 30 days
    
    DEBUG_PRINTF("[API] Fetching 30 days of form: %s to %s\n", oldest.c_str(), today.c_str());
    
    DynamicJsonDocument doc(16384);
    if (!fetchWellnessJson(oldest, today, doc)) {
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    DEBUG_PRINTF("[API] Parsed %d wellness entries\n", arr.size());

    formHistoryCount = 0;
    for (size_t i = 0; i < arr.size() && formHistoryCount < FORM_HISTORY_DAYS; i++) {
        JsonObject entry = arr[i];
        const char* date = entry["id"] | "unknown";
        float rawCtl = entry["ctl"] | 0.0;
        float rawAtl = entry["atl"] | 0.0;

        int ctl = (int)round(rawCtl);
        int atl = (int)round(rawAtl);
        int form = ctl - atl;

        formHistory[formHistoryCount].date = String(date);
        formHistory[formHistoryCount].ctl = ctl;
        formHistory[formHistoryCount].atl = atl;
        formHistory[formHistoryCount].form = form;
        formHistoryCount++;
        yield();
    }

    if (formHistoryCount == 0) {
        DEBUG_PRINTLN("[API] No wellness data returned");
        return false;
    }

    FormEntry latest = formHistory[formHistoryCount - 1];
    currentCTL = latest.ctl;
    currentATL = latest.atl;
    currentForm = latest.form;
    lastUpdateTime = latest.date;

    DEBUG_PRINTF("[API] Latest date: %s, CTL: %d, ATL: %d, Form: %d\n",
                 latest.date.c_str(), latest.ctl, latest.atl, latest.form);
    DEBUG_PRINTF("[API] Stored %d days of form data\n", formHistoryCount);
    return true;
}

void printCurrentFormToSerial() {
    DEBUG_PRINTLN("[Serial] Today's Form");
    DEBUG_PRINTF("%s - Form: %d\n", lastUpdateTime.c_str(), (int)currentForm);
}

void printFormHistoryToSerial() {
    DEBUG_PRINTLN("[Serial] 30-Day Form History (Oldest -> Newest)");
    for (int i = 0; i < formHistoryCount; i++) {
        DEBUG_PRINTF("%s - Form: %d\n", formHistory[i].date.c_str(), formHistory[i].form);
        yield();
    }
}
