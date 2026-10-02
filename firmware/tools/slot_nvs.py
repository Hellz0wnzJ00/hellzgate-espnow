#!/usr/bin/env python3
"""Provision a unique ESP-NOW scanner ID (slot number minus one).

Without --port this only generates an NVS image. With --port it writes the
entire 0x6000-byte NVS partition at 0x9000, replacing existing NVS contents.
Use unique slots 1..20 with the supplied NVS scanner build.
"""

import argparse
import os
import subprocess
import sys
import tempfile

NVS_OFFSET = 0x9000
NVS_SIZE = 0x6000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("slot", type=int,
                    help="physical slot, 1 to 20 for this build; up to 32 for "
                         "espnow scaling runs")
    ap.add_argument("--out", help="output file, defaults to slot<N>.bin")
    ap.add_argument("--port", help="flash it to this port once built")
    args = ap.parse_args()

    # The generic format supports 32 IDs; the supplied cluster accepts 20.
    if not 1 <= args.slot <= 32:
        sys.exit("slot must be 1 to 32, slot 0 is the master and is not a scanner")

    node_id = args.slot - 1
    out = args.out or "slot%d.bin" % args.slot

    # the csv the generator wants. node_id is what the firmware reads
    csv = "key,type,encoding,value\n" \
          "hellzgate,namespace,,\n" \
          "node_id,data,u8,%d\n" % node_id

    fd, path = tempfile.mkstemp(suffix=".csv", text=True)
    with os.fdopen(fd, "w") as f:
        f.write(csv)

    try:
        subprocess.run(
            [sys.executable, "-m", "esp_idf_nvs_partition_gen", "generate",
             path, out, str(NVS_SIZE)],
            check=True)
    finally:
        os.unlink(path)

    print("slot %d, node_id %d, written to %s" % (args.slot, node_id, out))

    if args.port:
        subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32c5",
             "-p", args.port, "write_flash", hex(NVS_OFFSET), out],
            check=True)
    else:
        print("flash it with")
        print("  python -m esptool --chip esp32c5 -p PORT write_flash 0x%x %s"
              % (NVS_OFFSET, out))


if __name__ == "__main__":
    main()
