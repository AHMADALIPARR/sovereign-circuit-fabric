# Sovereign Circuit Fabric — Tcl 9 Provisioning Interface
# Circuit commands, tariff management, configuration.

package require Tcl 9.0

namespace eval ::scf {
    variable circuits
    array set circuits {}
    variable next_id 1
    variable tariffs
    array set tariffs {}
}

proc ::scf::create_circuit {src dst bw priority qos tariff} {
    variable circuits
    variable next_id
    set cid $next_id
    incr next_id
    set circuits($cid) [dict create \
        state FREE \
        src $src \
        dst $dst \
        bw $bw \
        priority $priority \
        qos $qos \
        tariff $tariff \
        reserved 0 \
        bytes 0]
    return $cid
}

proc ::scf::request {cid} {
    variable circuits
    if {![info exists circuits($cid)]} {
        return -code error "unknown circuit $cid"
    }
    dict with circuits($cid) {
        if {$state ne "FREE"} {
            return -code error "invalid transition from $state"
        }
        set state REQUESTED
    }
    return ok
}

proc ::scf::admit {cid} {
    variable circuits
    dict with circuits($cid) {
        if {$state ne "REQUESTED"} {
            return -code error "invalid transition from $state"
        }
        set state ADMITTED
    }
    return ok
}

proc ::scf::reserve {cid} {
    variable circuits
    dict with circuits($cid) {
        if {$state ne "ADMITTED"} {
            return -code error "invalid transition from $state"
        }
        set reserved $bw
        set state RESERVED
    }
    return ok
}

proc ::scf::install {cid labels ingress egress} {
    variable circuits
    dict with circuits($cid) {
        if {$state ne "RESERVED"} {
            return -code error "invalid transition from $state"
        }
        dict set circuits($cid) labels $labels
        dict set circuits($cid) ingress $ingress
        dict set circuits($cid) egress $egress
        set state INSTALLED
    }
    return ok
}

proc ::scf::activate {cid} {
    variable circuits
    dict with circuits($cid) {
        if {$state ne "INSTALLED"} {
            return -code error "invalid transition from $state"
        }
        set state ACTIVE
    }
    return ok
}

proc ::scf::drain {cid} {
    variable circuits
    dict with circuits($cid) {
        if {$state ne "ACTIVE"} {
            return -code error "invalid transition from $state"
        }
        set state DRAINING
    }
    return ok
}

proc ::scf::release {cid} {
    variable circuits
    dict with circuits($cid) {
        if {$state ni {DRAINING FAILED}} {
            return -code error "invalid transition from $state"
        }
        set reserved 0
        set state RELEASED
    }
    return ok
}

proc ::scf::show {cid} {
    variable circuits
    if {![info exists circuits($cid)]} {
        return -code error "unknown circuit $cid"
    }
    return $circuits($cid)
}

proc ::scf::define_tariff {id setup_fee rate_bps rate_byte priority_mult} {
    variable tariffs
    set tariffs($id) [dict create \
        setup $setup_fee \
        rate_bps $rate_bps \
        rate_byte $rate_byte \
        prio_mult $priority_mult]
    return ok
}

# Example usage (not executed on load):
# set cid [::scf::create_circuit A B 1000000 3 1 1]
# ::scf::request $cid
# ::scf::admit $cid
# ...
