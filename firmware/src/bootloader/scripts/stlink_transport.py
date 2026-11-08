Import("env")

# PlatformIO ST STM32 19.4.0 forces HLA even with newer native ST-Link
# drivers. Leave transport selection to interface/stlink.cfg instead.
server = env.BoardConfig().get("debug.tools.stlink.server")
args = server["arguments"]
for index in range(len(args) - 1):
    if args[index:index + 2] == ["-c", "transport select hla_swd"]:
        del args[index:index + 2]
        break
