LittleFS is used only for the initial CHMI radar cache prepared before the LCD starts.
Runtime radar refreshes download the newest PNG directly into PSRAM and do not write radar PNG files to LittleFS.
No manual filesystem upload is required.
