# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 2.0.x   | :white_check_mark: |
| < 2.0   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability within swl, please send an email to saikhsahil4883@gmail.com. All security vulnerabilities will be promptly addressed.

**Please do not report security vulnerabilities through public GitHub issues.**

## Security Considerations

### Privilege Dropping

swl supports running as root to read system logs (e.g., `/var/log/syslog`) and then dropping privileges:

```bash
swl --user syslog /var/log/syslog
```

When `--user` is specified:
1. Opens the log file as root
2. Drops to the specified user via `setuid()`/`setgid()`
3. Continues monitoring with reduced privileges

### File Descriptor Leaks

All file descriptors are opened with `O_CLOEXEC` (or `IN_CLOEXEC` for inotify) to prevent leaking into child processes.

### Signal Handling

Signal handlers use `_exit()` instead of `std::exit()` to avoid undefined behavior and resource leaks.

### Webhook Security

- SSL certificate verification is enabled by default
- Use `--no-ssl-verify` only for self-signed certificates in trusted environments
- Webhook URLs should use HTTPS for production

### PID File

- PID file is written after signal handlers are installed (race condition fixed)
- Stale PID files are handled gracefully

### Configuration File Permissions

Config files may contain sensitive information (webhook URLs). Ensure proper file permissions:

```bash
chmod 600 ~/.config/swl/config
chmod 600 /etc/swl/config
```

### Incident Files

Incident files contain log context which may include sensitive information. Ensure the incidents directory has appropriate permissions:

```bash
chmod 700 /var/log/swl-incidents
```

## Best Practices

### Production Deployment

1. **Use a dedicated user**:
   ```bash
   useradd -r -s /bin/false swl
   swl --user swl /var/log/app.log
   ```

2. **Restrict config file access**:
   ```bash
   chown root:swl /etc/swl/config
   chmod 640 /etc/swl/config
   ```

3. **Use systemd service** with hardening (see `init/swl.service`):
   ```ini
   [Service]
   User=swl
   Group=swl
   ProtectSystem=strict
   ProtectHome=true
   NoNewPrivileges=true
   PrivateTmp=true
   ```

4. **Monitor incident directory**:
   ```bash
   # Add to crontab
   0 0 * * * find /var/log/swl-incidents -mtime +30 -delete
   ```

### Network Security

- Use HTTPS webhooks in production
- Consider VPN or private networks for webhook endpoints
- Rotate webhook URLs periodically

### Log File Permissions

- Ensure log files are readable by the swl process
- Use `--user` to match the log file owner
- Avoid running as root when possible

## Known Security Limitations

1. **Linux-only**: inotify is Linux-specific
2. **No encryption**: Incident files are plain text
3. **No authentication**: Webhook endpoints should implement their own auth
4. **No rate limiting**: Cooldown only applies per-trigger, not globally

## Updates

Security updates will be released as patch versions (e.g., 2.0.3). Subscribe to releases on GitHub for notifications.
