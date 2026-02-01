/*
 * web.ino - Web Server Handlers
 * 
 * Contains:
 * - CSS styles (PROGMEM)
 * - AP mode pages (WiFi setup)
 * - Station mode pages (dashboard)
 * - API endpoints
 */

// ==================== CSS Styles ====================
// Stored in PROGMEM to save RAM
const char CSS_STYLES[] PROGMEM = R"rawliteral(
<style>
*{box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);color:#eee;min-height:100vh;margin:0;padding:20px}
.container{max-width:500px;margin:0 auto;background:rgba(255,255,255,0.05);border-radius:16px;padding:30px;box-shadow:0 8px 32px rgba(0,0,0,0.3)}
h1{text-align:center;color:#00d4ff;margin-bottom:10px}
h2{color:#00d4ff;border-bottom:1px solid #00d4ff33;padding-bottom:10px}
.subtitle{text-align:center;color:#888;margin-bottom:30px}
label{display:block;margin-bottom:5px;color:#aaa}
input,select{width:100%;padding:12px;margin-bottom:20px;border:1px solid #333;border-radius:8px;background:#1a1a2e;color:#fff;font-size:16px}
input:focus,select:focus{outline:none;border-color:#00d4ff}
button,.btn{width:100%;padding:14px;background:linear-gradient(135deg,#00d4ff,#0099cc);color:#000;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;margin-bottom:10px;text-decoration:none;display:block;text-align:center}
button:hover,.btn:hover{background:linear-gradient(135deg,#00e5ff,#00aadd)}
.btn-secondary{background:#333;color:#fff}
.btn-danger{background:linear-gradient(135deg,#ff4444,#cc0000);color:#fff}
.form-card{background:linear-gradient(135deg,#0f3460 0%,#16213e 100%);border-radius:16px;padding:30px;text-align:center;margin:20px 0}
.form-value{font-size:72px;font-weight:bold;color:#00d4ff}
.form-value.positive{color:#00ff88}
.form-value.negative{color:#ff4444}
.form-label{font-size:14px;color:#888;text-transform:uppercase;letter-spacing:2px}
.stats{display:flex;justify-content:space-around;margin-top:20px}
.stat{text-align:center}
.stat-value{font-size:24px;font-weight:bold;color:#fff}
.stat-label{font-size:12px;color:#888}
.status{padding:10px;border-radius:8px;margin-bottom:20px;text-align:center}
.status.success{background:#00ff8833;color:#00ff88}
.status.error{background:#ff444433;color:#ff4444}
.status.info{background:#00d4ff33;color:#00d4ff}
table{width:100%;border-collapse:collapse;margin-top:10px}
th,td{padding:8px;text-align:left;border-bottom:1px solid #333}
th{color:#00d4ff}
.loading{text-align:center;padding:40px;color:#888}
</style>
)rawliteral";

// ==================== AP Mode Pages ====================

// WiFi Setup Page
void handleAPRoot() {
    DEBUG_PRINTLN("[Web] Serving AP root page");
    String networks = scanNetworks();
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>MedivalOS WiFi Setup</title>";
    html += FPSTR(CSS_STYLES);
    yield();  // Let ESP8266 handle background tasks
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>MedivalOS</h1>";
    html += "<p class='subtitle'>WiFi Configuration</p>";
    
    html += "<form method='POST' action='/connect'>";
    html += "<label for='ssid'>WiFi Network</label>";
    html += "<select name='ssid' id='ssid'>" + networks + "</select>";
    html += "<label for='password'>Password</label>";
    html += "<input type='password' name='password' id='password' placeholder='Enter WiFi password'>";
    html += "<button type='submit'>Connect</button>";
    html += "</form>";
    
    html += "<button class='btn btn-secondary' onclick='location.reload()'>Refresh Networks</button>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
    DEBUG_PRINTLN("[Web] AP root page sent");
}

// Handle WiFi Connect Request
void handleConnect() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    DEBUG_PRINTF("[Web] Connect request - SSID: %s\n", ssid.c_str());
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>MedivalOS - Connecting</title>";
    html += FPSTR(CSS_STYLES);
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>MedivalOS</h1>";
    html += "<div class='status info'>Connecting to " + ssid + "...</div>";
    html += "<p>The device will restart. After connecting to your WiFi, access:</p>";
    html += "<p style='text-align:center;font-size:24px;color:#00d4ff;'>http://medivalos.local</p>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
    delay(1000);
    
    // Save credentials and restart
    wifiSSID = ssid;
    wifiPassword = password;
    saveWiFiCredentials(ssid, password);
    
    DEBUG_PRINTLN("[WiFi] Restarting...");
    delay(500);
    ESP.restart();
}

// ==================== Station Mode Pages ====================

// Main Dashboard
void handleRoot() {
    DEBUG_PRINTLN("[Web] Serving main dashboard");
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>MedivalOS Dashboard</title>";
    html += FPSTR(CSS_STYLES);
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>MedivalOS</h1>";
    html += "<p class='subtitle'>Intervals.icu Form Tracker</p>";
    
    yield();  // Let ESP8266 handle background tasks
    
    // Status
    html += "<div class='status success'>Connected to: " + wifiSSID + "</div>";
    if (timeSync) {
        html += "<div class='status info'>" + getTodayDate() + "</div>";
    } else {
        html += "<div class='status error'>Time not synced</div>";
    }
    
    // Form Display Card
    html += "<div class='form-card'>";
    html += "<div class='form-label'>Current Form</div>";
    String formClass = currentForm >= 0 ? "positive" : "negative";
    html += "<div class='form-value " + formClass + "'>" + String((int)currentForm) + "</div>";
    html += "<div class='stats'>";
    html += "<div class='stat'><div class='stat-value'>" + String((int)currentCTL) + "</div><div class='stat-label'>Fitness (CTL)</div></div>";
    html += "<div class='stat'><div class='stat-value'>" + String((int)currentATL) + "</div><div class='stat-label'>Fatigue (ATL)</div></div>";
    html += "</div>";
    html += "<p style='color:#666;font-size:12px;margin-top:15px;'>Last updated: " + lastUpdateTime + "</p>";
    html += "</div>";
    
    yield();  // Let ESP8266 handle background tasks
    
    // Action Buttons
    html += "<button onclick='getForm()'>Get Today's Form</button>";
    html += "<button onclick='getHistory()'>View Last 30 Days</button>";
    
    // API Configuration
    html += "<h2>API Settings</h2>";
    if (apiConfigured) {
        html += "<div class='status success'>API Key configured</div>";
    } else {
        html += "<div class='status error'>API Key not configured</div>";
    }
    
    html += "<form method='POST' action='/saveapi'>";
    html += "<label for='apikey'>Intervals.icu API Key</label>";
    html += "<input type='password' name='apikey' id='apikey' placeholder='Enter your API key' value='";
    html += (apiConfigured ? "********" : "");
    html += "'>";
    html += "<label for='athleteid'>Athlete ID (leave 0 for default)</label>";
    html += "<input type='text' name='athleteid' id='athleteid' placeholder='0' value='" + athleteId + "'>";
    html += "<button type='submit'>Save API Key</button>";
    html += "</form>";
    
    yield();  // Let ESP8266 handle background tasks
    
    // Reset Button
    html += "<h2>Device Settings</h2>";
    html += "<form method='POST' action='/reset' onsubmit='return confirm(\"Reset all settings?\")'>";
    html += "<button type='submit' class='btn btn-danger'>Reset All Settings</button>";
    html += "</form>";
    
    // History Display Area (hidden by default)
    html += "<div id='historyArea' style='display:none;'>";
    html += "<h2>Form History (Last 30 Days)</h2>";
    html += "<div id='historyContent'></div>";
    html += "</div>";
    
    // JavaScript (minified)
    html += "<script>";
    html += "function getForm(){fetch('/api/form').then(r=>r.json()).then(d=>{if(d.success)location.reload();else alert('Error: '+d.error);}).catch(e=>alert('Error fetching form'));}";
    html += "function getHistory(){document.getElementById('historyArea').style.display='block';document.getElementById('historyContent').innerHTML='<div class=loading>Loading...</div>';fetch('/api/history').then(r=>r.json()).then(d=>{if(d.error){document.getElementById('historyContent').innerHTML='<div class=status>'+d.error+'</div>';return;}let h='<table><tr><th>Date</th><th>Form</th><th>CTL</th><th>ATL</th></tr>';d.reverse().forEach(r=>{let c=r.form>=0?'#00ff88':'#ff4444';h+='<tr><td>'+r.date+'</td><td style=color:'+c+'>'+r.form+'</td><td>'+r.ctl+'</td><td>'+r.atl+'</td></tr>';});h+='</table>';document.getElementById('historyContent').innerHTML=h;}).catch(e=>document.getElementById('historyContent').innerHTML='<div class=status>Error</div>');}";
    html += "</script>";
    
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
    DEBUG_PRINTLN("[Web] Dashboard sent");
}

// Save API Key
void handleSaveAPI() {
    String key = server.arg("apikey");
    String athId = server.arg("athleteid");
    
    // Don't overwrite if placeholder
    if (key != "********" && key.length() > 0) {
        apiKey = key;
    }
    
    if (athId.length() == 0) athId = "0";
    athleteId = athId;
    
    saveAPIKey(apiKey, athleteId);
    apiConfigured = (apiKey.length() > 0);
    
    DEBUG_PRINTLN("[Web] API key saved");
    
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

// Reset All Settings
void handleReset() {
    clearAllCredentials();
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='3;url=http://192.168.4.1'>";
    html += "<title>MedivalOS - Reset</title>";
    html += FPSTR(CSS_STYLES);
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>MedivalOS</h1>";
    html += "<div class='status info'>All settings cleared. Device restarting...</div>";
    html += "<p>Connect to WiFi network: <strong>" + String(AP_SSID) + "</strong></p>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
}

// ==================== API Endpoints ====================

// Get Today's Form (JSON)
void handleAPIForm() {
    DEBUG_PRINTLN("[API] Form request received");
    
    if (!apiConfigured) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"API key not configured\"}");
        return;
    }
    
    if (getTodayForm()) {
        String json = "{\"success\":true,\"form\":" + String((int)currentForm) + 
                     ",\"ctl\":" + String((int)currentCTL) + 
                     ",\"atl\":" + String((int)currentATL) + 
                     ",\"date\":\"" + lastUpdateTime + "\"}";
        server.send(200, "application/json", json);
    } else {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Failed to fetch data\"}");
    }
}

// Get 30-Day History (JSON)
void handleAPIHistory() {
    DEBUG_PRINTLN("[API] History request received");
    
    if (!apiConfigured) {
        server.send(400, "application/json", "{\"error\":\"API key not configured\"}");
        return;
    }
    
    String history = getLast30DaysForm();
    server.send(200, "application/json", history);
}

// Handle 404 / Captive Portal Redirect
void handleNotFound() {
    if (isAPMode) {
        // In AP mode, redirect all requests to the setup page (captive portal)
        DEBUG_PRINTF("[Web] Redirecting %s to captive portal\n", server.uri().c_str());
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}
