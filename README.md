# TCL Display

A small native Windows background app paired with a Home Assistant integration exposing **one display switch**. [Инструкция на русском](README.ru.md).

Switch on connects the selected monitor to the Windows desktop; switch off disconnects it, like Windows Display Settings. It does not control TV power. Status is polled every 15 seconds and confirmed immediately after commands. An offline PC or missing monitor makes the switch unavailable. Setup and polling never change the display automatically.

## Windows

Download `TCL-Display-Windows-v1.4.1.zip` from the [latest release](https://github.com/BrainDeLook/tcl-display/releases/latest). Extract to a permanent folder and start the EXE. Select the display, check the PC address, enter your HA IPv4 address for the firewall rule, save, allow HA access through the Windows prompt, and start the listener. Copy the connection code from the HA setup tab.

The app uses C/Win32 with no Python/.NET runtime. Tray visibility and close-to-background versus exit are independent settings. Relaunching shows a hidden window. The Exit button always exits. For upgrades, exit the old app and replace its EXE while keeping `agent.ini`.

## Home Assistant

1. In regular HACS, add `https://github.com/BrainDeLook/tcl-display` as a custom **Integration** repository. Install TCL Display and restart HA. Existing installations: update to 0.2.0 and restart.
2. Settings → Devices & services → Add integration → TCL Display. Paste the Windows connection code and optionally name the device.
3. The device exposes a single **Windows display** switch. Use its actual entity ID with `switch.turn_on` / `switch.turn_off` in your existing scripts or automations.

No TV, volume, ADB, source scripts or media player configuration is required. Connect this switch to your existing media player yourself. Version 0.1.0 settings migrate automatically: the display switch identity is preserved; obsolete media player, auto-sync switch and connection sensor belonging to this integration are removed. Existing Universal Players are untouched. Reconfigure the integration to change address/key.

The code contains a secret: keep it private. Use only on a trusted LAN (HTTP with key, no TLS); never forward the port to the internet. Reserve the PC IP in DHCP. The firewall helper allows the specified HA IPv4 on private networks.

The last active display cannot be disabled. Enabling adds the display to the right of existing monitors; full HDR/refresh/layout profiles are not restored. Driver behavior needs verification on your hardware.

## Development

HA code: `custom_components/tcl_display`. Native app: `windows/src`; build with `windows/build.ps1` and Zig 0.13. CI runs protocol tests, real Home Assistant integration tests, hassfest and HACS validation.
