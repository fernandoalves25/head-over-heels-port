#!/bin/bash
# Head Over Heels (remake Retrospec 2003) - PORT NATIVO aarch64 (SDL2), sem Box64/Wine/Weston.
# Mesma estrutura dos ports do PortMaster (control.txt, gptokeyb para o controle).

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

GAMEDIR="/$directory/ports/hohnative"
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

$ESUDO chmod 666 /dev/tty0 2>/dev/null
say(){ echo "$1"; printf "%s\n" "$1" > /dev/tty0 2>/dev/null; printf "%s\n" "$1" > /dev/tty1 2>/dev/null; }

if [ ! -f "$GAMEDIR/gamedata/HoHOriginal.dat" ] || [ ! -f "$GAMEDIR/gamedata/Sound/Sound.dat" ]; then
  printf "\033c" > /dev/tty1 2>/dev/null
  say ""
  say "=== ARQUIVOS DO JOGO NAO ENCONTRADOS ==="
  say "Copie da pasta do jogo original (remake Retrospec 2003) para:"
  say "  roms/ports/hohnative/gamedata/"
  say "  HoHOriginal.dat  e a pasta  Sound/  (Sound.dat, Theme.ogg, Game.ogg)"
  sleep 15; pm_finish; exit 1
fi

chmod +x "$GAMEDIR/hoh" 2>/dev/null
echo "hoh: $(file -b "$GAMEDIR/hoh" 2>/dev/null)"
echo "SDL: $(ls /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0* 2>/dev/null | head -1)"

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
$GPTOKEYB "hoh" -c "$GAMEDIR/hoh.gptk" &
./hoh --data "$GAMEDIR/gamedata"
echo "hoh saiu com codigo $?"

$ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null
pm_finish
printf "\033c" > /dev/tty1 2>/dev/null
