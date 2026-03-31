# Legion — Linux Daemon Process Manager
This project was built to explore how Linux handles long-running background processes. 
It focuses on coordinating parent/child processes, handling signals correctly, and 
ensuring reliable startup and cleanup without leaving zombie processes behind.

Legion provides a CLI to register, start, stop, and monitor daemons, along with log rotation support.

## Key Features
- Daemon lifecycle management (start/stop/restart)
- Sync-safe startup using pipe handshake with SIGALRM timeout
- Signal-safe CLI using select() + EINTR handling
- Zombie process prevention via SIGCHLD + waitpid
- Log rotation with versioned files and automatic restart

## Commands

| Command | Args | Description |
|--------|------|-------------|
| `register` | `<name> <cmd> [args...]` | Register a new daemon |
| `unregister` | `<name>` | Unregister an inactive daemon |
| `start` | `<name>` | Start a registered daemon |
| `stop` | `<name>` | Stop a running daemon |
| `status` | `<name>` | Print the status of a daemon |
| `status-all` | — | Print status of all registered daemons |
| `logrotate` | `<name>` | Rotate log files for an active daemon |
| `quit` | — | Stop all daemons and exit |
| `help` | — | Print available commands |

## Daemon States

status_inactive → status_starting → status_active → status_stopping → status_exited
                                                                     → status_crashed
## Build

```bash
make
./legion
```

