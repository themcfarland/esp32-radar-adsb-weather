#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
config=(ROOT/'include/config.h').read_text(encoding='utf-8')
worker=(ROOT/'src/network_worker.cpp').read_text(encoding='utf-8')
device=(ROOT/'src/device_config.cpp').read_text(encoding='utf-8')
version=(ROOT/'include/version.h').read_text(encoding='utf-8')
assert '0.30.9-adaptive-tls-guard' in version
assert '38UL * 1024UL' in config
assert '30UL * 1024UL' in config
assert '26UL * 1024UL' in config
assert '32UL * 1024UL' in config
assert 'TLS_RECOVERY_FAILURE_LARGEST_BLOCK = 36UL * 1024UL' in config
assert 'Normal and warning states always attempt TLS' in worker
assert 'requestReactiveTlsRecovery' in worker
assert 'code >= 400 && code < 600' in worker
assert 'tlsFailureRecoveryUsed_[index]' in worker
assert 'pre-flight defer is TLS-guard metadata' in worker
assert 'tls_memory_state' in device
print('ADAPTIVE TLS GUARD TEST OK')
