import queue
import threading
import time
import logging
import serial

from protocol_common import (
    MSG_ACK,
    MSG_NACK,
    TYPE_NAMES,
    FrameParser,
    build_frame,
)

LOGGER = logging.getLogger(__name__)


class SerialAgent:
    """Test agent for sending/receiving framed-link messages over serial."""

    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.parser = FrameParser()
        self.rx_queue = queue.Queue()
        self._stop = threading.Event()
        self._seq = 0
        self._seq_lock = threading.Lock()

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.01)
            self.ser.dtr = True
            # RTS is connected to the board reset/boot circuitry.  Keep it
            # deasserted; asserting it can leave the ESP32 in download mode.
            self.ser.rts = False
            LOGGER.debug("[SERIAL] Connected to %s", self.port)
        except serial.SerialException as e:
            raise RuntimeError(f"Failed to connect to serial port {self.port}") from e

    def _rx_loop(self):
        while not self._stop.is_set():
            try:
                read_size = self.ser.in_waiting or 1
                data = self.ser.read(read_size)
                if data:
                    frames = self.parser.feed(data)
                    for frame in frames:
                        self.rx_queue.put(frame)
            except Exception as e:
                if not self._stop.is_set():
                    LOGGER.debug("[SERIAL] RX error: %s", e)
                break

    def start_rx(self):
        rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        rx_thread.start()

    def send_frame(self, msg_type: int, payload: bytes) -> int:
        self.drain_rx()

        with self._seq_lock:
            seq = self._seq
            self._seq = (self._seq + 1) & 0xFF

        frame = build_frame(msg_type, seq, payload)
        self.ser.write(frame)
        name = TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
        LOGGER.debug("  [TX] %s seq=%d payload=%s", name, seq, payload.hex())
        return seq

    def wait_for_ack(self, expected_seq: int, timeout=1.0) -> bool:
        return self.wait_for_response(MSG_ACK, expected_seq, timeout) is not None

    def wait_for_nack(self, expected_seq: int, timeout=1.0):
        return self.wait_for_response(MSG_NACK, expected_seq, timeout)

    def wait_for_type(self, expected_type: int, timeout=1.0):
        """Wait for an unsolicited frame type and return its sequence and payload."""
        start = time.monotonic()
        while time.monotonic() - start < timeout:
            remaining = max(0.0, timeout - (time.monotonic() - start))
            try:
                msg_type, seq, payload = self.rx_queue.get(timeout=min(0.1, remaining))
            except queue.Empty:
                continue

            name = TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
            if msg_type == expected_type:
                LOGGER.debug("  [RX] %s seq=%d payload=%s", name, seq, payload.hex())
                return seq, payload

            LOGGER.debug(
                "  [RX] Unexpected %s seq=%d payload=%s", name, seq, payload.hex()
            )

        name = TYPE_NAMES.get(expected_type, f"0x{expected_type:02X}")
        LOGGER.debug("  [TIMEOUT] No %s frame", name)
        return None

    def wait_for_response(self, expected_type: int, expected_seq: int, timeout=1.0):
        start = time.monotonic()
        while time.monotonic() - start < timeout:
            remaining = max(0.0, timeout - (time.monotonic() - start))
            try:
                msg_type, seq, payload = self.rx_queue.get(timeout=min(0.1, remaining))
            except queue.Empty:
                continue

            if msg_type == expected_type and seq == expected_seq:
                name = TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
                LOGGER.debug("  [RX] %s seq=%d payload=%s", name, seq, payload.hex())
                return payload

            name = TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
            LOGGER.debug(
                "  [RX] Unexpected %s seq=%d payload=%s", name, seq, payload.hex()
            )

        name = TYPE_NAMES.get(expected_type, f"0x{expected_type:02X}")
        LOGGER.debug("  [TIMEOUT] No %s for seq=%d", name, expected_seq)
        return None

    def drain_rx(self):
        while True:
            try:
                self.rx_queue.get_nowait()
            except queue.Empty:
                return

    def close(self):
        self._stop.set()
        if self.ser:
            self.ser.close()
