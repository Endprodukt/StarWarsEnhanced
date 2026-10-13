#!/bin/sh
# deploy.sh - copy the Pi build to the cabinet, put the new program in place
# and start it there, so that what is on the screen is the new build. A
# game that is running is ended first. When the game is left, the cabinet's
# menu comes back (package/run.sh). Run from the project's folder, in Git
# Bash, after `cmake --build --preset pi-clang`. Arguments go to the game.
K="-i $HOME/.ssh/id_ed25519_vectorpie -o ConnectTimeout=8 -o BatchMode=yes"
H=pi@192.168.0.122
D=/persistent/games/starwarsvp
ssh $K $H "mkdir -p $D/save" || exit 1
scp -q $K build-pi/starwarsvp $H:$D/starwarsvp.new || exit 1
scp -q $K build-pi/avgreplay package/game.ini package/starwarsvp.ini package/run.sh $H:$D/ || exit 1
ssh $K $H "cd $D && pkill -INT -x starwarsvp; for i in 1 2 3 4 5 6; do pgrep -x starwarsvp > /dev/null || break; sleep 0.5; done; pkill -KILL -x starwarsvp; chmod 755 starwarsvp.new avgreplay run.sh && mv -f starwarsvp.new starwarsvp && md5sum starwarsvp && grep '^version' game.ini && (setsid nohup ./run.sh $* > /dev/null 2>&1 &) ; sleep 6; pgrep -x starwarsvp > /dev/null && echo game-running || echo GAME-NOT-RUNNING; tail -n 5 run.log"
md5sum build-pi/starwarsvp
