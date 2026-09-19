## Onboard pins
| FUNC         | PIN | ------ | PIN | FUNC         |
|--------------|-----|--------|-----|--------------|
|              | 3V3 |        | GND |              |
|              | 3V3 |        | 44  | TX CH343     |
|              | RST |        | 43  | RX CH343     |
| M1-ENC-A     | 4   |        | 1   | RX RP3       |
| M1-ENC-B     | 5   |        | 2   | M1-PWM       |
| M2-ENC-A     | 6   |        | 42  | M2-PWM       |
| M2-ENC-B     | 7   |        | 41  | M3-PWM       |
| M3-ENC-A     | 15  |        | 40  | M4-PWM       |
| M3-ENC-B     | 16  |        | 39  | DIR 1        |
| M4-ENC-A     | 17  |        | 38  | DIR 2        |
| M4-ENC-B     | 18  |        | 37  | PSRAM        |
| SDA          | 8   |        | 36  | PSRAM        |
| BNO085 CS    | 3   |        | 35  | PSRAM        |
| BOOT STRAP   | 46  |        | 0   | BOOT STRAP   |
| SCL          | 9   |        | 45  |              |
| BNO085 INR   | 10  |        | 48  | BUILTIN LED  |
| BNO085 CLK   | 11  |        | 47  | BRAKE 1      |
| BNO085 MOSI  | 12  |        | 21  | BRAKE 1      |
| BNO085 MISO  | 13  |        | 20  | USB D+       |
| BNO085 RST   | 14  |        | 19  | USB D-       |
|              | 5V  |        | GND |              |
|              | GND |        | GND |              |

## Other pins
| 34  | PSRAM        |
| 33  | PSRAM        |
| 32  | PSRAM        |

Note: GPIO26–37 are reserved by the ESP32-S3 octal PSRAM and must not be used for application I/O. The motor 3 and motor 4 encoder signals were moved from GPIO38 and GPIO35–37 to GPIO21–24.

Motor
|Red|Black|Blue|Green|White|Yellow|Grey|
|-|-|-|-|-|-|-|
|VCC|GND|DIR|Encoder A|PWM speed control|BRAKE|Encoder B|