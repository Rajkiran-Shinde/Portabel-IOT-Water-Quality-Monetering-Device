#include "sms.h"
#include "config.h" // We still include this to access the extern variables
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void SMSManager::sendAlert(String alertLevel, float ph, float tds, float hardness) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Cannot send Twilio SMS.");
        return;
    }

    // --- SAFETY CHECK ---
    // Prevent the ESP32 from trying to send a text if the settings are empty
    if (twilio_sid == "" || twilio_token == "" || twilio_to == "") {
        Serial.println("Twilio credentials not configured in Web Dashboard! Aborting SMS.");
        return;
    }

    WiFiClientSecure client;
    client.setInsecure(); // Bypasses SSL certificate checks to keep things fast

    HTTPClient http;
    
    // Use dynamic SID from memory
    String url = "https://api.twilio.com/2010-04-01/Accounts/" + twilio_sid + "/Messages.json";
    
    http.begin(client, url);
    
    // HTTPClient handles the Base64 encoding for the authorization header automatically
    http.setAuthorization(twilio_sid.c_str(), twilio_token.c_str());
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // 1. Build the message string using your custom dynamic website message
    String message = twilio_msg + alertLevel + "\n";
    message += "pH: " + String(ph, 2) + "\n";
    message += "TDS: " + String(tds, 0) + " mg/L\n";
    message += "Hardness: " + String(hardness, 0) + " mg/L";

    // 2. Format it for the web (URL Encoding spaces and newlines)
    message.replace(" ", "%20");
    message.replace("\n", "%0A");

    // 3. Construct the final payload using dynamic phone numbers
    String payload = "To=" + twilio_to + "&From=" + twilio_from + "&Body=" + message;

    Serial.println("Connecting to Twilio...");
    
    // 4. Send the POST request
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode == 201) { // 201 is Twilio's success code for "Created"
        Serial.println("Twilio SMS Sent Successfully!");
    } else {
        Serial.print("Error sending SMS. HTTP Code: ");
        Serial.println(httpResponseCode);
        Serial.println(http.getString()); // Prints the exact error from Twilio
    }

    http.end();
}