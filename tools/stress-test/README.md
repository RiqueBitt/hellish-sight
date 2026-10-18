# LCE Server Stress Test

Rapid bot connect/disconnect cycles to stress test server thread safety.

## Requirements

- Python 3.10+
- `pycryptodome` (only if server has cipher enabled)

```
pip install pycryptodome
```

## Usage

```bash
# Basic: 50 cycles, 8 concurrent bots, 2-10s hold time
python stress_test.py 127.0.0.1 19132

# Aggressive: rapid join/leave with short hold times
python stress_test.py 127.0.0.1 19132 --bots 12 --hold 0.5 2 --ramp 0.2

# With movement packets (tests "moved wrongly" code path)
python stress_test.py 127.0.0.1 19132 --move --hold 5 15

# Burst mode: 4 bots join simultaneously
python stress_test.py 127.0.0.1 19132 --burst 4 --hold 1 3

# Run for 5 minutes continuously
python stress_test.py 127.0.0.1 19132 --cycles 0 --duration 300

# Quiet mode (only shows stats, no per-bot messages)
python stress_test.py 127.0.0.1 19132 --quiet
```

## What it tests

- **Player list thread safety**: Rapid add/remove from the players vector
- **Socket write races**: Disconnect while server is mid-write
- **Movement validation**: `--move` flag sends position packets that trigger "moved wrongly" checks
- **Concurrent join/leave**: `--burst` flag joins multiple bots at once

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--bots N` | 8 | Max concurrent bot connections |
| `--cycles N` | 50 | Total connect/disconnect cycles (0 = infinite) |
| `--hold MIN MAX` | 2 10 | Random hold time range before disconnect (seconds) |
| `--ramp SECS` | 0.5 | Delay between spawning bots |
| `--move` | off | Send movement packets while connected |
| `--burst N` | 1 | Bots to spawn simultaneously per cycle |
| `--duration SECS` | 0 | Time limit (0 = run until cycles complete) |
| `--quiet` | off | Only show periodic stats, not per-bot logs |
