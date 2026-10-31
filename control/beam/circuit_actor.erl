%%%-------------------------------------------------------------------
%%% @doc Sovereign Circuit Fabric — Circuit Actor
%%%
%%% One supervised logical process per active circuit.
%%% Packet forwarding stays outside BEAM.
%%%-------------------------------------------------------------------
-module(circuit_actor).
-behaviour(gen_statem).

-export([start_link/1, request/2, admit/1, reject/2, reserve/1,
         install/2, activate/1, drain/1, release/1, fail/2,
         timeout/1, recycle/1]).
-export([init/1, callback_mode/0, terminate/3, code_change/4]).
-export([free/3, requested/3, admitted/3, reserved/3, installed/3,
         active/3, draining/3, failed/3, released/3]).

-record(circuit, {
          id            :: non_neg_integer(),
          namespace     :: non_neg_integer(),
          src           :: term(),
          dst           :: term(),
          reserved_bps  :: non_neg_integer(),
          priority      :: 0..7,
          qos           :: 0..15,
          tariff_id     :: non_neg_integer(),
          labels        :: term(),
          ingress       :: term(),
          egress        :: term(),
          setup_ts      :: integer(),
          bytes_sent    = 0 :: non_neg_integer(),
          bytes_recv    = 0 :: non_neg_integer(),
          owner         :: pid()
         }).

%%% API

start_link(Args) ->
    gen_statem:start_link(?MODULE, Args, []).

request(Pid, Spec) ->
    gen_statem:call(Pid, {request, Spec}).

admit(Pid) ->
    gen_statem:call(Pid, admit).

reject(Pid, Reason) ->
    gen_statem:call(Pid, {reject, Reason}).

reserve(Pid) ->
    gen_statem:call(Pid, reserve).

install(Pid, InstallSpec) ->
    gen_statem:call(Pid, {install, InstallSpec}).

activate(Pid) ->
    gen_statem:call(Pid, activate).

drain(Pid) ->
    gen_statem:call(Pid, drain).

release(Pid) ->
    gen_statem:call(Pid, release).

fail(Pid, Reason) ->
    gen_statem:call(Pid, {fail, Reason}).

timeout(Pid) ->
    gen_statem:call(Pid, timeout).

recycle(Pid) ->
    gen_statem:call(Pid, recycle).

%%% gen_statem callbacks

callback_mode() -> state_functions.

init(#{id := Id, namespace := Ns} = Args) ->
    C = #circuit{
           id = Id,
           namespace = Ns,
           src = maps:get(src, Args, undefined),
           dst = maps:get(dst, Args, undefined),
           reserved_bps = 0,
           priority = maps:get(priority, Args, 0),
           qos = maps:get(qos, Args, 0),
           tariff_id = maps:get(tariff_id, Args, 0),
           setup_ts = erlang:system_time(nanosecond),
           owner = self()
          },
    {ok, free, C}.

%% FREE
free({call, From}, {request, Spec}, C) ->
    C1 = C#circuit{
           src = maps:get(src, Spec, C#circuit.src),
           dst = maps:get(dst, Spec, C#circuit.dst),
           reserved_bps = maps:get(bw, Spec, 0),
           priority = maps:get(priority, Spec, C#circuit.priority),
           qos = maps:get(qos, Spec, C#circuit.qos),
           tariff_id = maps:get(tariff_id, Spec, C#circuit.tariff_id)
          },
    {next_state, requested, C1, [{reply, From, ok}]};
free(EventType, Event, Data) ->
    handle_common(EventType, Event, free, Data).

%% REQUESTED
requested({call, From}, admit, C) ->
    {next_state, admitted, C, [{reply, From, ok}]};
requested({call, From}, {reject, Reason}, C) ->
    {next_state, free, C#circuit{reserved_bps = 0},
     [{reply, From, {rejected, Reason}}]};
requested({call, From}, timeout, C) ->
    {next_state, free, C#circuit{reserved_bps = 0},
     [{reply, From, timeout}]};
requested(EventType, Event, Data) ->
    handle_common(EventType, Event, requested, Data).

%% ADMITTED
admitted({call, From}, reserve, C) ->
    %% Resource reservation is performed by the orchestration layer;
    %% this actor only records the successful outcome.
    {next_state, reserved, C, [{reply, From, ok}]};
admitted({call, From}, {fail, Reason}, C) ->
    {next_state, failed, C, [{reply, From, {failed, Reason}}]};
admitted(EventType, Event, Data) ->
    handle_common(EventType, Event, admitted, Data).

%% RESERVED
reserved({call, From}, {install, InstallSpec}, C) ->
    C1 = C#circuit{
           labels  = maps:get(labels, InstallSpec, undefined),
           ingress = maps:get(ingress, InstallSpec, undefined),
           egress  = maps:get(egress, InstallSpec, undefined)
          },
    {next_state, installed, C1, [{reply, From, ok}]};
reserved({call, From}, {fail, Reason}, C) ->
    {next_state, failed, C, [{reply, From, {failed, Reason}}]};
reserved(EventType, Event, Data) ->
    handle_common(EventType, Event, reserved, Data).

%% INSTALLED
installed({call, From}, activate, C) ->
    {next_state, active, C, [{reply, From, ok}]};
installed({call, From}, {fail, Reason}, C) ->
    {next_state, failed, C, [{reply, From, {failed, Reason}}]};
installed(EventType, Event, Data) ->
    handle_common(EventType, Event, installed, Data).

%% ACTIVE
active({call, From}, drain, C) ->
    {next_state, draining, C, [{reply, From, ok}]};
active({call, From}, {fail, Reason}, C) ->
    {next_state, failed, C, [{reply, From, {failed, Reason}}]};
active(EventType, Event, Data) ->
    handle_common(EventType, Event, active, Data).

%% DRAINING
draining({call, From}, release, C) ->
    %% Final accounting event emitted by accounting subsystem
    {next_state, released, C, [{reply, From, ok}]};
draining({call, From}, {fail, Reason}, C) ->
    {next_state, failed, C, [{reply, From, {failed, Reason}}]};
draining(EventType, Event, Data) ->
    handle_common(EventType, Event, draining, Data).

%% FAILED
failed({call, From}, release, C) ->
    {next_state, released, C, [{reply, From, ok}]};
failed(EventType, Event, Data) ->
    handle_common(EventType, Event, failed, Data).

%% RELEASED
released({call, From}, recycle, C) ->
    {next_state, free, C#circuit{reserved_bps = 0}, [{reply, From, ok}]};
released(EventType, Event, Data) ->
    handle_common(EventType, Event, released, Data).

handle_common({call, From}, _Event, State, Data) ->
    {keep_state, Data, [{reply, From, {error, {invalid_transition, State}}}]};
handle_common(_EventType, _Event, _State, Data) ->
    {keep_state, Data}.

terminate(_Reason, _State, _Data) ->
    ok.

code_change(_OldVsn, State, Data, _Extra) ->
    {ok, State, Data}.
