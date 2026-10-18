#!/usr/bin/env python3
"""
LCE Server Performance Monitor -- server-side instrumentation GUI.

Connects to a performance monitoring DLL injected into the running
Minecraft.Server.exe process.  Displays real-time tick phase breakdowns,
entity/chunk counts, memory usage, and lag spike root causes.

Requires the DLL to be injected first:
    python inject.py

Usage:
    python main.py
    python main.py --host 127.0.0.1 --port 19800
"""

import sys
import os
import logging
import argparse

from PySide6.QtWidgets import QApplication
from gui import PerfMonitorWindow


def setup_logging():
    """Configure file logger next to the executable/script."""
    if getattr(sys, "frozen", False):
        base = os.path.dirname(sys.executable)
    else:
        base = os.path.dirname(os.path.abspath(__file__))

    log_path = os.path.join(base, "perfmon.log")

    logger = logging.getLogger("perfmon")
    logger.setLevel(logging.DEBUG)

    fh = logging.FileHandler(log_path, mode="w", encoding="utf-8")
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(logging.Formatter("%(asctime)s %(message)s", datefmt="%H:%M:%S"))

    logger.addHandler(fh)
    logger.info(f"Log file: {log_path}")
    return logger


def main():
    parser = argparse.ArgumentParser(description="LCE Server Performance Monitor")
    parser.add_argument("--host", default=None, help="Monitor host address")
    parser.add_argument("--port", type=int, default=None, help="Monitor port")
    args = parser.parse_args()

    setup_logging()

    app = QApplication(sys.argv)
    app.setApplicationName("LCE Server Performance Monitor")

    window = PerfMonitorWindow()

    if args.host:
        window._host_input.setText(args.host)
    if args.port:
        window._port_input.setValue(args.port)

    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
