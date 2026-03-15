#!/usr/bin/env python3
import argparse
import json


def parse_bool(value: str) -> bool:
    v = value.strip().lower()
    if v in ("1", "true", "yes", "on"):
        return True
    if v in ("0", "false", "no", "off"):
        return False
    raise argparse.ArgumentTypeError(f"invalid boolean: {value}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate SparkyCheck OTA manifest JSON.")
    parser.add_argument("--output", required=True, help="Output JSON path")
    parser.add_argument("--channel", default="stable", help="Update channel")
    parser.add_argument("--version", required=True, help="Firmware version string")
    parser.add_argument("--firmware-url", required=True, help="HTTPS URL to firmware .bin")
    parser.add_argument("--md5", default="", help="Optional firmware MD5")
    parser.add_argument("--rollout-pct", type=int, default=100, help="Rollout percentage 1-100")
    parser.add_argument("--force", type=parse_bool, default=False, help="Force install")
    parser.add_argument("--notes", default="", help="Release notes shown in manifest")
    args = parser.parse_args()

    if args.rollout_pct < 1 or args.rollout_pct > 100:
        raise SystemExit("--rollout-pct must be between 1 and 100")

    payload = {
        "channel": args.channel,
        "version": args.version,
        "firmware_url": args.firmware_url,
        "md5": args.md5,
        "rollout_pct": args.rollout_pct,
        "force": args.force,
        "notes": args.notes,
    }

    with open(args.output, "w", encoding="ascii") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
