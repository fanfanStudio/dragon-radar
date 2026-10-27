#!/bin/sh
DIR=$(dirname "$0")
cd "$DIR"
chmod +x ./dragonradar
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
./dragonradar
