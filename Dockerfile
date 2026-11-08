# nodeflash in a container - Linux hosts only.
# Docker Desktop on Windows/macOS can't pass USB serial ports or the ST-Link
# into a container; there, use nodeflash.cmd / nodeflash.sh instead.
#
#   docker build -t nodeflash .
#   docker run --rm -it --device /dev/ttyUSB0 nodeflash scan --port /dev/ttyUSB0
#   docker run --rm -it --device /dev/ttyUSB0 -v "$PWD/myapp:/app" nodeflash update --project /app --no-build
#   ST-Link (factory):  add  --privileged -v /dev/bus/usb:/dev/bus/usb
FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends openocd && rm -rf /var/lib/apt/lists/*
WORKDIR /nodeflash
COPY pyproject.toml ./
COPY nodeflash ./nodeflash
COPY firmware/bin ./firmware/bin
COPY firmware/nodeflash-lib ./firmware/nodeflash-lib
ENV NODEFLASH_HOME=/nodeflash
RUN pip install --no-cache-dir -e ".[mcp]"
ENTRYPOINT ["nodeflash"]
