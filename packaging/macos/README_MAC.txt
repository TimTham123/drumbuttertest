DrumSpread for macOS (universal: Apple Silicon + Intel, macOS 10.15 or newer)
=============================================================================

What's in this folder
  DrumSpread.app         Standalone app. Runs from anywhere (Desktop, USB stick...).
  DrumSpread.vst3        VST3 plugin (Ableton Live, Reaper, Cubase, Studio One, Bitwig, FL Studio).
  DrumSpread.component   Audio Unit plugin (Logic Pro, GarageBand, also Live/Reaper).
  install.command        Double-click to install the plugins for your user account.

These builds are not notarized by Apple, so macOS blocks them the first time.

Easiest install
  1. Double-click install.command. If macOS refuses to open it, right-click it,
     choose Open, then click Open in the dialog.
  2. It copies the plugins to ~/Library/Audio/Plug-Ins/VST3 and .../Components,
     removes the download "quarantine" flag, and refreshes the Audio Unit cache.
  3. Restart your DAW and rescan plugins. DrumSpread is listed under "DrumSpread Audio".

Manual install
  1. Copy DrumSpread.vst3 to      ~/Library/Audio/Plug-Ins/VST3/
     Copy DrumSpread.component to ~/Library/Audio/Plug-Ins/Components/
     (In Finder: Go > Go to Folder..., paste the path.)
  2. In Terminal, clear the quarantine flag:
       xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/DrumSpread.vst3
       xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/DrumSpread.component

Standalone app (portable, no install)
  Right-click DrumSpread.app > Open > Open the first time. If macOS still says it
  is "damaged" or can't be opened, run:
       xattr -dr com.apple.quarantine /path/to/DrumSpread.app
  Choose your audio interface under Options > Audio/MIDI Settings. Allow
  microphone access when asked, otherwise there is no input signal.

Uninstall
  Delete the two plugin files from the Plug-Ins folders above. User presets live in
  ~/Library/DrumSpread/Presets.
