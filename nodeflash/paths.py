import os

# The bundle folder (holds firmware/). NODEFLASH_HOME overrides it, e.g. when
# the package is installed somewhere else than the bundle.
BUNDLE = os.environ.get("NODEFLASH_HOME") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE_BIN = os.path.join(BUNDLE, "firmware", "bin")
FIRMWARE_LIB = os.path.join(BUNDLE, "firmware", "nodeflash-lib")
GATEWAY_BIN = os.path.join(FIRMWARE_BIN, "gateway", "gateway-esp32s3.bin")
