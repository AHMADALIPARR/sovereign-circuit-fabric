%%%-------------------------------------------------------------------
%%% @doc Sovereign Circuit Fabric — circuit actor lifecycle tests.
%%%
%%% Plain runner (no eunit dependency): compile with circuit_actor
%%% and pricing, then run_tests.sh executes run/0. Every check is an
%%% exact match; a mismatch crashes the run and the script fails.
%%%-------------------------------------------------------------------
-module(circuit_actor_test).
-export([run/0]).

state(Pid) ->
    {S, _} = sys:get_state(Pid),
    S.

run() ->
    {ok, Pid} = circuit_actor:start_link(#{id => 7, namespace => 0}),
    free = state(Pid),

    %% illegal transition from FREE
    {error, {invalid_transition, free}} = circuit_actor:admit(Pid),
    io:format("illegal admit from FREE refused~n"),

    %% full lifecycle
    ok = circuit_actor:request(Pid, #{src => a, dst => b, bw => 1000000,
                                      priority => 3, qos => 1,
                                      tariff_id => 1}),
    requested = state(Pid),
    ok = circuit_actor:admit(Pid),
    admitted = state(Pid),
    ok = circuit_actor:reserve(Pid),
    reserved = state(Pid),
    ok = circuit_actor:install(Pid, #{labels => [42], ingress => p1,
                                      egress => p2}),
    installed = state(Pid),
    ok = circuit_actor:activate(Pid),
    active = state(Pid),
    ok = circuit_actor:drain(Pid),
    draining = state(Pid),
    ok = circuit_actor:release(Pid),
    released = state(Pid),
    io:format("lifecycle FREE->...->RELEASED ok~n"),

    %% recycle through the API
    ok = circuit_actor:recycle(Pid),
    free = state(Pid),
    io:format("recycle RELEASED->FREE ok~n"),

    %% rejection: REQUESTED --reject--> FREE
    ok = circuit_actor:request(Pid, #{bw => 500}),
    requested = state(Pid),
    {rejected, policy} = circuit_actor:reject(Pid, policy),
    free = state(Pid),
    io:format("reject REQUESTED->FREE ok~n"),

    %% fail is not a legal transition from REQUESTED
    ok = circuit_actor:request(Pid, #{bw => 500}),
    {error, {invalid_transition, requested}} =
        circuit_actor:fail(Pid, boom),
    requested = state(Pid),
    io:format("fail from REQUESTED refused~n"),

    %% timeout: REQUESTED --timeout--> FREE
    timeout = circuit_actor:timeout(Pid),
    free = state(Pid),
    io:format("timeout REQUESTED->FREE ok~n"),

    %% failure path from ACTIVE, then release from FAILED
    ok = circuit_actor:request(Pid, #{bw => 500}),
    ok = circuit_actor:admit(Pid),
    ok = circuit_actor:reserve(Pid),
    ok = circuit_actor:install(Pid, #{}),
    ok = circuit_actor:activate(Pid),
    {failed, link_down} = circuit_actor:fail(Pid, link_down),
    failed = state(Pid),
    ok = circuit_actor:release(Pid),
    released = state(Pid),
    ok = circuit_actor:recycle(Pid),
    free = state(Pid),
    io:format("fail ACTIVE->FAILED->RELEASED ok~n"),

    %% pricing regression (documented example in pricing.erl)
    T = #{setup => 1000, rate_bps => 1, rate_byte => 2,
          prio_mult => 10},
    2500 = pricing:charge(#{tariff => T, reserved_bps => 100,
                            duration_s => 10, bytes => 50,
                            priority => 2}),
    1000 = pricing:charge(#{tariff => T, reserved_bps => 0,
                            duration_s => 0, bytes => 0,
                            priority => 0}),
    io:format("pricing documented example = 2500 ok~n"),

    io:format("ALL TESTS PASSED~n"),
    ok.
