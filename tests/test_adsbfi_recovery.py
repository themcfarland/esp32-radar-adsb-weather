from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def text(path):
    return (ROOT / path).read_text(encoding="utf-8")


def main():
    cfg = text("include/config.h")
    worker = text("src/network_worker.cpp")
    main_cpp = text("src/main.cpp")
    dc_h = text("src/device_config.h")
    dc_cpp = text("src/device_config.cpp")

    assert "ADSB_FI_BACKOFF_FIRST_MS = 15UL * 1000UL" in cfg
    assert "ADSB_FI_BACKOFF_SECOND_MS = 30UL * 1000UL" in cfg
    assert "ADSB_FI_BACKOFF_MAX_MS = 60UL * 1000UL" in cfg
    assert "ADSB_FI_RECOVERY_RETRY_MS = 30UL * 1000UL" in cfg
    assert "return Config::ADSB_FI_BACKOFF_MAX_MS" in worker
    assert "stale cache -> forced recovery request" in main_cpp
    assert "consumeAdsbInternetRefreshRequested" in dc_h
    assert 'server_.on("/adsb-refresh"' in dc_cpp
    assert "Obnovit internetove ADS-B" in dc_cpp
    assert 'adsb_internet_http_code' in dc_cpp
    assert 'adsb_internet_retry_ms' in dc_cpp
    print("ADSB.FI RECOVERY TEST OK")


if __name__ == "__main__":
    main()
