-- Lays out the Pixelview Desktop install window on a mounted read-write DMG:
-- the app and the Applications shortcut side by side over the background's
-- chevron, the Licenses folder on the lower band. Finder records the layout in
-- the volume's .DS_Store. Geometry matches pixelview-dmg-background.swift.
--
--   osascript pixelview-dmg-layout.applescript "<volume name>"
on run argv
    set volumeName to item 1 of argv
    set windowLeft to 200
    set windowTop to 120
    set titleBarHeight to 32
    tell application "Finder"
        tell disk volumeName
            open
            tell container window
                set current view to icon view
                set toolbar visible to false
                set statusbar visible to false
                set pathbar visible to false
                set sidebar width to 0
                -- Bounds include the title bar; the content area must stay 640x480
                -- to match the background.
                set the bounds to {windowLeft, windowTop, windowLeft + 640, windowTop + 480 + titleBarHeight}
            end tell
            set opts to the icon view options of container window
            tell opts
                set icon size to 120
                set text size to 13
                set arrangement to not arranged
                set shows item info to false
                set shows icon preview to false
            end tell
            set background picture of opts to file ".background:background.tiff"
            set position of item "Pixelview Desktop.app" of container window to {170, 190}
            set position of item "Applications" of container window to {470, 190}
            set position of item "Licenses" of container window to {320, 365}
            update without registering applications
            -- Reopen so Finder commits the window size and positions.
            close
            open
            delay 1
            close
        end tell
    end tell
end run
