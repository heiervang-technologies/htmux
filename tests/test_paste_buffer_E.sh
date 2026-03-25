#!/usr/bin/env bash
# test_paste_buffer_E.sh - Comprehensive tests for htmux paste-buffer -E
# Tests: silent blocker, concurrent paste guard, pane exit crash, consistency
#
# Usage: HTMUX=/path/to/htmux bash tests/test_paste_buffer_E.sh

set -euo pipefail

HTMUX="${HTMUX:-/home/me/ht/forks/htmux/htmux}"
SOCKET="paste-test-$$"
SESSION="pt$$"
WORKDIR="/tmp/htmux-test-$$"
PASS=0
FAIL=0

mkdir -p "$WORKDIR"

cleanup() {
    $HTMUX -L "$SOCKET" kill-server 2>/dev/null || true
    rm -rf "$WORKDIR"
    echo ""
    echo "=== Results: $PASS passed, $FAIL failed ==="
    [[ "$FAIL" -eq 0 ]] && exit 0 || exit 1
}
trap cleanup EXIT INT TERM

H="$HTMUX -L $SOCKET"

# Create the test app script once
cat > "$WORKDIR/bp_app.py" <<'PYEOF'
import sys, os, tty, termios, time

logfile = sys.argv[1]
delay = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5

sys.stdout.write("\033[?2004h")
sys.stdout.flush()
fd = sys.stdin.fileno()
old = termios.tcgetattr(fd)
tty.setraw(fd)
buf = ""
in_paste = False

with open(logfile, "a") as log:
    log.write("APP_STARTED\n")
    log.flush()
    sys.stdout.write("ready> ")
    sys.stdout.flush()
    while True:
        try:
            ch = os.read(fd, 1).decode("utf-8", errors="replace")
            if not ch:
                break
            buf += ch
            if buf.endswith("\x1b[200~"):
                in_paste = True
                buf = ""
                continue
            if buf.endswith("\x1b[201~"):
                in_paste = False
                buf = buf[:-len("\x1b[201~")]
                time.sleep(delay)
                termios.tcflush(fd, termios.TCIFLUSH)
                continue
            if ch == '\r' and not in_paste:
                line = buf.strip()
                if line:
                    log.write(f"SUBMIT:{line}\n")
                    log.flush()
                    sys.stdout.write(f"\r\nok\r\nready> ")
                    sys.stdout.flush()
                buf = ""
        except (KeyboardInterrupt, EOFError, OSError):
            break
termios.tcsetattr(fd, termios.TCSADRAIN, old)
PYEOF

# Start htmux server with initial session
$HTMUX -L "$SOCKET" new-session -d -s "$SESSION" -x 120 -y 40
sleep 0.5

echo "=== htmux paste-buffer -E edge case tests ==="
echo ""

# Helper: start app in a fresh window, return window name
start_app() {
    local win="$1" log="$2" delay="${3:-1.5}"
    $H new-window -t "$SESSION" -n "$win"
    sleep 0.3
    $H send-keys -t "$SESSION:$win" "python3 $WORKDIR/bp_app.py $log $delay" C-m
    sleep 2
    grep -q "APP_STARTED" "$log"
}

# --- Test 1: Silent Blocker (hits max timeout) ---
echo "-- Test 1: Silent blocker app (max timeout) --"
LOG="$WORKDIR/test1.log"
> "$LOG"
if start_app t1 "$LOG" 1.5; then
    $H set-buffer -b test "silent blocker test"
    START=$(date +%s)
    $H paste-buffer -E 1 -p -b test -t "$SESSION:t1"
    END=$(date +%s)
    DURATION=$((END - START))
    sleep 1

    if grep -q "SUBMIT:silent blocker test" "$LOG" && [ "$DURATION" -ge 1 ]; then
        echo "  PASS: silent blocker (blocked ${DURATION}s, enter delivered)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: silent blocker (duration=${DURATION}s)"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  FAIL: app did not start"
    FAIL=$((FAIL + 1))
fi

# --- Test 2: Concurrent paste guard ---
echo ""
echo "-- Test 2: Concurrent paste-pending guard --"
LOG="$WORKDIR/test2.log"
> "$LOG"
if start_app t2 "$LOG" 1.5; then
    $H set-buffer -b test "concurrent test"
    $H paste-buffer -E 1 -p -b test -t "$SESSION:t2" &
    PID1=$!
    sleep 0.3

    # Second paste should fail; error goes to tmux message log
    $H paste-buffer -E 1 -p -b test -t "$SESSION:t2" 2>/dev/null || true
    sleep 0.3
    MSGS=$($H show-messages 2>&1 || true)
    if echo "$MSGS" | grep -q "already pending"; then
        echo "  PASS: concurrent paste rejected (error in message log)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: concurrent paste was not rejected"
        echo "    messages: $MSGS"
        FAIL=$((FAIL + 1))
    fi
    wait $PID1 2>/dev/null || true
else
    echo "  FAIL: app did not start"
    FAIL=$((FAIL + 1))
fi

# --- Test 3: Pane exit during pending paste (crash test) ---
echo ""
echo "-- Test 3: Pane exit during pending paste (crash test) --"
$H new-window -t "$SESSION" -n crashtest
sleep 0.3
$H set-buffer -b test "exit"
$H paste-buffer -E 1 -b test -t "$SESSION:crashtest" 2>/dev/null &
PID2=$!
sleep 3

if $H has-session -t "$SESSION" 2>/dev/null; then
    echo "  PASS: server survived pane exit during pending paste"
    PASS=$((PASS + 1))
else
    echo "  FAIL: server crashed"
    FAIL=$((FAIL + 1))
fi
wait $PID2 2>/dev/null || true

# --- Test 4: 10x consistency ---
echo ""
echo "-- Test 4: 10x paste-buffer -E consistency --"
LOG="$WORKDIR/test4.log"
> "$LOG"
if start_app t4 "$LOG" 1.0; then
    for i in $(seq 1 10); do
        $H set-buffer -b test "consistency $i"
        $H paste-buffer -E 1 -p -b test -t "$SESSION:t4"
        sleep 0.1
    done
    sleep 3

    COUNT=$(grep -c "SUBMIT:consistency" "$LOG")
    if [ "$COUNT" -eq 10 ]; then
        echo "  PASS: 10/10 paste-enter operations succeeded"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $COUNT/10 succeeded"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  FAIL: app did not start"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "All tests complete."
