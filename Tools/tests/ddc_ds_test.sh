#!/usr/bin/env bash
# Dedicated server test for Dimensional Dismantle Crate (DEV package: DDC_DEV_COMMANDS=1).
# The server loads a test save ($DDC_TEST_SAVE, default DDCTest); the mod's auto-test runs ON THE SERVER via the
# HTTPS API (DDC.Dev.RunTests). The save needs the Dimensional Depot research and at least one uploader built.
# Optionally a GameCopy client joins first, so test crates spawn next to a remote player and the client logs its
# replicated view every 5 s (-DDCClientWatch). Whatever is in the client copy's Mods decides the client kind:
#   with this mod installed  -> "modded client"  (client view logged)
#   without it               -> "vanilla client" (checks that RequiredOnRemote:false lets it join)
#
# Usage: ddc_ds_test.sh windows|linux [client]   (use "client": a server with no player auto-pauses)
# DDC_SMOKE=1: for a RELEASE package (no dev commands): runs DDC.Status, waits DDC_SMOKE_SECONDS (default 90), runs it
# again and prints the mod's log lines, instead of running the auto-test.
# Mods on server and client must match, except this one (RequiredOnRemote: false).
# Targets the game 1.2.5.1 copies: Windows server $DS_DIR (default PatchCheck/DS-new), Linux server
# PatchCheck/DS-new-linux in the EasyDDLinux WSL distro, client $GAMECOPY_DIR (default GameCopy-1.2.5.1) with
# -userdir $CLIENT_USERDIR. The Windows server writes its saves to the real SaveGames/server folder whatever -userdir
# says, so the cleanup below only removes files this test added.
set -u
TARGET="${1:-windows}"
WITH_CLIENT="${2:-}"
SMOKE="${DDC_SMOKE:-0}"
SMOKE_SECONDS="${DDC_SMOKE_SECONDS:-90}"
DS=/m/Satismod/tools/ds.sh
export DS_DIR="${DS_DIR:-M:/Satismod/PatchCheck/DS-new}"
GAMECOPY_DIR="${GAMECOPY_DIR:-M:/Satismod/GameCopy-1.2.5.1}"
CLIENT_USERDIR="${CLIENT_USERDIR:-M:/Satismod/GameCopy1251User}"
LINUX_DS_DIR=/mnt/m/Satismod/PatchCheck/DS-new-linux
SAVES="$(cygpath "$LOCALAPPDATA")/FactoryGame/Saved/SaveGames"
CLIENT_SAVES=$(ls -d "$SAVES"/[0-9]* | head -1)   # the Steam save folder (named after the account id)
TEST_SAVE="${DDC_TEST_SAVE:-DDCTest}"
TEST_SESSION="${DDC_TEST_SESSION:-DDCTest}"        # session name stored inside the test save (its autosaves use it)
CLIENT_LOG="$(cygpath "$CLIENT_USERDIR")/Saved/Logs/FactoryGame.log"
GAMECOPY_WIN="$(cygpath -w "$GAMECOPY_DIR")"
HERE_WIN="$(cygpath -w "$(dirname "$0")")"
# wsl.exe reads stdin; </dev/null keeps it from swallowing the input of a surrounding while-read loop.
wsl_run() { MSYS_NO_PATHCONV=1 wsl.exe -d EasyDDLinux -u ubuntu -- bash -c "$1" < /dev/null 2>&1 | tr -d '\0'; }
cmd() { echo "> $*"; bash "$DS" cmd "$*"; }
# Only processes started from the test copy are ours; the user's own game may be running from the real install.
kill_client() {
  [ -n "${SHOT_PID:-}" ] && kill "$SHOT_PID" 2>/dev/null
  powershell.exe -NoProfile -Command "Get-Process FactoryGameSteam*, CrashReportClient*, crashpad_handler -ErrorAction SilentlyContinue | Where-Object { \$_.Path -like '$GAMECOPY_WIN*' } | Stop-Process -Force"
}
# Call repeatedly: Unreal's splash window is replaced by the real game window, which opens on-screen again.
offscreen() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE_WIN/move_offscreen.ps1" -GameDir "$GAMECOPY_WIN" >/dev/null 2>&1; }

if [ "$TARGET" = linux ]; then
  if wsl_run "pgrep -f 'FactoryServer-Linu[x]'" | grep -q .; then echo "!! a Linux server is already running in EasyDDLinux; stop it first"; exit 1; fi
  export DS_HOST=$(MSYS_NO_PATHCONV=1 wsl.exe -d EasyDDLinux -- hostname -I < /dev/null | tr -d '\0\r' | awk '{print $1}') DS_PORT=7787 DS_STATE_DIR=/m/Satismod/DedicatedServerLinuxUser
  # $HOME, not ~: the cleanup quotes the path, and a quoted ~ is not expanded.
  LINUX_SAVES='$HOME/.config/Epic/FactoryGame/Saved/SaveGames/server'
  CONNECT="$DS_HOST:7787"
  echo "== Linux server save folder before:"; wsl_run "ls -1 $LINUX_SAVES" | tee /tmp/ddc_ds_before.txt
  wsl_run "cp '/mnt/c${CLIENT_SAVES#/c}/$TEST_SAVE.sav' $LINUX_SAVES/$TEST_SAVE.sav"
  wsl_run "cd $LINUX_DS_DIR && (setsid nohup ./FactoryServer.sh -log -unattended -Port=7787 -multihome=0.0.0.0 > \$HOME/server-stdout.txt 2>&1 < /dev/null &); sleep 8; pgrep -af 'FactoryServer-Linu[x]' | head -1"
  # The Linux server logs into its install folder or ~/.config depending on the build; read whichever is newer.
  # --exec, not --: "wsl.exe -- bash -c" runs the line through the login shell first, which expands $f (empty)
  # before bash sees it, so the old version printed nothing.
  server_log() { MSYS_NO_PATHCONV=1 wsl.exe -d EasyDDLinux -u ubuntu --exec bash -c "f=\$(ls -t $LINUX_DS_DIR/FactoryGame/Saved/Logs/FactoryGame.log \$HOME/.config/Epic/FactoryGame/Saved/Logs/FactoryGame.log 2>/dev/null | head -1); cat \"\$f\"" < /dev/null 2>&1 | tr -d '\0'; }
else
  if powershell.exe -NoProfile -Command "Get-Process FactoryServer* -ErrorAction SilentlyContinue" | grep -q FactoryServer; then echo "!! a Windows dedicated server is already running; stop it first"; exit 1; fi
  SERVER_SAVES="$SAVES/server"
  SERVER_LOG=/m/Satismod/DedicatedServerUser/Saved/Logs/FactoryGame.log
  CONNECT="127.0.0.1:7777"
  echo "== server save folder before:"; ls -1 "$SERVER_SAVES" | tee /tmp/ddc_ds_before.txt
  cp "$CLIENT_SAVES/$TEST_SAVE.sav" "$SERVER_SAVES/$TEST_SAVE.sav"
  bash "$DS" start
  server_log() { cat "$SERVER_LOG" 2>/dev/null; }
fi
# Dedicated servers auto-pause ~30 s after load while no player is connected, which freezes a server-only test.
# Run with a client ("client") so the world keeps ticking.

bash "$DS" wait-api >/dev/null || { echo "API did not come up"; exit 1; }
if ! bash "$DS" QueryServerState | grep -q serverGameState; then echo "stored token rejected; claiming"; bash "$DS" claim; fi
bash "$DS" LoadGame "{\"SaveName\":\"$TEST_SAVE\",\"EnableAdvancedGameSettings\":false}"; echo
bash "$DS" wait-game >/dev/null || { echo "game did not start"; exit 1; }
cmd "net.AllowEncryption 1"
cmd "DDC.Status"

CLIENT_PID=""
if [ "$WITH_CLIENT" = client ]; then
  echo "== launching the GameCopy client to $CONNECT (mods: $(ls "$(cygpath "$GAMECOPY_DIR")/FactoryGame/Mods" | tr '\n' ' '))"
  CLIENT_PID=$(powershell.exe -NoProfile -Command "\$p = Start-Process '$GAMECOPY_WIN/FactoryGameSteam.exe' -ArgumentList '-userdir=$CLIENT_USERDIR','-windowed','-ResX=1280','-ResY=720','-DDCClientWatch','-ExecCmds=\"open $CONNECT\"' -PassThru; \$p.Id" | tr -d '\r')
  echo "client launcher pid $CLIENT_PID"
  for i in $(seq 1 240); do
    offscreen
    if bash "$DS" QueryServerState | grep -q '"numConnectedPlayers":[1-9]'; then echo "player connected after ~$((i*5)) s"; break; fi
    if [ "$i" -gt 6 ] && grep -aq "PendingConnectionFailure\|NMT_Failure" "$CLIENT_LOG" 2>/dev/null; then
      echo "!! client was refused:"; grep -a "NMT_Failure\|PendingConnectionFailure" "$CLIENT_LOG" | head -2 | cut -c1-260; break
    fi
    sleep 5
  done
  for k in 1 2 3 4; do offscreen; sleep 10; done   # let the remote pawn spawn

  # Chat notices on the player's screen: after each of the first four "Notice:" lines in the server log, the client
  # window is captured 2 s and 6 s later (works with or without the mod on the client). Saved to $SHOT_DIR.
  SHOT_DIR="${DDC_SHOT_DIR:-/m/Satismod/BuildTemp/ddc-shots}"
  mkdir -p "$SHOT_DIR"
  SHOT_TAG="$(date +%Y%m%d-%H%M%S)-$TARGET"
  SHOT_PREFIX="$(cygpath -w "$SHOT_DIR")\\$SHOT_TAG"
  (
    seen=0
    for i in $(seq 1 900); do
      kill -0 $$ 2>/dev/null || break   # the test script is gone
      n=$(server_log | grep -ac 'LogDimensionalDismantleCrate: Notice:')
      if [ "$n" -gt "$seen" ] && [ "$seen" -lt 4 ]; then
        seen=$n
        sleep 2; powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE_WIN/capture_client.ps1" -Out "$SHOT_PREFIX-notice$n-a.png" -GameDir "$GAMECOPY_WIN" | tr -d '\r'
        sleep 4; powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE_WIN/capture_client.ps1" -Out "$SHOT_PREFIX-notice$n-b.png" -GameDir "$GAMECOPY_WIN" | tr -d '\r'
      fi
      sleep 1
    done
  ) &
  SHOT_PID=$!
fi

if [ "$SMOKE" = 1 ]; then
  echo "== release smoke: status now and after $SMOKE_SECONDS s"
  for k in $(seq 1 $((SMOKE_SECONDS / 10))); do offscreen; sleep 10; done
  cmd "DDC.Status"
  server_log | grep -a 'LogDimensionalDismantleCrate\|LogSatisfactoryModLoader: .*DimensionalDismantleCrate\|Critical error\|Fatal error\|Unhandled Exception' | sed 's/^\[[^]]*\]\[[^]]*\]//' | tail -60
else
  echo "== auto-test on the server"
  cmd "DDC.Dev.RunTests"
  for i in $(seq 1 180); do
    if server_log | grep -aq '\[DDCTest\] DONE'; then break; fi
    offscreen
    sleep 5
  done
  server_log | grep -a '\[DDCTest\]\|Gate:\|Uploaded since' | sed 's/^\[[^]]*\]\[[^]]*\]//' | tail -200
  cmd "DDC.Dev.Crates"
  cmd "DDC.Status"
fi

if [ -n "$CLIENT_PID" ]; then
  sleep 12
  echo "== client view (last lines):"
  grep -a '\[DDCClient\]\|NMT_Failure\|LogNet: .*Welcomed\|Join succeeded\|Critical error\|Fatal error' "$CLIENT_LOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | tail -12 | cut -c1-400
  echo "== chat messages the client received (modded client only; a vanilla client has the screenshots):"
  grep -a '\[DDCClient\] chat' "$CLIENT_LOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | cut -c1-500
  kill "$SHOT_PID" 2>/dev/null
  echo "== client screenshots: $(ls "$SHOT_DIR" | grep -c "^$SHOT_TAG-") in $SHOT_DIR ($SHOT_TAG-*)"
fi

echo "== stopping"
bash "$DS" stop
[ -n "$CLIENT_PID" ] && kill_client
sleep 20

echo "== cleaning up the server save folder (only files this test added)"
if [ "$TARGET" = linux ]; then
  wsl_run "ls -1 $LINUX_SAVES" > /tmp/ddc_ds_after.txt
  comm -13 <(sort /tmp/ddc_ds_before.txt) <(sort /tmp/ddc_ds_after.txt) | while read -r f; do
    case "$f" in "$TEST_SAVE.sav"|"$TEST_SESSION"*) wsl_run "rm -f \"$LINUX_SAVES/$f\""; echo "removed $f";; *) echo "NEW FILE left in place: $f";; esac
  done
  if wsl_run "pgrep -f 'FactoryServer-Linu[x]'" | grep -q .; then echo "!! the Linux server is still running"; fi
else
  ls -1 "$SERVER_SAVES" > /tmp/ddc_ds_after.txt
  comm -13 <(sort /tmp/ddc_ds_before.txt) <(sort /tmp/ddc_ds_after.txt) | while read -r f; do
    case "$f" in "$TEST_SAVE.sav"|"$TEST_SESSION"*) rm -f "$SERVER_SAVES/$f"; echo "removed $f";; *) echo "NEW FILE left in place: $f";; esac
  done
fi
echo "== done"
