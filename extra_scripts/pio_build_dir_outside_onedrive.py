# Place PlatformIO build output under ~/.pio-build/SparkyCheck (or %USERPROFILE% on Windows).
# Projects under OneDrive\Documents often hit ld "cannot find *.cpp.o" when sync touches .pio/build
# between compile and link. This keeps object files outside the synced tree.
#
# Skip on GitHub Actions: .github/workflows/ota-release.yml expects .pio/build/<env>/firmware.bin

from pathlib import Path
import os

Import("env")


def _home() -> str:
    return os.environ.get("USERPROFILE") or os.environ.get("HOME") or str(Path.home())


if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
    pass
else:
    bd = Path(_home()) / ".pio-build" / "SparkyCheck"
    try:
        bd.mkdir(parents=True, exist_ok=True)
    except OSError:
        pass
    env.Replace(BUILD_DIR=str(bd))
