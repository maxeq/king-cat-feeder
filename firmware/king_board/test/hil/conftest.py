"""Hardware-in-the-loop tests: run against a real board on the network.

    KING_HOST=<board-ip> pytest firmware/king_board/test/hil              # read-only checks
    KING_HOST=<board-ip> pytest firmware/king_board/test/hil --actuators  # also moves things

KING_USER / KING_PASS default to the web login in secrets.h.example. Without
KING_HOST every test is skipped, which is what happens in CI.
"""
import base64
import json
import os
import socket
import time
import urllib.error
import urllib.request

import pytest


def pytest_addoption(parser):
    parser.addoption("--actuators", action="store_true", help="allow tests that move the door, light, rotor")


def pytest_configure(config):
    config.addinivalue_line("markers", "actuators: moves the door, light or rotor")


def pytest_collection_modifyitems(config, items):
    if not os.environ.get("KING_HOST"):
        skip = pytest.mark.skip(reason="set KING_HOST to run hardware tests")
        for item in items:
            item.add_marker(skip)
    elif not config.getoption("--actuators"):
        skip = pytest.mark.skip(reason="moves hardware: pass --actuators")
        for item in items:
            if "actuators" in item.keywords:
                item.add_marker(skip)


class Board:
    def __init__(self, host, user, password):
        self.host = host
        self.password = password  # the web password, which telnet asks for too
        self.auth = "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()

    def get(self, path, auth=True, timeout=5.0, method="GET", headers=None):
        req = urllib.request.Request(f"http://{self.host}{path}", method=method, headers=headers or {})
        if auth:
            req.add_header("Authorization", self.auth)
        t0 = time.monotonic()
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.read().decode("utf-8", "replace"), time.monotonic() - t0
        except urllib.error.HTTPError as e:
            return e.code, "", time.monotonic() - t0

    def json(self, path):
        status, body, _ = self.get(path)
        assert status == 200, f"{path}: HTTP {status}"
        return json.loads(body)

    def status(self):
        _, body, _ = self.get("/status")
        out = {}
        for line in body.splitlines():
            key, _, value = line.partition(":")
            out[key.strip()] = value.strip()
        return out

    def telnet(self, password=None):
        """A logged-in telnet socket (password prompt answered, banner read)."""
        s = socket.create_connection((self.host, 23), timeout=5)
        s.recv(256)  # "password:" prompt
        s.sendall(((self.password if password is None else password) + "\n").encode())
        time.sleep(0.3)
        s.recv(512)  # banner, or "wrong password"
        return s

    def command(self, keys, settle=0.3):
        """Send one-letter console commands over telnet and return what the board printed."""
        with self.telnet() as s:
            s.sendall(keys.encode())
            time.sleep(settle)
            s.settimeout(1.0)
            out = b""
            try:
                while chunk := s.recv(4096):
                    out += chunk
            except TimeoutError:
                pass
        return out.decode("utf-8", "replace")

    def wait_for(self, predicate, timeout=5.0, every=0.2):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate():
                return True
            time.sleep(every)
        return False


@pytest.fixture(scope="session")
def board():
    env = os.environ
    return Board(env["KING_HOST"], env.get("KING_USER", "admin"), env.get("KING_PASS", "change-me"))
