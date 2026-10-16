"""Checks a running King board end to end: version, web, console, and that slow
work (the camera) never blocks the rest. See conftest.py for how to run."""
import re
import time
from pathlib import Path

import pytest

VERSION_H = Path(__file__).resolve().parents[2] / "src" / "version.h"


def firmware_version():
    return re.search(r'#define FW_VERSION "([^"]+)"', VERSION_H.read_text()).group(1)


def test_version_matches_source(board):
    v = board.json("/version")
    assert v["version"] == firmware_version()
    assert v["git"] and v["built"]


def test_web_requires_login(board):
    status, _, _ = board.get("/status", auth=False)
    assert status == 401


def test_status_has_every_field(board):
    s = board.status()
    for key in ("WEIGHT", "PIR", "DOOR", "LIGHT", "AUTO", "FW"):
        assert key in s, f"missing {key} in /status"


def test_status_stays_fast(board):
    times = [board.get("/status")[2] for _ in range(10)]
    assert max(times) < 0.5, f"slowest /status took {max(times):.2f} s"


def test_camera_never_blocks_the_web(board):
    status, body, took = board.get("/frame?since=0", timeout=3)
    assert status == 200 and took < 1.5, f"/frame took {took:.2f} s"
    # while the camera worker is busy with the frame just requested, the rest stays responsive
    assert board.get("/status")[2] < 0.5


def test_health_words(board):
    h = board.json("/health")
    assert isinstance(h["faults"], int) and isinstance(h["states"], int)
    assert h["tickMaxUs"] < 10000, f"a control tick took {h['tickMaxUs']} us, over its 10 ms slot"
    assert h["overruns"] == 0
    s = board.status()
    assert s["FAULTS"].startswith(f"0x{h['faults']:04X}")


def test_health_reports_every_margin(board):
    h = board.json("/health")
    assert h["valid"] and h["resetReason"]
    for task, free in h["stackFree"].items():
        assert free >= 512, f"{task} task is close to a stack overflow: {free} bytes left"
    assert h["heapMaxBlock"] >= 16 * 1024, "largest free heap block is small: fragmentation?"
    assert h["logDropped"] == 0, "log lines were lost"
    assert h["lockTimeouts"] == 0
    assert h["errors"] == 0, "the board logged an error: see /log (lines start with !!)"


def test_state_changes_need_post_and_the_header(board):
    assert board.get("/light?m=pir")[0] == 403  # GET
    assert board.get("/light?m=pir", method="POST")[0] == 403  # no X-King
    assert board.get("/light?m=pir", method="POST", headers={"X-King": "1"})[0] == 200
    assert board.json("/bright")["pct"] >= 0  # reading needs no POST


def test_telnet_needs_the_password(board):
    with board.telnet(password="not-the-password") as s:
        s.sendall(b"?")
        time.sleep(0.5)
        s.settimeout(1.0)
        try:
            out = s.recv(4096).decode("utf-8", "replace")
        except TimeoutError:
            out = ""
    assert "IP " not in out, "telnet ran a command without the password"


def test_console_reports_version_and_heap(board):
    out = board.command("?")
    assert f"fw {firmware_version()}" in out
    m = re.search(r"heap (\d+)k free, (\d+)k max block", out)
    assert m, out
    assert int(m.group(2)) >= 16, "largest free heap block is small: fragmentation?"


@pytest.mark.actuators
def test_door_opens_and_closes(board):
    board.command("o")
    assert board.wait_for(lambda: board.status()["DOOR"].startswith("OPEN"), timeout=6)
    board.command("c")
    assert board.wait_for(lambda: board.status()["DOOR"].startswith("closed"), timeout=6)


@pytest.mark.actuators
def test_light_ramps_on_and_off(board):
    board.command("L")
    assert board.wait_for(lambda: board.status()["LIGHT"] != "0%", timeout=2)
    board.command("l")
    assert board.wait_for(lambda: board.status()["LIGHT"] == "0%", timeout=2)


@pytest.mark.actuators
def test_rotor_pulse_stops_on_its_own(board):
    out = board.command("3", settle=1.0)  # 300 ms pulse
    assert "rotor: pulse 300 ms" in out
    assert board.wait_for(lambda: "rotor: STOP" in board.get("/log")[1], timeout=3)
