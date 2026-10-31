---------------------------- MODULE CircuitState ----------------------------
(***************************************************************************)
(* Sovereign Circuit Fabric — TLA+ model of circuit lifecycle               *)
(* Invariants: legal transitions, no double reservation, resource release  *)
(***************************************************************************)

EXTENDS Integers, FiniteSets, Sequences

CONSTANTS Circuits, MaxBandwidth

VARIABLES state, reserved, totalReserved

TypeOK ==
  /\ state \in [Circuits -> {"FREE", "REQUESTED", "ADMITTED", "RESERVED",
                             "INSTALLED", "ACTIVE", "DRAINING", "RELEASED", "FAILED"}]
  /\ reserved \in [Circuits -> 0..MaxBandwidth]
  /\ totalReserved \in 0..MaxBandwidth

Init ==
  /\ state = [c \in Circuits |-> "FREE"]
  /\ reserved = [c \in Circuits |-> 0]
  /\ totalReserved = 0

Request(c) ==
  /\ state[c] = "FREE"
  /\ state' = [state EXCEPT ![c] = "REQUESTED"]
  /\ UNCHANGED <<reserved, totalReserved>>

Admit(c) ==
  /\ state[c] = "REQUESTED"
  /\ state' = [state EXCEPT ![c] = "ADMITTED"]
  /\ UNCHANGED <<reserved, totalReserved>>

Reject(c) ==
  /\ state[c] = "REQUESTED"
  /\ state' = [state EXCEPT ![c] = "FREE"]
  /\ UNCHANGED <<reserved, totalReserved>>

Reserve(c, bw) ==
  /\ state[c] = "ADMITTED"
  /\ bw > 0
  /\ totalReserved + bw <= MaxBandwidth
  /\ state' = [state EXCEPT ![c] = "RESERVED"]
  /\ reserved' = [reserved EXCEPT ![c] = bw]
  /\ totalReserved' = totalReserved + bw

Install(c) ==
  /\ state[c] = "RESERVED"
  /\ state' = [state EXCEPT ![c] = "INSTALLED"]
  /\ UNCHANGED <<reserved, totalReserved>>

Activate(c) ==
  /\ state[c] = "INSTALLED"
  /\ state' = [state EXCEPT ![c] = "ACTIVE"]
  /\ UNCHANGED <<reserved, totalReserved>>

Drain(c) ==
  /\ state[c] = "ACTIVE"
  /\ state' = [state EXCEPT ![c] = "DRAINING"]
  /\ UNCHANGED <<reserved, totalReserved>>

Release(c) ==
  /\ state[c] \in {"DRAINING", "FAILED"}
  /\ state' = [state EXCEPT ![c] = "RELEASED"]
  /\ totalReserved' = totalReserved - reserved[c]
  /\ reserved' = [reserved EXCEPT ![c] = 0]

Fail(c) ==
  /\ state[c] \notin {"FREE", "RELEASED", "FAILED"}
  /\ state' = [state EXCEPT ![c] = "FAILED"]
  /\ UNCHANGED <<reserved, totalReserved>>

Recycle(c) ==
  /\ state[c] = "RELEASED"
  /\ state' = [state EXCEPT ![c] = "FREE"]
  /\ UNCHANGED <<reserved, totalReserved>>

Next ==
  \E c \in Circuits :
    \/ Request(c)
    \/ Admit(c)
    \/ Reject(c)
    \/ \E bw \in 1..MaxBandwidth : Reserve(c, bw)
    \/ Install(c)
    \/ Activate(c)
    \/ Drain(c)
    \/ Release(c)
    \/ Fail(c)
    \/ Recycle(c)

Spec == Init /\ [][Next]_<<state, reserved, totalReserved>>

(***************************************************************************)
(* Invariants                                                              *)
(***************************************************************************)

NoOverCommit == totalReserved <= MaxBandwidth

ReservedOnlyWhenHeld ==
  \A c \in Circuits :
    reserved[c] > 0 => state[c] \in {"RESERVED", "INSTALLED", "ACTIVE", "DRAINING", "FAILED"}

FreeHasZero ==
  \A c \in Circuits :
    state[c] = "FREE" => reserved[c] = 0

==============================================================================
