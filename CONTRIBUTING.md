# Contributing

Keep changes focused, buildable, and independent of any real installation.
Never commit credentials, personal information, real device identifiers, or
private tank dimensions.

Before submitting a change:

```bash
source ~/esp/esp-idf-v5.5/export.sh
idf.py set-target esp32
idf.py build
```

Pure configuration, protocol, tank, and alarm logic should include host tests.
Hardware changes must identify the exact board profile and describe how they
were tested on target hardware.