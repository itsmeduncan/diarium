#!/usr/bin/env python3
"""Talk to an RSSpaper device over its serial console.

The device has no keyboard and its card does not come out, so this is how a
file gets onto it, how the card is inspected, and how a compose is asked for
without editing the firmware to force one.

    tools/device.py log                       stream the serial output
    tools/device.py ls                        what is on the card
    tools/device.py put FILE [AS]             copy a file onto the card
    tools/device.py rm PATH                   remove a file from the card
    tools/device.py compose                   fetch and compose now

A file's bytes go straight from disk to the port and are never printed, which
is what makes it safe to send a feeds.toml with wifi credentials in it.
"""
import os
import sys
import time

import glob

try:
    import serial
except ImportError:
    sys.exit("needs pyserial: pip install pyserial, or use the python "
             "PlatformIO ships, which already has it.")


def find_port():
    """The board, wherever it turned up. A hardcoded device node is one
    person's USB slot on one afternoon."""
    env = os.environ.get("RSSPAPER_PORT")
    if env:
        return env
    for pattern in ("/dev/cu.usbserial*", "/dev/cu.SLAB*", "/dev/cu.wchusb*",
                    "/dev/ttyUSB*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    sys.exit("no board found. Plug one in, or set RSSPAPER_PORT.")


PORT = find_port()
BAUD = 115200


def open_and_reset(reset=True):
    s = serial.Serial(PORT, BAUD, timeout=2)
    if reset:
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.2)
        s.setRTS(False)
        time.sleep(0.3)
        s.reset_input_buffer()
    return s


def wait_for_ready(s, seconds=20):
    """The console opens for a moment after boot and says so."""
    end = time.time() + seconds
    while time.time() < end:
        line = s.readline()
        if line and line.strip() == b"READY":
            return True
    return False


def read_reply(s, seconds=20):
    end = time.time() + seconds
    out = []
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        txt = line.decode("utf-8", "replace").rstrip()
        if txt.startswith("OK") or txt.startswith("ERR"):
            return txt, out
        out.append(txt)
    return None, out


def cmd_log(_args):
    s = open_and_reset()
    print("streaming %s — ctrl-c to stop" % PORT)
    try:
        while True:
            line = s.readline()
            if line:
                print(line.decode("utf-8", "replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
    return 0


def _simple(command, seconds=30):
    s = open_and_reset()
    if not wait_for_ready(s):
        s.close()
        return "device never opened its console"
    s.write((command + "\n").encode())
    s.flush()
    status, lines = read_reply(s, seconds)
    for l in lines:
        print(l)
    s.write(b"GO\n")
    s.close()
    if status is None:
        return "no reply to %s" % command
    print(status)
    return None if status.startswith("OK") else status


def cmd_ls(_args):
    err = _simple("LS")
    if err:
        print(err)
        return 1
    return 0


def cmd_rm(args):
    if not args:
        print("usage: device.py rm PATH")
        return 2
    err = _simple("RM " + args[0])
    if err:
        print(err)
        return 1
    return 0


def cmd_compose(_args):
    """Ask for a compose and follow it: fetching and laying out take minutes."""
    s = open_and_reset()
    if not wait_for_ready(s):
        s.close()
        print("device never opened its console")
        return 1
    s.write(b"COMPOSE\nGO\n")
    s.flush()
    end = time.time() + 420
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        txt = line.decode("utf-8", "replace").rstrip()
        if txt:
            print(txt, flush=True)
        if "next edition in" in txt or "keeping the paper" in txt:
            break
    s.close()
    return 0


def cmd_put(args):
    if not args:
        print("usage: device.py put FILE [AS]")
        return 2
    local = args[0]
    remote = args[1] if len(args) > 1 else "/" + os.path.basename(local)
    size = os.path.getsize(local)

    s = open_and_reset()
    if not wait_for_ready(s):
        s.close()
        print("device never opened its console")
        return 1

    s.write(("PUT %s %d\n" % (remote, size)).encode())
    s.flush()
    with open(local, "rb") as f:
        while True:
            chunk = f.read(256)
            if not chunk:
                break
            s.write(chunk)      # never printed: this may hold credentials
            s.flush()
            time.sleep(0.01)

    status, _ = read_reply(s, 20)
    s.write(b"GO\n")
    s.close()
    print(status or "no reply")
    return 0 if status and status.startswith("OK") else 1


COMMANDS = {"log": cmd_log, "ls": cmd_ls, "put": cmd_put, "rm": cmd_rm,
            "compose": cmd_compose}

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print(__doc__)
        sys.exit(2)
    sys.exit(COMMANDS[sys.argv[1]](sys.argv[2:]) or 0)
