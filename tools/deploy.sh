#!/bin/sh
# deploy.sh - copy the Pi build to the cabinet, put the new program in place
# and start it there, so that what is on the screen is the new build. A
# game that is running is ended first. When the game is left, the cabinet's
# menu comes back (package/run.sh). Run from the project's folder, in Git
# Bash, after `cmake --build --preset pi-clang`. Arguments go to the game.
K="-i $HOME/.ssh/id_ed25519_vectorpie -o ConnectTimeout=8 -o BatchMode=yes"
H=pi@192.168.0.122
D=/persistent/games/swenhanced
ssh $K $H "mkdir -p $D/save" || exit 1
scp -q $K build-pi/swenhanced $H:$D/swenhanced.new || exit 1
scp -q $K build-pi/avgreplay package/game.ini package/swenhanced.ini package/marquee.png package/run.sh $H:$D/ || exit 1
ssh $K $H "rm -rf $D/sounds" && scp -q -r $K package/sounds $H:$D/sounds || exit 1      # (the recordings)
# The old run.sh goes first: left alive, it would bring the menu back (getty)
# as soon as its game dies, while the new game is starting - both then hold
# the screen and the board, the game stalls (seen 2026-10-17). The new run.sh
# stops the menu itself; we wait for the game up to 20 s (the menu takes a
# while to leave).
ssh $K $H "cd $D && pkill -f 'sh \./run\.sh'; pkill -INT -x swenhanced; for i in 1 2 3 4 5 6; do pgrep -x swenhanced > /dev/null || break; sleep 0.5; done; pkill -KILL -x swenhanced; chmod 755 swenhanced.new avgreplay run.sh && mv -f swenhanced.new swenhanced && md5sum swenhanced && grep '^version' game.ini && (setsid nohup ./run.sh $* > /dev/null 2>&1 &) ; for i in \$(seq 1 40); do sleep 0.5; pgrep -x swenhanced > /dev/null && break; done; sleep 3; pgrep -x swenhanced > /dev/null && echo game-running || echo GAME-NOT-RUNNING; pgrep -x vector_pie_menu > /dev/null && echo MENU-STILL-RUNNING; tail -n 3 run.log"
md5sum build-pi/swenhanced
