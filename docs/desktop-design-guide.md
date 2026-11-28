# Workspace design guide

LG Input Switch serves two different moments. At the desk, someone usually wants to switch a screen or adjust brightness immediately. During setup, they need to see exactly which physical monitor, input and shortcut each action will use. The workspace keeps that context visible while they edit it.

## Visual identity

Use the project's monitor glyph for LG Input Switch. It identifies an independent utility. UniFi informs the alignment and restrained use of color. BetterDisplay informs the choice to put immediate monitor controls in the tray. Vendor logos and product illustrations have no role in navigation.

| Source | Use |
| --- | --- |
| [Workspace, palette and runtime monitor icon](../desktop/main.cpp) | Qt Widgets draws the shared Windows and Linux interface. The monitor icon has variants from 16 to 128 pixels. |
| [Workspace editing icons](../desktop/workspace_icons.h) | The pencil and trash can follow the platform text color and scale to the requested size. |
| [Inline input editor](../desktop/monitor_input_editor.cpp) | Monitor names, visible ports and advanced DDC controls belong to the selected monitor. |
| [Linux launcher icon](../desktop/lginputswitch.svg) and [Windows icon](../resource/app.ico) | The Linux package installs the scalable image. The [Windows resource](../resource/app.rc) embeds the ICO. The project maintains the platform files separately. Check both at tray size after an icon change. |

## Navigation and layout

Use the sidebar for Input Profiles, Monitors and Settings. Tabs inside the first two pages select a particular profile or monitor. The trailing **+** adds another. Keep the tab strip, section headings and brightness control in place when entering an editor. Each page scrolls within the window. Status and the workspace footer sit below that viewport.

| Surface | Reading order | Primary interaction |
| --- | --- | --- |
| Input Profiles | Profile tabs, assigned monitors, actions | **Switch** on an action sends its per-monitor mappings. Show the destinations and shortcut on the same row. |
| Monitors | Monitor tabs, connection, brightness, inputs, advanced settings | Adjust brightness or switch one monitor. Detect inputs automatically. Keep connection changes and source addresses under **Advanced**. Show input codes when customizing the list. |
| Tray | Monitor name, brightness, direct inputs, then configured group actions | One click opens the quick panel. Double-click raises the workspace. The context menu includes Quit. |
| Settings | Preferences grouped under headings, updates, diagnostics | Keep occasional maintenance tasks out of the switching lists. |

## Editing

Place a pencil immediately beside the section it edits. Give icon buttons a descriptive accessible name and tooltip. Use an explicit verb on buttons that send a command to hardware. The last command sent can be shown as history. A claim about the current physical input requires a monitor reading.

Keep Actions in one table with the same column order in both modes. Names, monitor mappings and shortcuts turn into editable fields in their own rows. The page handles vertical scrolling, and the assignments list uses the workspace surface color. In Actions, the left grip moves the complete action. Its ID, per-monitor mappings, enabled state and shortcut travel together. Alt+Up and Alt+Down provide a keyboard alternative. The trash button on the right removes that row from the draft. The last action is retained because a profile requires one. Place **Cancel** next to **Save actions**. Cancel restores the section's starting state, including row order.

Use the same interaction for assignments and monitor inputs. Label commits **Save monitors** and **Save inputs**. For inputs, checkboxes control visibility in the direct switchers. Keep saved profile mappings independent of that visibility. Adding or removing a port selects manual mode. Confirm removal of a mapped port and disable its affected actions only when saved. Unrelated drafts should leave saved monitor commands available. Brightness takes effect on slider release or a keyboard adjustment and uses monitor readback.

## Color, type and spacing

- Use Qt palette roles for surfaces, text, dividers, focus and the system accent option. The default accent is the monitor icon's blue, `#3268E8`. Pair it with readable highlighted text for **Switch** and section save buttons.
- Use flat fills for selected tabs and navigation items. Retain a separate keyboard focus outline. Keep page backgrounds and separators neutral in both themes.
- Use the platform UI sans family, a 28 px bold page title, 14 px semibold section labels and 12 px secondary text. Use the system monospace face for shortcuts and brightness values.
- Align labels to the leading edge and repeated actions to the trailing edge. Use 12–16 logical pixels between sections and tighter spacing within a row. Separate major sections with a one-pixel rule. Keep icon-only editing controls at least 30 logical pixels high.

## Reviewing a change

Render the real Qt widgets in light and dark themes with `LGInputSwitchDesktop --preview --screenshot-dir previews`. Compare profile and monitor viewing and editing states. At 640 × 480 and 800 × 600, check that controls scroll into reach and that the footer never covers the last row. Exercise keyboard focus, reorder, delete, Cancel, save and reload. Exercise tray single-click and double-click on the target desktop. Preview data and hardware-free tests cover UI behavior. A hardware claim requires a separate device check.

## States, access and privacy

Keep the last confirmed brightness reading visible during normal use. If a read fails, show the reason and offer a refresh. Identify a disconnected monitor in text. On a partial group switch, keep each monitor's result visible and offer **Retry failed monitors**. If settings validation fails, keep the editor open so the user can correct the value. Give icon and shortcut controls accessible names. Keep focused controls within the scrollable page.

The accessibility target is WCAG 2.2 AA as applicable to native widgets. Review normal text at 4.5:1 contrast and meaningful controls at 3:1. Test keyboard navigation, large text and both palettes. A passing smoke test covers specific interactions only.

Profile editing runs locally. Users choose when to send a display command or export a diagnostic report. Review and redact a report before sharing it. See the [desktop behavior guide](desktop-ui.md), [shared core](shared-core.md) and [update trust model](updates.md).

## Reference captures

These are real Qt offscreen renders with synthetic monitor names and mappings. The local CachyOS build uses Qt 6.12.1 and the 2.0 working tree based on `20c6264`. The standard viewport is 1000 × 740 logical pixels at scale 1. The compact editing view is 640 × 480. Use the preview command above to regenerate the set after an interaction or visual identity change.

| Screen | Dark | Light |
| --- | --- | --- |
| Input Profiles | [View](design-captures/profiles-dark.png) · [Edit](design-captures/profiles-dark-editing.png) | [View](design-captures/profiles-light.png) · [Edit](design-captures/profiles-light-editing.png) |
| Monitors | [View](design-captures/desktop-dark-monitors.png) · [Edit](design-captures/monitors-dark-editing.png) | [View](design-captures/desktop-light-monitors.png) · [Edit](design-captures/monitors-light-editing.png) |
| Compact action editor | [Dark](design-captures/profiles-dark-compact.png) | [Light](design-captures/profiles-light-compact.png) |
| Tray quick panel | [Dark](design-captures/tray-dark.png) | [Light](design-captures/tray-light.png) |

Native Windows rendering, Explorer click delivery, screen-reader announcements and 200% text scaling still need review on the target desktop.
