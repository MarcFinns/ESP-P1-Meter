# P1 Meter

- Attaches to the standard P1 port of Dutch smart meters
- Powered via the P1 port (no power supply needed)
- Can be invoked via HTTP from any home automation system via REST API
- Readings in JSON format via via **/readMeter** endpoint
- Reboot via **/reboot** endpoint
- Status can be visualised via **/settings** endpoing
- Integrates WifiManager for network selection without hardcoding credentials
- Integrates ArduinoOTA for updates without cable
