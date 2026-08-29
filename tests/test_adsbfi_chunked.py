from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
cpp = (ROOT / 'src' / 'adsb_service.cpp').read_text()
version = (ROOT / 'include' / 'version.h').read_text()
assert '0.30.16-adsbfi-110nm' in version
assert 'class PsramBodyStream : public Stream' in cpp
assert 'http.writeToStream(&sink)' in cpp
assert 'Transfer-Encoding: chunked' in cpp
assert 'body no-data timeout' not in cpp
assert 'kNoDataTimeoutMs' not in cpp
assert 'body exceeded 1 MB limit' in cpp
assert 'stream size mismatch' in cpp
print('ADSB.FI CHUNKED TEST OK')
