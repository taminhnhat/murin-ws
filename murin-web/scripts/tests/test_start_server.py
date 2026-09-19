import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('start_server', Path(__file__).resolve().parents[1] / 'tools/start-server.py')
START = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(START)


class ConfigurationTests(unittest.TestCase):
    def check_config(self, settings, environ=None):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'server').mkdir()
            (root / 'server/.env').write_text(settings)
            return START.check_configuration(root, environ={} if environ is None else environ)[1]

    def test_socket_needs_no_serial_ports(self):
        self.assertEqual(self.check_config('ROBOT_TRANSPORT=socket\nHTTP_PORT=9091\n'), [])

    def test_serial_needs_both_ports(self):
        self.assertEqual(self.check_config('ROBOT_TRANSPORT=serial\n'), ['USB_PORT', 'CONSOLE_PORT'])

    def test_environment_takes_precedence(self):
        self.assertEqual(self.check_config('ROBOT_TRANSPORT=serial\n', {'ROBOT_TRANSPORT': 'socket'}), [])

    def test_bad_http_port(self):
        for port in ['0', '65536', 'abc', '1.5']:
            self.assertIn('HTTP_PORT', self.check_config(f'ROBOT_TRANSPORT=socket\nHTTP_PORT={port}\n')[0])

    def test_invalid_mode(self):
        self.assertIn('ROBOT_TRANSPORT', self.check_config('ROBOT_TRANSPORT=unknown\n')[0])

    def test_dotenv_quoting(self):
        self.assertEqual(START.read_settings('export HTTP_PORT="9091"\nROBOT_TRANSPORT=socket # comment\n'), {'HTTP_PORT': '9091', 'ROBOT_TRANSPORT': 'socket'})


if __name__ == '__main__':
    unittest.main()
