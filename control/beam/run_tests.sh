#!/bin/sh
# Compile all Erlang modules and run the circuit actor test suite.
# Requires Erlang/OTP (erl, erlc) on PATH.
set -e
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
erlc -o "$OUT" circuit_actor.erl circuit_actor_test.erl
erlc -o "$OUT" ../../accounting/pricing.erl
erl -noshell -pa "$OUT" -eval 'ok = circuit_actor_test:run(), halt().'
