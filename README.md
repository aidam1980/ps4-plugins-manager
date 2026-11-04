# PS4 Plugins Manager

**PS4 Plugins Manager** is a homebrew utility for managing plugins on
your PlayStation 4. It lets you browse installed games and applications,
enable plugins for one or more games, manage a default plugin, and edit
or back up `plugins.ini` directly from the console.

## Features

-   Browse applications installed on the PS4.
-   Select plugin files from the `plugins` folder and assign them to
    games.
-   View the plugins currently enabled for a selected game.
-   Assign one plugin to multiple games.
-   Set a plugin as the default so it loads with all applications.
-   Switch between a standard list and an image-based plugin view.
-   Edit `plugins.ini`, copy and paste lines, delete entries, and create
    or restore backups.

## User Guide

### 1. Games List 🎮

This is the main screen. It displays the applications installed on your
console as PKG-installed titles.

  -----------------------------------------------------------------------
  Control                             Action
  ----------------------------------- -----------------------------------
  D-Pad / Arrow Buttons / Analog      Navigate through the games list
  Stick                               

  ✕ Cross                             Open the plugin file list from the
                                      `plugins` folder. You can select
                                      multiple plugin files.

  △ Triangle                          Show the plugins currently enabled
                                      for the selected game.
  -----------------------------------------------------------------------

### 2. My Plugins 🔌

This screen displays the available plugins. Use **L1 / R1** to switch
between menus.

  -----------------------------------------------------------------------
  Control                             Action
  ----------------------------------- -----------------------------------
  D-Pad / Arrow Buttons / Analog      Navigate through the plugin list
  Stick                               

  ✕ Cross                             Open the games list and select one
                                      or more games to use the selected
                                      plugin

  OPTIONS                             Set the selected plugin as the
                                      default plugin. A default plugin
                                      loads with all applications.

  R3                                  Switch between list view and image
                                      view

  □ Square                            Reload the plugin list
  -----------------------------------------------------------------------

#### Plugin images

In image view, the application looks for a PNG image with the same
filename as the plugin. For example:

``` text
orbisnet.sprx
orbisnet.png
```

Place the image alongside the plugin files in the appropriate plugins
directory. If a matching image is not available, the application may
display its default image.

### 3. plugins.ini 📝

This screen lets you edit `plugins.ini` manually and manage backup
copies of the file.

  Control                                Action
  -------------------------------------- -------------------------------------------------------
  D-Pad / Arrow Buttons / Analog Stick   Navigate through the entries
  ✕ Cross                                Open the on-screen keyboard to edit the selected line
  □ Square                               Delete the selected line
  L2                                     Copy the selected line
  R2                                     Paste the copied line below the selected line
  △ Triangle                             Create a backup of `plugins.ini`
  OPTIONS                                Open the list of existing backups

> **Important:** Backups are not created automatically. Create a backup
> manually by pressing **△ Triangle** before making changes to
> `plugins.ini`.

## Notes

-   Plugin files must be placed in the console's `plugins` folder to
    appear in the plugin selection list.
-   Be careful when editing `plugins.ini`. Incorrect entries may prevent
    plugins from loading as expected.
-   Creating a backup before editing the file is strongly recommended.
