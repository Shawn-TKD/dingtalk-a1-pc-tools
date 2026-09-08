"""Query the user's own A1 through the already authenticated emulator process."""
import argparse
import json
import sys
from pathlib import Path

import frida

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["inspect", "query", "packageurl"])
    parser.add_argument("--version", default="")
    parser.add_argument("--pid", type=int, default=2819)
    args = parser.parse_args()
    device = frida.get_device_manager().add_remote_device("127.0.0.1:27042")
    session = device.attach(args.pid)
    try:
        script = session.create_script((ROOT / "inspect-ota.js").read_text(encoding="utf-8"))
        script.on("message", lambda message, data: print(json.dumps(message, ensure_ascii=False)))
        script.load()
        if args.action == "inspect":
            result = script.exports_sync.inspect()
        elif args.action == "query":
            result = script.exports_sync.query(args.version)
        else:
            result = script.exports_sync.packageurl(args.version)
        path = ROOT / (args.action + "-result.json")
        path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        public = dict(result)
        if "deviceId" in public:
            public["deviceId"] = "[saved locally]"
        if "packageDownloadUrl" in public:
            public["packageDownloadUrl"] = "[saved locally]"
        if "deviceTask" in public:
            public["deviceTask"] = {k: v for k, v in public["deviceTask"].items()
                                    if k in ("state", "taskType", "stateCode")}
        print(json.dumps(public, ensure_ascii=False, indent=2))
    finally:
        session.detach()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
