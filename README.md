# TCL Display

[Русская инструкция](README.ru.md)

A HACS custom integration that combines a TCL Android TV, existing input scripts and a Windows display into a single Home Assistant media player. Setup is entirely in the HA user interface. No `configuration.yaml`, input helpers or separate automations are required.

The small native Windows application connects/disconnects only the selected TV from the Windows desktop. Home Assistant handles source detection and synchronization.

## Features

- UI setup: paste one connection code, choose the TV, optional number-based volume entity and existing source scripts.
- A media player with actual TV power and input feedback; HDMI 1/2/3 and native Android apps.
- Automatic Windows display connection on HDMI 3 and disconnection on other confirmed sources / TV power off.
- Unknown or ambiguous input readings do not disconnect the display.
- Manual PC display switch, automatic-sync switch, and Windows connectivity sensor.
- Configurable tray icon and close-button behavior in the Windows app.
- Local authenticated HTTP; no cloud, MQTT, Python or .NET runtime on the PC.

## Requirements

- Home Assistant 2026.10 or newer and HACS.
- The TCL TV already configured using **Android Debug Bridge**, with a working `media_player` entity.
- Existing scripts for input changes. HDMI 3 is reserved for this Windows PC; other inputs are optional.
- Windows 10/11 x64, connected TV and a logged-in local Windows user.

The source detector uses TCL hardware paths exposed by `dumpsys tv_input`. TCL firmware varies. Fixtures cover the observed hardware-path and CEC-input formats; actual input detection on a different TV must be checked.

## Installation through HACS

1. In HACS, open **Custom repositories** and add `https://github.com/BrainDeLook/tcl-display` as **Integration**.
2. Download **TCL Display** and restart Home Assistant.
3. Download the Windows application ZIP from [Releases](https://github.com/BrainDeLook/tcl-display/releases), extract it to a permanent folder and run `TCL-Display.exe`.
4. Select the TV display. Verify the PC LAN IPv4, enter HA's IPv4, save, allow HA access and start the listener. Test the display buttons yourself.
5. On the Windows **Home Assistant setup** tab, copy the connection code.
6. In HA: **Settings → Devices & services → Add integration → TCL Display**. Paste the code, choose the physical TV / volume entity and input scripts from dropdowns.

This is installation through a HACS custom repository, not inclusion in the default HACS catalogue.

## Daily use

Use the new media player to select an input. For HDMI 3 it runs the configured input script, waits for HDMI discovery, then connects the Windows display. When switching with the physical remote, feedback is polled about every 15 seconds. TV power changes also request an update.

The PC-display switch is useful for manual operation. Disable **Automatic synchronization** first if the manual setting should persist; otherwise the next confirmed TV update restores the automatic state.

Change TV/source settings with the integration's **Configure** action. Change connection details with **Reconfigure**. With an invalid key HA requests reauthentication.

## Migrating from the earlier YAML configuration

After verifying the new integration, disable the previous ADB source-detection and Windows-sync automations. They would otherwise send duplicate or conflicting commands. Choose the original per-input scripts (`tcl_hdmi_1`, `tcl_hdmi_2`, `google_tv_domoj`, `tcl_hdmi_3`), not the old selector wrapper. The old Universal media player/helper can then be removed if unused elsewhere. Keep the existing Android Debug Bridge integration and base input scripts.

## Windows application

The app is portable, one native process. It saves settings beside the EXE. Copy your existing `agent.ini` when upgrading. Tray visibility and close-button behavior are independent. When hidden without an icon, starting the EXE again reopens the existing process. **Exit** always stops the listener. Autostart runs in the user's session, not as a Windows service.

On enabling the TV display, Windows may choose a compatible video mode; this version does not save a complete HDR/refresh-rate/desktop-position profile. The TV is added to the right of existing screens. The app cannot wake a sleeping PC, and the graphics driver must see the TV. Keep PC and HA IPs stable using DHCP reservations.

The connection code includes a secret. Treat it like a password. The HTTP port is intended only for a trusted home LAN; do not expose it to the internet. The firewall button grants only the specified HA IPv4 in Windows' Private network profile, with a user-confirmed UAC prompt.

## Validation

CI runs Home Assistant setup/flow/entity tests, protocol/parser tests, hassfest and HACS validation. Local native testing checks display enumeration, HTTP authentication and window lifecycle without changing physical displays. Build/CI success does not replace a real TV/Windows/HA field test.

Windows sources and the build script are in `windows/`. Build with portable Zig 0.13.0. No binaries, connection codes, user configuration or raw TV logs are stored in Git.
