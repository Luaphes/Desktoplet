"""PlatformIO pre-build script: read version.txt → inject FIRMWARE_VERSION build flag."""
import os
Import("env")

root = env.Dir("#").abspath
ver_file = os.path.join(root, "version.txt")
try:
    with open(ver_file) as f:
        ver = f.read().strip()
except Exception:
    ver = "dev"
if ver and not ver.startswith("v"):
    ver = f"v{ver}"
env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\'"{ver}"\''])
print(f"[version] FIRMWARE_VERSION set to {ver}")
