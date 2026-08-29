#!/usr/bin/env bash
#
# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
#
# Drive the bridge against a real MQTT broker.
#
# Everything above the socket is already covered by tests against a fake, and a
# fake agrees with whatever the client does -- including whatever it does
# wrong. Mosquitto does not. It is an independent implementation that will
# reject a malformed CONNECT, ignore a subscription it did not understand, and
# publish a last will exactly when the specification says to, none of which our
# own tests can establish about our own client.
#
# Two phases:
#   A  a clean run: connect, subscribe, report, take a command, say goodbye
#   B  a hard kill: the broker publishes the will on our behalf
#
# Usage: scripts/integration-broker.sh [path-to-taiseia-bridge]

set -uo pipefail

BIN="${1:-./build/taiseia-bridge}"
PORT="${MQTT_PORT:-11883}"
DEVICE="IT-0001"
PREFIX="v1"
FROM="${PREFIX}/from/device/${DEVICE}"
TO="${PREFIX}/to/device/${DEVICE}"

DIR="$(mktemp -d)"
MOSQ_PID=""
SUB_PID=""
BRIDGE_PID=""
FAILURES=0

cleanup() {
    [ -n "$BRIDGE_PID" ] && kill -9 "$BRIDGE_PID" 2>/dev/null
    [ -n "$SUB_PID" ]    && kill    "$SUB_PID"    2>/dev/null
    [ -n "$MOSQ_PID" ]   && kill    "$MOSQ_PID"   2>/dev/null
    wait 2>/dev/null
    rm -rf "$DIR"
}
trap cleanup EXIT

say()  { printf '  %s\n' "$*"; }
head2() { printf '\n== %s\n' "$*"; }

fail() {
    printf '  FAIL %s\n' "$*"
    FAILURES=$((FAILURES + 1))
}

# Wait until a file contains a pattern, or give up. Everything here is local,
# so a slow answer means something is wrong rather than merely busy.
wait_for() {
    local file="$1" pattern="$2" secs="${3:-15}" i
    for ((i = 0; i < secs * 10; i++)); do
        if grep -q -- "$pattern" "$file" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# Wait until a file grows past a mark. Used where a pattern would match a
# message from before the thing under test happened.
wait_lines() {
    local file="$1" mark="$2" secs="${3:-15}" i now
    for ((i = 0; i < secs * 10; i++)); do
        now=$(wc -l < "$file" 2>/dev/null || echo 0)
        if [ "$now" -gt "$mark" ]; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

expect() {
    local file="$1" pattern="$2" what="$3"
    if grep -q -- "$pattern" "$file" 2>/dev/null; then
        say "ok   $what"
    else
        fail "$what"
        printf '       looked for: %s\n' "$pattern"
    fi
}

[ -x "$BIN" ] || { echo "no such executable: $BIN" >&2; exit 2; }
command -v mosquitto     >/dev/null || { echo "mosquitto not installed" >&2; exit 2; }
command -v mosquitto_sub >/dev/null || { echo "mosquitto-clients not installed" >&2; exit 2; }

# ------------------------------------------------------------------- broker --

cat > "$DIR/mosquitto.conf" <<EOF
listener $PORT 127.0.0.1
allow_anonymous true
EOF

mosquitto -c "$DIR/mosquitto.conf" > "$DIR/mosquitto.log" 2>&1 &
MOSQ_PID=$!

# Ask the broker rather than read its log: the wording of the startup message
# is not a contract, but answering a publish is.
broker_ready=0
for ((i = 0; i < 100; i++)); do
    if mosquitto_pub -h 127.0.0.1 -p "$PORT" -t "probe" -m "x" >/dev/null 2>&1; then
        broker_ready=1
        break
    fi
    sleep 0.1
done
if [ "$broker_ready" -ne 1 ]; then
    echo "broker did not start" >&2
    cat "$DIR/mosquitto.log" >&2
    exit 1
fi

cat > "$DIR/bridge.conf" <<EOF
device_id       = $DEVICE
mqtt_host       = 127.0.0.1
mqtt_port       = $PORT
mqtt_keepalive  = 10
topic_prefix    = $PREFIX
report_interval = 5
log_level       = info
EOF

# ------------------------------------------------------- phase A: a clean run

head2 "phase A: connect, report, take a command, say goodbye"

mosquitto_sub -h 127.0.0.1 -p "$PORT" -t "${FROM}/#" -v > "$DIR/rx.txt" 2>&1 &
SUB_PID=$!
sleep 0.5

"$BIN" --link sim -c "$DIR/bridge.conf" --run-for 12 > "$DIR/bridge.log" 2>&1 &
BRIDGE_PID=$!

if ! wait_for "$DIR/rx.txt" "${FROM}/telemetry" 20; then
    fail "no telemetry arrived"
    cat "$DIR/bridge.log"
    exit 1
fi

expect "$DIR/rx.txt" "${FROM}/status .*\"online\":true"     "status says online"
expect "$DIR/rx.txt" "${FROM}/telemetry .*\"0\":1"          "telemetry carries service 0"
expect "$DIR/rx.txt" "${FROM}/telemetry .*\"7\":62"         "telemetry carries service 7"
expect "$DIR/rx.txt" "${FROM}/capabilities .*SIMULATOR"     "capabilities name the appliance"
expect "$DIR/rx.txt" "${FROM}/capabilities .*\"format\":\"general\"" \
                                                            "capabilities state the record format"

# A control command, and the acknowledgement it is owed.
mosquitto_pub -h 127.0.0.1 -p "$PORT" -t "${TO}/control" \
    -m '{"tag":"t1","attributes":{"3":70,"7":1}}'

if ! wait_for "$DIR/rx.txt" "control/acknowledgement" 15; then
    fail "no acknowledgement arrived"
else
    expect "$DIR/rx.txt" "acknowledgement .*\"tag\":\"t1\"" \
           "the tag comes back byte for byte"
    expect "$DIR/rx.txt" "acknowledgement .*\"3\":\"ok\"" \
           "the writable service was written"
    # Service 7 is declared read-only, and that is settled from the capability
    # set without an exchange.
    expect "$DIR/rx.txt" "acknowledgement .*\"7\":\"read_only\"" \
           "the read-only service was refused by name"
    expect "$DIR/rx.txt" "acknowledgement .*\"success\":false" \
           "a partly refused command is not a success"
fi

# The write should show up in the next report.
if wait_for "$DIR/rx.txt" "${FROM}/telemetry .*\"3\":70" 15; then
    say "ok   the written value appears in telemetry"
else
    fail "the written value never appeared in telemetry"
fi

# A clean stop says goodbye rather than leaving the broker to do it.
wait "$BRIDGE_PID" 2>/dev/null
BRIDGE_PID=""
expect "$DIR/rx.txt" "${FROM}/status .*\"online\":false" "a clean stop says goodbye"

kill "$SUB_PID" 2>/dev/null; wait "$SUB_PID" 2>/dev/null; SUB_PID=""

# Capabilities and status are retained, so a subscriber that arrives after the
# device has gone still learns what it was.
timeout 5 mosquitto_sub -h 127.0.0.1 -p "$PORT" -t "${FROM}/capabilities" \
    -C 1 > "$DIR/retained.txt" 2>&1
if [ -s "$DIR/retained.txt" ]; then
    expect "$DIR/retained.txt" "SIMULATOR" "capabilities were retained"
else
    fail "capabilities were not retained"
fi

# ------------------------------------------------------- phase B: a hard kill

head2 "phase B: the broker publishes the will"

# Phase A left a retained "offline" here. Left in place it would be delivered
# to the new subscriber the moment it connects, and every check below would
# pass without the will ever being sent.
mosquitto_pub -h 127.0.0.1 -p "$PORT" -r -n -t "${FROM}/status"
sleep 0.3

mosquitto_sub -h 127.0.0.1 -p "$PORT" -t "${FROM}/status" -v > "$DIR/will.txt" 2>&1 &
SUB_PID=$!
sleep 0.5

"$BIN" --link sim -c "$DIR/bridge.conf" > "$DIR/bridge2.log" 2>&1 &
BRIDGE_PID=$!

if ! wait_for "$DIR/will.txt" "\"online\":true" 20; then
    fail "the bridge never came online"
    cat "$DIR/bridge2.log"
else
    # The bridge publishes an offline status of its own while it is still
    # identifying the appliance, so match on what arrives *after* the kill
    # rather than on a pattern that was already true.
    mark=$(wc -l < "$DIR/will.txt")

    # No chance to say anything: whatever arrives now is the will we handed
    # the broker at CONNECT.
    kill -9 "$BRIDGE_PID" 2>/dev/null
    wait "$BRIDGE_PID" 2>/dev/null
    BRIDGE_PID=""

    if wait_lines "$DIR/will.txt" "$mark" 15; then
        last=$(tail -n 1 "$DIR/will.txt")

        case "$last" in
            *'"online":false'*)
                say "ok   the will was published on our behalf" ;;
            *)
                fail "what arrived after the kill was not the will"
                printf '       %s\n' "$last" ;;
        esac

        # The will carries no timestamp on purpose: the broker sends it at a
        # moment we cannot know, and a time taken at CONNECT would be a lie
        # about when the device went away.
        case "$last" in
            *'"device_time":null'*)
                say "ok   the will carries no timestamp" ;;
            *)
                fail "the will should carry a null timestamp" ;;
        esac
    else
        fail "the will was never published"
    fi
fi

# ------------------------------------------------------------------ verdict --

head2 "result"
if [ "$FAILURES" -eq 0 ]; then
    say "all checks passed"
    exit 0
fi

say "$FAILURES check(s) failed"
printf '\n--- what the broker delivered ---\n'
cat "$DIR/rx.txt" 2>/dev/null
printf '\n--- what the bridge logged ---\n'
cat "$DIR/bridge.log" 2>/dev/null
exit 1
