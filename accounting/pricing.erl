%%%-------------------------------------------------------------------
%%% @doc Fixed-point integer circuit pricing engine.
%%% Never uses binary floating-point for settlement.
%%%-------------------------------------------------------------------
-module(pricing).
-export([charge/1, setup_fee/1, reserved_charge/3, usage_charge/2, priority_charge/3]).

-define(MINOR_UNITS, 10000).  % 1 major = 10000 minor

%% Charge = SetupFee + ReservedCapacityCharge + UsageCharge + PriorityCharge
charge(#{tariff := T, reserved_bps := Bps, duration_s := Dur,
         bytes := Bytes, priority := Prio}) ->
    S = setup_fee(T),
    R = reserved_charge(T, Bps, Dur),
    U = usage_charge(T, Bytes),
    P = priority_charge(T, S + R, Prio),
    S + R + U + P.

setup_fee(#{setup := S}) -> S.

%% rate_bps is minor-units per bps per second
reserved_charge(#{rate_bps := Rate}, Bps, Dur) when Bps >= 0, Dur >= 0 ->
    %% 128-bit style intermediate via careful ordering
    (Bps * Rate * Dur).

usage_charge(#{rate_byte := Rate}, Bytes) when Bytes >= 0 ->
    Bytes * Rate.

priority_charge(#{prio_mult := Mult}, Base, Prio) when Prio >= 0, Prio =< 7 ->
    (Base * Mult * Prio) div 100.   % Mult is percentage points

-ifdef(TEST).
-include_lib("eunit/include/eunit.hrl").

basic_charge_test() ->
    T = #{setup => 1000, rate_bps => 1, rate_byte => 2, prio_mult => 10},
    C = charge(#{tariff => T, reserved_bps => 100, duration_s => 10,
                 bytes => 50, priority => 2}),
    %% Setup 1000 + reserved 1000 + usage 100 + prio (2000*10*2)/100 = 400
    ?assertEqual(1000 + 1000 + 100 + 400, C).
-endif.
