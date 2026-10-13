#!/bin/sh
# Stop the VectorPie menu (only one program can hold the vector board and the
# screen; it does not leave on a plain kill), run the game on the board, then
# bring the menu back. Arguments go to the game.
sudo -n systemctl stop getty@tty1
while pgrep -x vector_pie_menu > /dev/null; do sleep 1; done
sleep 1     # the menu is gone a moment before the screen is free
cd "$(dirname "$0")" || exit 1
chmod +x swenhanced
VP_OUTPUT=dvg VP_STATS=1 ./swenhanced "$@" > run.log 2>&1
sudo -n systemctl start getty@tty1
