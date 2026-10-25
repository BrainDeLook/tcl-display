# TCL Display

A small native Windows background app paired with a Home Assistant integration exposing **a display switch and a primary-display preference switch**. [Инструкция на русском](README.ru.md).

Switch on connects the selected monitor to the Windows desktop; switch off disconnects it, like Windows Display Settings. It does not control TV power. Status is polled every 15 seconds and confirmed immediately after commands. An offline PC or missing monitor makes the switch unavailable. Setup and polling never change the display automatically.

## Windows

Download `TCL-Display-Windows-v1.5.zip` from the [latest release](https://github.com/BrainDeLook/tcl-display/releases/latest). Extract to a permanent folder and start the EXE. Select the display, check the PC address, enter your HA IPv4 address for the firewall rule, save, allow HA access through the Windows prompt, and start the listener. Copy the connection code from the HA setup tab.

The app uses C/Win32 with no Python/.NET runtime. Tray visibility and close-to-background versus exit are independent settings. Relaunching shows a hidden window. The Exit button always exits. For upgrades, exit the old app and replace its EXE while keeping `agent.ini`.

## Home Assistant

1. In regular HACS, add `https://github.com/BrainDeLook/tcl-display` as a custom **Integration** repository. Install TCL Display and restart HA. Existing installations: update to 0.3.0 and restart.
2. Settings → Devices & services → Add integration → TCL Display. Paste the Windows connection code and optionally name the device.
3. The device exposes **Windows display** and a configuration switch, **Make TV primary** (off by default). Use its actual entity ID with `switch.turn_on` / `switch.turn_off` in your existing scripts or automations.

No TV, volume, ADB, source scripts or media player configuration is required. Connect this switch to your existing media player yourself. Version 0.1.0 settings migrate automatically: the display switch identity is preserved; obsolete media player, auto-sync switch and connection sensor belonging to this integration are removed. Existing Universal Players are untouched. Reconfigure the integration to change address/key.

The code contains a secret: keep it private. Use only on a trusted LAN (HTTP with key, no TLS); never forward the port to the internet. Reserve the PC IP in DHCP. The firewall helper allows the specified HA IPv4 on private networks.

The last active display cannot be disabled. Enabling adds the display to the right of existing monitors; full HDR/refresh/layout profiles are not restored. Driver behavior needs verification on your hardware.

## Make TV primary

Enable the separate **Make TV primary** configuration switch. It stores a preference without changing the desktop immediately. On the next display-on command, the app remembers the current primary monitor and makes the TV primary; display-off restores the previous primary. A repeated on command also works when the TV is already connected.

The previous monitor is saved in `agent.ini` before applying changes and survives app/Windows restarts. Repeated on commands do not overwrite it with the TV. If the old monitor is unplugged, a remaining active monitor becomes primary. Disabling the preference while the TV is primary still preserves the pending restoration on display-off. Restoration requires an off command through the app/HA; unplugging the cable alone does not trigger it.

The preference is also available in the Windows app's Behavior tab and saves immediately. Update the Windows app to 1.5; older agents keep their display control but cannot expose the new preference. No re-pairing is required. Primary switching translates desktop coordinates as described in [Microsoft's source-mode documentation](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_source_mode).

## Development

HA code: `custom_components/tcl_display`. Native app: `windows/src`; build with `windows/build.ps1` and Zig 0.13. CI runs protocol tests, real Home Assistant integration tests, hassfest and HACS validation.

Native regression tests: `windows/test.ps1 -Zig <path-to-zig.exe>`; synthetic topology, no physical display changes.
