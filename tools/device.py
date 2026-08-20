#!/usr/bin/env python3
"""Talk to an RSSpaper device over its serial console.

The device has no keyboard and its card does not come out, so this is how a
file gets onto it, how the card is inspected, and how a compose is asked for
without editing the firmware to force one.

    tools/device.py log [--no-reset]     stream the serial output
    tools/device.py ls                   what is on the card
    tools/device.py put FILE [AS]        copy a file onto the card
    tools/device.py rm PATH              remove a file from the card
    tools/device.py compose              fetch and compose now

The board is found automatically. Set RSSPAPER_PORT if a machine has more
than one attached.

A file's bytes go straight from disk to the port and are never printed, which
is what makes it safe to send a feeds.toml with wifi credentials in it.

The console it talks to is open for a moment after boot and speaks one line
at a time:

    PUT <path> <bytes>   followed by exactly that many bytes
    LS                   list the card
    RM <path>            remove one file
    COMPOSE              take the compose path on this boot
    GO                   stop listening

Every command answers with a line starting OK or ERR.
"""
import glob
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit(
        "tools/device.py needs pyserial.\n"
        "  pip install pyserial\n"
        "or use the python PlatformIO ships, which already has it:\n"
        "  ~/.platformio/penv/bin/python tools/device.py ...")

BAUD = 115200
PORT_PATTERNS = (
    "/dev/cu.usbserial*",   # macOS, CH340 and FTDI
    "/dev/cu.SLAB*",        # macOS, CP210x
    "/dev/cu.wchusb*",      # macOS, some CH34x drivers
    "/dev/ttyUSB*",         # Linux
    "/dev/ttyACM*",         # Linux, native-USB boards
)


class DeviceError(Exception):
    """Something the user can act on, rather than a traceback."""


def find_port():
    env = os.environ.get("RSSPAPER_PORT")
    if env:
        if not os.path.exists(env):
            raise DeviceError("RSSPAPER_PORT is %s, which is not there." % env)
        return env
    found = []
    for pattern in PORT_PATTERNS:
        found.extend(sorted(glob.glob(pattern)))
    if not found:
        raise DeviceError(
            "No board found. Plug one in, or set RSSPAPER_PORT.\n"
            "Looked for: " + ", ".join(PORT_PATTERNS))
    if len(found) > 1:
        raise DeviceError(
            "More than one board is attached:\n  " + "\n  ".join(found) +
            "\nPick one with RSSPAPER_PORT.")
    return found[0]


def open_port(reset=True):
    port = find_port()
    try:
        s = serial.Serial(port, BAUD, timeout=2)
    except serial.SerialException as e:
        raise DeviceError("Cannot open %s: %s\n"
                          "Something else may be holding it — a monitor, or "
                          "another copy of this script." % (port, e))
    if reset:
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.2)
        s.setRTS(False)
        time.sleep(0.3)
        s.reset_input_buffer()
    return s


def wait_for_ready(s, seconds=25):
    """The console opens for a moment after boot and announces itself."""
    end = time.time() + seconds
    while time.time() < end:
        line = s.readline()
        if line and line.strip() == b"READY":
            return
    raise DeviceError(
        "The device never opened its console.\n"
        "It may be running firmware without one — reflash with "
        "`make device-flash`.")


def read_reply(s, seconds=25):
    """Lines until one starts OK or ERR. Returns (status, lines_before)."""
    end = time.time() + seconds
    before = []
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        txt = line.decode("utf-8", "replace").rstrip()
        if txt.startswith("OK") or txt.startswith("ERR"):
            return txt, before
        before.append(txt)
    raise DeviceError("The device stopped replying part-way through.")


def run_command(command, seconds=25):
    s = open_port()
    try:
        wait_for_ready(s)
        s.write((command + "\n").encode())
        s.flush()
        status, before = read_reply(s, seconds)
        for line in before:
            print(line)
        print(status)
        s.write(b"GO\n")
        return 0 if status.startswith("OK") else 1
    finally:
        s.close()


def cmd_log(args):
    s = open_port(reset="--no-reset" not in args)
    print("streaming %s — ctrl-c to stop" % s.port, file=sys.stderr)
    try:
        while True:
            line = s.readline()
            if line:
                print(line.decode("utf-8", "replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        return 0
    finally:
        s.close()


def cmd_ls(_args):
    return run_command("LS")


def cmd_rm(args):
    if not args:
        raise DeviceError("usage: device.py rm PATH")
    return run_command("RM " + args[0])


def cmd_compose(_args):
    """Ask for a compose and follow it: fetching and laying out take minutes."""
    s = open_port()
    try:
        wait_for_ready(s)
        s.write(b"COMPOSE\nGO\n")
        s.flush()
        end = time.time() + 600
        saw_end = False
        while time.time() < end:
            line = s.readline()
            if not line:
                continue
            txt = line.decode("utf-8", "replace").rstrip()
            if txt:
                print(txt, flush=True)
            if "next edition in" in txt or "keeping the paper" in txt:
                saw_end = True
                break
        if not saw_end:
            print("(gave up waiting; the device may still be composing)",
                  file=sys.stderr)
            return 1
        return 0
    finally:
        s.close()


def cmd_put(args):
    if not args:
        raise DeviceError("usage: device.py put FILE [AS]")
    local = args[0]
    if not os.path.isfile(local):
        raise DeviceError("No such file: %s" % local)
    remote = args[1] if len(args) > 1 else "/" + os.path.basename(local)
    if not remote.startswith("/"):
        remote = "/" + remote
    size = os.path.getsize(local)
    if size == 0:
        raise DeviceError("%s is empty; the device would reject it." % local)

    s = open_port()
    try:
        wait_for_ready(s)
        s.write(("PUT %s %d\n" % (remote, size)).encode())
        s.flush()
        with open(local, "rb") as f:
            while True:
                chunk = f.read(256)
                if not chunk:
                    break
                s.write(chunk)   # never printed: this may hold credentials
                s.flush()
                time.sleep(0.01)
        status, _ = read_reply(s)
        print(status)
        s.write(b"GO\n")
        return 0 if status.startswith("OK") else 1
    finally:
        s.close()


COMMANDS = {
    "log": cmd_log,
    "ls": cmd_ls,
    "put": cmd_put,
    "rm": cmd_rm,
    "compose": cmd_compose,
}

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print(__doc__)
        sys.exit(2)
    try:
        sys.exit(COMMANDS[sys.argv[1]](sys.argv[2:]))
    except DeviceError as err:
        sys.exit("%s" % err)
