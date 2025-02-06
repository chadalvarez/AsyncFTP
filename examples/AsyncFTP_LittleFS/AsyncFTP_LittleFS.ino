
#include <WiFi.h>
#include "AsyncFTP.h"

// Replace these with your network credentials.
const char* ssid     = "ssid";
const char* password = "pw";

// Create an FTP server instance using LittleFS.
AsyncFTP ftpServer(DEFAULT_FTP_PORT, FTP_FS::LITTLEFS);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Connect to WiFi.
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: "); Serial.println(WiFi.localIP());
  
  // Initialize the filesystem (LittleFS in this example).
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    ESP.restart();
  }
  
  // Start the FTP server with optional username and password.
  ftpServer.begin("esp32", "esp32");
  Serial.println("FTP server started on port " + String(DEFAULT_FTP_PORT));
}

void loop() {

}