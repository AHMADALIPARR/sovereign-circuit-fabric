/* -*- P4_16 -*- */
/*
 * Sovereign Circuit Fabric — P4_16 Forwarding Pipeline
 * Custom circuit-frame format (Magic 0x5343).
 * No assumption of Ethernet or IP for internal switching.
 */

#include <core.p4>
#include <v1model.p4>

const bit<16> SCF_MAGIC = 0x5343;
const bit<8>  SCF_VERSION = 0x01;

typedef bit<32> CircuitId_t;
typedef bit<16> NamespaceId_t;
typedef bit<9>  PortId_t;

header scf_t {
    bit<16> magic;
    bit<8>  version;
    bit<8>  flags;
    bit<32> circuit_id;
    bit<16> namespace_id;
    bit<8>  priority;
    bit<8>  qos_class;
    bit<32> sequence;
    bit<16> payload_len;
    bit<16> checksum;
}

struct metadata_t {
    bit<1>  drop;
    bit<1>  admitted;
    PortId_t egress_port;
    bit<32> meter_color;
}

struct headers_t {
    scf_t scf;
}

parser SCFParser(packet_in packet,
                 out headers_t hdr,
                 inout metadata_t meta,
                 inout standard_metadata_t standard_metadata) {
    state start {
        packet.extract(hdr.scf);
        transition select(hdr.scf.magic, hdr.scf.version) {
            (SCF_MAGIC, SCF_VERSION): accept;
            default: reject;
        }
    }
}

control SCFVerifyChecksum(inout headers_t hdr, inout metadata_t meta) {
    apply { /* checksum verification performed in ingress for determinism */ }
}

control SCFIngress(inout headers_t hdr,
                   inout metadata_t meta,
                   inout standard_metadata_t standard_metadata) {

    action drop() {
        meta.drop = 1;
        mark_to_drop(standard_metadata);
    }

    action set_egress(PortId_t port) {
        meta.egress_port = port;
        standard_metadata.egress_spec = port;
        meta.admitted = 1;
    }

    table circuit_table {
        key = {
            hdr.scf.namespace_id : exact;
            hdr.scf.circuit_id   : exact;
        }
        actions = {
            set_egress;
            drop;
        }
        size = 65536;
        default_action = drop();
    }

    apply {
        if (hdr.scf.isValid()) {
            circuit_table.apply();
            if (meta.admitted == 0) {
                drop();
            }
        } else {
            drop();
        }
    }
}

control SCFEgress(inout headers_t hdr,
                  inout metadata_t meta,
                  inout standard_metadata_t standard_metadata) {
    apply { /* metering / accounting metadata emission hooks */ }
}

control SCFComputeChecksum(inout headers_t hdr, inout metadata_t meta) {
    apply { }
}

control SCFDeparser(packet_out packet, in headers_t hdr) {
    apply {
        packet.emit(hdr.scf);
    }
}

V1Switch(
    SCFParser(),
    SCFVerifyChecksum(),
    SCFIngress(),
    SCFEgress(),
    SCFComputeChecksum(),
    SCFDeparser()
) main;
