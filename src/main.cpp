#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <FS.h>

struct Configuration
{
    const char *ssid;
    const char *password;
    IPAddress ip_address;
    IPAddress gateway;
    IPAddress subnet;
};

/* ESP8266 CONFIG FILE */
const char *DEFAULT_CONFIG_FILENAME = "/settings.json";

/* WEB SERVER CONFIG */
ushort DEFAULT_HTTP_PORT = 80;

/* DNS CONFIG */
ushort DEFAULT_DNS_PORT = 53;
int DEFAULT_DNS_TTL = 86400; // 24 hours

Configuration config;
AsyncWebServer webServer(DEFAULT_HTTP_PORT);
DNSServer dnsServer;

void loadConfiguration(const char *filename, Configuration &config)
{
    File file = SPIFFS.open(filename, "r");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);

    if (error) {
        Serial.println("Failed to read file, using default configuration");
    }

    /* Parse the JSON object to the Configuration Structure */
    config.ssid = doc["ssid"].as<const char *>();
    config.password = doc["password"].as<const char *>();
    config.ip_address.fromString(doc["ip_address"].as<char *>());
    config.subnet.fromString(doc["subnet"].as<char *>());
    config.gateway.fromString(doc["gateway"].as<char *>());

    doc.clear();
    file.close();
}

void saveConfiguration(const char *filename, const Configuration &config)
{
    /* Create backup file */
    String backupFilename = String(filename) + ".bak";
    SPIFFS.rename(filename, backupFilename);
    
    // Opens a file for writing only. Overwrites the file if the file exists. If the file does not exist, creates a new file for writing.
    File file = SPIFFS.open(filename, "w");
    if (!file)
    {
        Serial.println("Failed to create file");
        // Restore backup file
        SPIFFS.rename(backupFilename, filename);
        return;
    }

   StaticJsonDocument<512> doc;

    /* Set the values */
    doc["ssid"] = config.ssid;
    doc["password"] = config.password;
    doc["ip_address"] = config.ip_address.toString();
    doc["subnet"] = config.subnet.toString();
    doc["gateway"] = config.gateway.toString();

    /* Serialize JSON to file */
    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Failed to write to file");
        // Restore backup file
        SPIFFS.rename(backupFilename, filename);
    }

    doc.clear();
    file.close();
}

void setup()
{
    delay(1000);
    Serial.begin(9600);

    /* Loading general configuration */
    if (!SPIFFS.begin())
    {
        Serial.println("Failed to mount SPIFFS");
    }

    loadConfiguration(DEFAULT_CONFIG_FILENAME, config);

    /* Settings up Wi-Fi AP */
    WiFi.softAPConfig(config.ip_address, config.gateway, config.subnet);
    WiFi.softAP(config.ssid, config.password);

    /* Settings up DNS Server */
    dnsServer.setTTL(DEFAULT_DNS_TTL);
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);

    /* Redirect all domains to the local WebServer */
    dnsServer.start(DEFAULT_DNS_PORT, "*", config.ip_address);

    /* Settings up WebServer */
    webServer.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    webServer.on("/esp8266/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        ESP.restart();
    });
    webServer.on("/esp8266/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        ESP.reset();
    });
    webServer.on("/esp8266/information", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(1024);

        /* Set values */
        doc["boot_mode"] = ESP.getBootMode();
        doc["boot_version"] = ESP.getBootVersion();
        doc["chip_id"] = ESP.getChipId();
        doc["core_version"] = ESP.getCoreVersion();
        doc["cpu_freq"] = ESP.getCpuFreqMHz();
        doc["cycle_count"] = ESP.getCycleCount();
        doc["flash_chip_id"] = ESP.getFlashChipId();
        doc["flash_chip_mode"] = ESP.getFlashChipMode();
        doc["flash_chip_real_size"] = ESP.getFlashChipRealSize();
        doc["flash_chip_size"] = ESP.getFlashChipSize();
        doc["flash_chip_size_by_chip_id"] = ESP.getFlashChipSizeByChipId();
        doc["flash_chip_speed"] = ESP.getFlashChipSpeed();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["free_sketch_space"] = ESP.getFreeSketchSpace();
        doc["full_version"] = ESP.getFullVersion();
        doc["reset_info"] = ESP.getResetInfo();
        doc["reset_reason"] = ESP.getResetReason();
        doc["sdk_version"] = ESP.getSdkVersion();
        doc["sketch_md5"] = ESP.getSketchMD5();
        doc["sketch_size"] = ESP.getSketchSize();
        doc["vcc"] = ESP.getVcc();
        String response;
        
        // Serialize JSON to Sring to send it
        serializeJson(doc, response);
        doc.clear();

        return request->send(200, "application/json", response);
    });
    webServer.on("/settings/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        /* Validation Rules */
        if (!request->hasParam("ssid", true))
        {
            return request->send(400, "text/plain", "SSID parameter is required");
        }
        if (!request->hasParam("password", true))
        {
            return request->send(400, "text/plain", "Password parameter is required");
        }
        if (!request->hasParam("ip_address", true))
        {
            return request->send(400, "text/plain", "IP Address parameter is required");
        }
        if (!request->hasParam("subnet", true))
        {
            return request->send(400, "text/plain", "Subnet parameter is required");
        }
        if (!request->hasParam("gateway", true))
        {
            return request->send(400, "text/plain", "Gateway parameter is required");
        }

        /* If we made it so far, we can modify the configuration and save it */
        AsyncWebParameter *p = request->getParam("ssid", true);
        if (p->value().length() == 0)
        {
            return request->send(400, "text/plain", "SSID value is required");
        }
        config.ssid = p->value().c_str();

        /* No verification for password because Wi-Fi AP can be open */
        p = request->getParam("password", true);
        config.password = p->value().c_str();

        p = request->getParam("ip_address", true);
        if (!config.ip_address.fromString(p->value()))
        {
            return request->send(400, "text/plain", "IP Address is not valid");
        }

        p = request->getParam("subnet", true);
        if (!config.subnet.fromString(p->value()))
        {
            return request->send(400, "text/plain", "Subnet is not valid");
        }

        p = request->getParam("gateway", true);
        if (!config.gateway.fromString(p->value()))
        {
            return request->send(400, "text/plain", "Gateway is not valid");
        }

        saveConfiguration(DEFAULT_CONFIG_FILENAME, config);
        return request->send(200, "text/plain", "Configuration updated");
    });
    /* If the user try to access to an unknown page, we redirect him to the root page */
    webServer.onNotFound([](AsyncWebServerRequest *request){
        return request->redirect("/");
    });
    // Start WebServer
    webServer.begin();
}

void loop()
{
    // Start processing DNS requests
    dnsServer.processNextRequest();
}
