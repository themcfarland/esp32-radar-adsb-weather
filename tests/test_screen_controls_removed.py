from pathlib import Path

ui = Path("src/ui.cpp").read_text(encoding="utf-8")
assert 'makeButton(header, 646, 70' not in ui
assert 'makeButton(header, 722, 72' not in ui
assert '"PAUZA", pauseEvent' not in ui
assert '"OBNOVIT", refreshEvent' not in ui
assert 'makeLabel(header, 220, 10, 570' in ui
print("SCREEN CONTROLS REMOVED TEST OK")
