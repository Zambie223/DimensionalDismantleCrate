#!/usr/bin/env bash
# Dedicated server test for Dimensional Dismantle Crate (DEV package: DDC_DEV_COMMANDS=1).
# The server loads a test save ($DDC_TEST_SAVE, default DDCTest); the mod's auto-test runs ON THE SERVER via the
# HTTPS API (DDC.Dev.RunTests). The save needs the Dimensional Depot research and at least one uploader built.
# Optionally a GameCopy client joins first, so test crates spawn next to a remote player and the client logs its
# replicated view every 5 s (-DDCClientWatch). Whatever is in GameCopy\Mods decides the client kind:
#   with this mod installed  -> "modded client"  (client view logged)
#   without it               -> "vanilla client" (checks that RequiredOnRemote:false lets it join)
#
# Usage: ddc_ds_test.sh windows|linux [client]   (use "client": a server with no player auto-pauses)
# Mods on server and client must match, except this one (RequiredOnRemote: false).
set -u
TARGET="${1:-windows}"
WITH_CLIENT="${2:-}"
DS=/m/Satismod/tools/ds.sh
SAVES="$(cygpath "$LOCALAPPDATA")/FactoryGame/Saved/SaveGames"
CLIENT_SAVES=$(ls -d "$SAVES"/[0-9]* | head -1)   # the Steam save folder (named after the account id)
TEST_SAVE="${DDC_TEST_SAVE:-DDCTest}"
TEST_SESSION="${DDC_TEST_SESSION:-DDCTest}"        # session name stored inside the test save (its autosaves use it)
CLIENT_LOG=/m/Satismod/GameCopyUser/Saved/Logs/FactoryGame.log
wsl_run() { MSYS_NO_PATHCONV=1 wsl.exe -d EasyDDLinux -u ubuntu -- bash -c "$1" 2>&1 | tr -d '\0'; }
cmd() { echo "> $*"; bash "$DS" cmd "$*"; }

if [ "$TARGET" = linux ]; then
  export DS_HOST=$(MSYS_NO_PATHCONV=1 wsl.exe -d EasyDDLinux -- hostname -I | tr -d '\0\r' | awk '{print $1}') DS_PORT=7787 DS_STATE_DIR=/m/Satismod/DedicatedServerLinuxUser
  # $HOME, not ~: the cleanup quotes the path, and a quoted ~ is not expanded.
  LINUX_SAVES='$HOME/.config/Epic/FactoryGame/Saved/SaveGames/server'
  # The Linux server logs into its install folder, not ~/.config (check with /proc/<pid>/fd if unsure).
  SERVER_LOG=/m/Satismod/DedicatedServerLinux/FactoryGame/Saved/Logs/FactoryGame.log
  CONNECT="$DS_HOST:7787"
  echo "== Linux server save folder before:"; wsl_run "ls -1 $LINUX_SAVES" | tee /tmp/ddc_ds_before.txt
  wsl_run "cp '/mnt/c${CLIENT_SAVES#/c}/$TEST_SAVE.sav' $LINUX_SAVES/$TEST_SAVE.sav"
  wsl_run 'bash /mnt/m/Satismod/tools/linux/start-linux-server.sh' | tail -2
else
  SERVER_SAVES="$SAVES/server"
  SERVER_LOG=/m/Satismod/DedicatedServerUser/Saved/Logs/FactoryGame.log
  CONNECT="127.0.0.1:7777"
  echo "== server save folder before:"; ls -1 "$SERVER_SAVES" | tee /tmp/ddc_ds_before.txt
  cp "$CLIENT_SAVES/$TEST_SAVE.sav" "$SERVER_SAVES/$TEST_SAVE.sav"
  bash "$DS" start
fi
# Dedicated servers auto-pause ~30 s after load while no player is connected, which freezes a server-only test.
# Run with a client ("client") so the world keeps ticking.
server_log() { cat "$SERVER_LOG" 2>/dev/null; }

bash "$DS" wait-api >/dev/null || { echo "API did not come up"; exit 1; }
if ! bash "$DS" QueryServerState | grep -q serverGameState; then echo "stored token rejected; claiming"; bash "$DS" claim; fi
bash "$DS" LoadGame "{\"SaveName\":\"$TEST_SAVE\",\"EnableAdvancedGameSettings\":false}"; echo
bash "$DS" wait-game >/dev/null || { echo "game did not start"; exit 1; }
cmd "net.AllowEncryption 1"
cmd "DDC.Status"

CLIENT_PID=""
if [ "$WITH_CLIENT" = client ]; then
  echo "== launching the GameCopy client to $CONNECT (mods: $(ls /m/Satismod/GameCopy/FactoryGame/Mods | tr '\n' ' '))"
  CLIENT_PID=$(powershell.exe -NoProfile -Command "\$p = Start-Process 'M:\\Satismod\\GameCopy\\FactoryGameSteam.exe' -ArgumentList '-userdir=M:/Satismod/GameCopyUser','-windowed','-ResX=1280','-ResY=720','-DDCClientWatch','-ExecCmds=\"open $CONNECT\"' -PassThru; \$p.Id" | tr -d '\r')
  echo "client launcher pid $CLIENT_PID"
  for i in $(seq 1 240); do
    if bash "$DS" QueryServerState | grep -q '"numConnectedPlayers":[1-9]'; then echo "player connected after ~$((i*5)) s"; break; fi
    if [ "$i" -gt 6 ] && grep -aq "PendingConnectionFailure\|NMT_Failure" "$CLIENT_LOG" 2>/dev/null; then
      echo "!! client was refused:"; grep -a "NMT_Failure\|PendingConnectionFailure" "$CLIENT_LOG" | head -2 | cut -c1-260; break
    fi
    sleep 5
  done
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$(dirname "$0")")\\move_offscreen.ps1" >/dev/null 2>&1
  sleep 40   # let the remote pawn spawn
fi

echo "== auto-test on the server"
cmd "DDC.Dev.RunTests"
for i in $(seq 1 120); do
  if server_log | grep -aq '\[DDCTest\] DONE'; then break; fi
  sleep 5
done
server_log | grep -a '\[DDCTest\]\|Gate:\|Uploaded since' | sed 's/^\[[^]]*\]\[[^]]*\]//' | tail -60
cmd "DDC.Dev.Crates"
cmd "DDC.Status"

if [ -n "$CLIENT_PID" ]; then
  sleep 12
  echo "== client view (last lines):"
  grep -a '\[DDCClient\]\|NMT_Failure\|LogNet: .*Welcomed\|Join succeeded' "$CLIENT_LOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | tail -12 | cut -c1-400
fi

echo "== stopping"
bash "$DS" stop
[ -n "$CLIENT_PID" ] && powershell.exe -NoProfile -Command "Get-Process FactoryGameSteam*, CrashReportClient* -ErrorAction SilentlyContinue | Stop-Process -Force"
sleep 20

echo "== cleaning up the server save folder (only files this test added)"
if [ "$TARGET" = linux ]; then
  wsl_run "ls -1 $LINUX_SAVES" > /tmp/ddc_ds_after.txt
  comm -13 <(sort /tmp/ddc_ds_before.txt) <(sort /tmp/ddc_ds_after.txt) | while read -r f; do
    case "$f" in "$TEST_SAVE.sav"|"$TEST_SESSION"*) wsl_run "rm -f \"$LINUX_SAVES/$f\""; echo "removed $f";; *) echo "NEW FILE left in place: $f";; esac
  done
else
  ls -1 "$SERVER_SAVES" > /tmp/ddc_ds_after.txt
  comm -13 <(sort /tmp/ddc_ds_before.txt) <(sort /tmp/ddc_ds_after.txt) | while read -r f; do
    case "$f" in "$TEST_SAVE.sav"|"$TEST_SESSION"*) rm -f "$SERVER_SAVES/$f"; echo "removed $f";; *) echo "NEW FILE left in place: $f";; esac
  done
fi
echo "== done"
