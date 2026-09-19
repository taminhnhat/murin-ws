# Host tools

These tools monitor the ESP32 serial connection, decode framed-link and ROS2
messages, and send command frames to a running firmware image.

## Files

- `parser.py` — Python serial monitor and framed-link/ROS2 message sender.
- `parser.js` — Node.js serial monitor and message sender.
- `config.yaml` — local serial port configuration.

Configure the serial connection in `config.yaml`:

```yaml
port: "COM12"
baudrate: 2000000
```

## Run the parsers

From the repository root:

```powershell
python tools/parser.py
node tools/parser.js
```

Override serial settings from the command line:

```powershell
python tools/parser.py --port COM12 --baudrate 2000000
node tools/parser.js --port COM12 --baudrate 2000000
```

Print raw serial bytes:

```powershell
python tools/parser.py --raw
node tools/parser.js --raw
```

Send ROS2 command frames:

```powershell
python tools/parser.py --heartbeat
python tools/parser.py --motor 512 -512
python tools/parser.py --servo 0 1500
python tools/parser.py --config 1 1

node tools/parser.js --heartbeat
node tools/parser.js --motor 512 -512
node tools/parser.js --servo 0 1500
node tools/parser.js --config 1 1
```

Options can be combined. For example, send a heartbeat and disable telemetry:

```powershell
python tools/parser.py --heartbeat --config 1 0
node tools/parser.js --heartbeat --config 1 0
```

Send generic framed-link payloads:

```powershell
python tools/parser.py --frame 0x30 "01 02 AA 1B"
python tools/parser.py --json 0x30 '{"cmd":"motor","left":512,"right":-512}'

node tools/parser.js --frame 0x30 "01 02 AA 1B"
node tools/parser.js --json 0x30 '{"cmd":"motor","left":512,"right":-512}'
```

Decoded output includes ACK/NACK responses, ROS2 command payloads, telemetry
values, and printable custom text or JSON payloads.

Install the Python dependencies from the repository root with:

```powershell
python -m pip install -r requirements.txt
```
