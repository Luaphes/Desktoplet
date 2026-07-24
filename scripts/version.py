"""PlatformIO pre-build script: read version.txt → inject FIRMWARE_VERSION build flag."""
import os
Import("env")

ver_file = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "version.txt")
try:
    with open(ver_file) as f:
        ver = f.read().strip()
except Exception:
    ver = "dev"
env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\'"{ver}"\''])
print(f"[version] FIRMWARE_VERSION set to v{ver}")
