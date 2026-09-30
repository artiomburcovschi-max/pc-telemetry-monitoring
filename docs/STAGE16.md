# Stage 16 — native settings and hot UI behaviour

Stage 16 is an intermediate checkpoint of the Python/PySide6 to C++20/Qt 6
Widgets migration. It ports the settings structure and the behaviours that can
already be backed by complete native functionality; it does not claim final UI
or feature parity.

## Settings parity

The compact settings form was replaced with the original four sections:

- Appearance: Eclipse, Quantum Cyan, Matrix Terminal, Slate Minimal and Custom;
  50–100% opacity; four persisted Custom colours and explicit save/use actions.
- Window: persisted minimum/maximum geometry, always-on-top and free resize.
- Overview cards: independent CPU/GPU/RAM/storage/network visibility,
  movable/floating control and an immediate canonical-layout reset.
- Advanced: alarm-banner hold, tray icon and tray notifications. Terminal,
  Servers, Gamer Mode and the persisted ping target are visible but disabled
  until their full native producers are ported, so the UI does not advertise
  placeholder functionality. The former prototype-only CPU chart selector was
  removed from this panel because it is not part of the Python settings UI and
  does not yet have a native chart implementation.

Theme changes apply immediately to the main window, settings dialog and every
native sparkline. Unknown legacy theme keys normalize to Eclipse. Custom colours
are validated and preserved even when another preset is active.

## Runtime behaviour

Window constraints now follow the persisted Python values instead of using a
hard-coded enlarged geometry. Free resize removes both limits. Always-on-top is
reapplied through the required hide/flag/show sequence. Overview visibility and
dock features update without restart, and reset safely returns floating docks
before rebuilding the default layout. Critical CPU/RAM banners retain the last
alarm for five seconds unless the matching Python option disables the hold.

## Tests and visual verification

Storage tests now cover window, alarm, tray and dock settings plus malformed
input normalization. The UI contract opens the settings dialog, validates all
four sections and verifies hot Quantum Cyan application, free resize, card
visibility and JSON persistence.

The live Windows GUI was inspected at the default 830x900 constraint. The main
Overview and every settings section remained readable, disabled optional-module
controls were visually distinct, and Quantum Cyan updated the window, dialog
and graphs immediately. Eclipse was restored after the check. CPU/GPU stress
was not run.
