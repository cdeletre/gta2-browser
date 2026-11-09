#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/gta2/
CONFDIR="$GAMEDIR/conf/"

mkdir -p "$GAMEDIR/conf"
mkdir -p "$GAMEDIR/saves"

cd $GAMEDIR

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

if [ ! -d "$GAMEDIR/gamedata/data" ]; then
    [ ! -f "$GAMEDIR/gamedata/GTA2INSTALLER.exe" ] && echo "GTA2INSTALLER.exe not found" && exit 1

    export PATH="$GAMEDIR/tools:$PATH"

    cd "$GAMEDIR/gamedata"

    $ESUDO chmod u+x tools/iss_extract tools/unshield

    $ESUDO iss_extract x "GTA2INSTALLER.exe" data1.cab data1.hdr data2.cab
    [ $? -ne 0 ] && echo "Error extracting GTA2INSTALLER.exe" && exit 1

    $ESUDO rm -f GTA2INSTALLER.exe

    LD_LIBRARY_PATH="$GAMEDIR/tools:$LD_LIBRARY_PATH" unshield x data1.cab
    [ $? -ne 0 ] && echo "Error extracting CAB files" && exit 1

    $ESUDO rm -f data1.cab data1.hdr data2.cab
    $ESUDO rm -rf _*
    $ESUDO mv App_Executables/data .
    $ESUDO rm -rf App_Executables

    cd "$GAMEDIR"
fi

$GPTOKEYB "gta2.${DEVICE_ARCH}" &

pm_platform_helper "$GAMEDIR/gta2.${DEVICE_ARCH}"
./gta2.${DEVICE_ARCH}

pm_finish