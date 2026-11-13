"""Generate the ignored credentials header for both firmware targets."""
from pathlib import Path
import json
root = Path(__file__).resolve().parents[1]
path = root / '.env'
if not path.is_file():
    raise SystemExit('Copy .env.example to .env before building.')
values = {}
for line in path.read_text(encoding='utf-8').splitlines():
    line = line.strip()
    if line and not line.startswith('#') and '=' in line:
        key, value = line.split('=', 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
ssid = values.get('CNC_AP_SSID', '')
password = values.get('CNC_AP_PASSWORD', '')
if not 1 <= len(ssid.encode()) <= 32 or not 8 <= len(password.encode()) <= 63:
    raise SystemExit('Use an SSID of 1–32 bytes and a password of 8–63 bytes.')
header = root / 'include/network_credentials.h'
header.parent.mkdir(exist_ok=True)
header.write_text('#pragma once\n// Generated locally. Never commit this file.\n'
                  + '#define CNC_AP_SSID ' + json.dumps(ssid, ensure_ascii=False) + '\n'
                  + '#define CNC_AP_PASSWORD ' + json.dumps(password, ensure_ascii=False) + '\n', encoding='utf-8')
print('Local firmware configuration generated.')
