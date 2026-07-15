# SHOCKWAKE-LOG

High-performance real-time log anomaly detection with contextual debugging.

## Overview

`shockwake-log` monitors application logs and instantly captures the full context when failures occur. Instead of just finding an error line, it records exactly what happened before and after the incident — with near-zero CPU and memory overhead.

**Key Features:**
- Kernel-level file monitoring (no polling, no CPU waste)
- Sliding context window (configurable pre/post trigger lines)
- Instant webhook alerts (Discord, Slack, custom endpoints)
- Incident reports with full context snapshots

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    SHOCKWAKE-LOG                        │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────────┐    ┌──────────────┐    ┌───────────┐ │
│  │   inotify    │───▶│  Ring Buffer │───▶│  Scanner  │ │
│  │   Watcher    │    │   (50 lines) │    │ (string_view)│
│  └──────────────┘    └──────────────┘    └─────┬─────┘ │
│                                                 │       │
│                                          Trigger Found  │
│                                                 │       │
│                           ┌─────────────────────┼───────┘
│                           │                     │
│                           ▼                     ▼
│                    ┌──────────┐         ┌─────────────┐
│                    │ Incident │         │   Webhook   │
│                    │  Report  │         │   Alert     │
│                    └──────────┘         └─────────────┘
│                                                │
│                                    ┌───────────┴───────┐
│                                    ▼                   ▼
│                              Discord/Slack      Custom Endpoint
└─────────────────────────────────────────────────────────┘
```

## Requirements

- C++17 compiler (GCC 7+ / Clang 5+)
- libcurl development files
- Linux (uses inotify)

## Building

```bash
# Clone repository
git clone https://github.com/yourusername/shockwake-log.git
cd shockwake-log

# Build with CMake
mkdir build && cd build
cmake ..
make

# Or build directly
g++ -std=c++17 src/*.cpp -Iinclude -lcurl -lpthread -o shockwake-log
```

## Usage

```bash
# Basic usage
./shockwake-log --log /var/log/app.log --webhook https://discord.com/api/webhooks/...

# With custom options
./shockwake-log \
  --log /var/log/app.log \
  --webhook https://hooks.slack.com/services/... \
  --triggers FATAL,ERROR,500,503 \
  --window 100 \
  --trailing 20 \
  --incidents /var/log/incidents
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `--log <path>` | Log file to monitor | *required* |
| `--webhook <url>` | Alert endpoint URL | *required* |
| `--triggers <k1,k2>` | Comma-separated trigger keywords | `FATAL,ERROR` |
| `--window <n>` | Lines to keep in context buffer | `50` |
| `--trailing <n>` | Lines to capture after trigger | `10` |
| `--incidents <dir>` | Directory for incident reports | `./incidents` |

## Incident Reports

When a trigger is detected, an incident report is generated:

```
=== SHOCKWAKE-LOG INCIDENT REPORT ===
Timestamp: 20260715_213415
Trigger: FATAL
Log File: /var/log/app.log
=====================================

--- PRE-TRIGGER CONTEXT ---
2024-01-15 10:23:45 INFO: User logged in
2024-01-15 10:23:46 DEBUG: Processing request
...

--- TRIGGER LINE ---
>>> FATAL DETECTED <<<

--- POST-TRIGGER CONTEXT ---
2024-01-15 10:23:47 ERROR: Connection lost
...
```

## Webhook Format

Alerts are sent as JSON payloads:

```json
{
  "content": "🚨 **SHOCKWAKE-LOG ALERT**\nTrigger: FATAL\nLog: /var/log/app.log\nIncident: ./incidents/incident_20260715.log"
}
```

## Testing

```bash
cd build && ctest
```

## License

MIT License. See [LICENSE](LICENSE) for details.
