#!/usr/bin/env python3
"""Check ECM's PPPoE lookup and establishment with live and replaced devices."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from test_qinq_node_lifetime import extract_function

MOCK = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ETH_ALEN 6
#define ECM_DB_IFACE_TYPE_PPPOE 1
#define DEBUG_INFO(...) ((void)0)
#define DEBUG_TRACE(...) ((void)0)
#define DEBUG_WARN(...) ((void)0)

typedef int ecm_db_iface_hash_t;
struct ecm_db_interface_info_pppoe {
    uint16_t pppoe_session_id;
    uint8_t remote_mac[ETH_ALEN];
};
struct ecm_db_iface_instance {
    int type, interface_identifier, ae_interface_identifier, refs;
    struct { struct ecm_db_interface_info_pppoe pppoe; } type_info;
    struct ecm_db_iface_instance *hash_next;
};

static int ecm_db_lock, ecm_interface_lock;
static struct ecm_db_iface_instance *ecm_db_iface_table[1];
static struct ecm_db_iface_instance objects[9];
static int allocated;
static void spin_lock_bh(int *lock) { assert(!(*lock)++); }
static void spin_unlock_bh(int *lock) { assert((*lock)-- == 1); }
static int ecm_db_iface_generate_hash_index_pppoe(uint16_t sid) { (void)sid; return 0; }
static void _ecm_db_iface_ref(struct ecm_db_iface_instance *ii) { ++ii->refs; }
static void ecm_db_iface_deref(struct ecm_db_iface_instance *ii) { assert(ii->refs-- > 0); }
static struct ecm_db_iface_instance *ecm_db_iface_alloc(void)
{
    assert(allocated < 9);
    struct ecm_db_iface_instance *ii = &objects[allocated++];
    ii->refs = 1;
    return ii;
}

static void ecm_db_iface_add_pppoe(struct ecm_db_iface_instance *ii, uint16_t sid,
                                  uint8_t *mac, char *name, int mtu, int ifindex,
                                  int nss_if, void *final, void *arg)
{
    assert(ecm_interface_lock && name && mtu == 1492 && !final && arg == ii);
    ii->type = ECM_DB_IFACE_TYPE_PPPOE;
    ii->interface_identifier = ifindex;
    ii->ae_interface_identifier = nss_if;
    ii->type_info.pppoe.pppoe_session_id = sid;
    memcpy(ii->type_info.pppoe.remote_mac, mac, ETH_ALEN);
    ii->hash_next = ecm_db_iface_table[0];
    ecm_db_iface_table[0] = ii;
}
"""

CHECKS = r"""
int main(void)
{
    struct ecm_db_interface_info_pppoe session = {.remote_mac = {2, 0, 0, 0, 0, 9}};
    struct ecm_db_iface_instance *active[8];

    /* Each WAN's concentrator uses the same MAC and assigns SIDs 1 through 4. */
    for (int i = 0; i < 8; i++) {
        session.pppoe_session_id = i % 4 + 1;
        active[i] = ecm_interface_pppoe_interface_establish(&session, "pppoe", 20 + i, 100 + i, 1492);
        assert(active[i]->interface_identifier == 20 + i);
        assert(active[i]->ae_interface_identifier == 100 + i);
    }
    assert(allocated == 8);

    /* Reconnect one session using the same SID on a replacement PPP device. */
    session.pppoe_session_id = 1;
    struct ecm_db_iface_instance *replacement =
        ecm_interface_pppoe_interface_establish(&session, "pppoe", 28, 108, 1492);
    assert(replacement != active[0] && replacement->interface_identifier == 28);
    assert(replacement->ae_interface_identifier == 108);

    for (int i = 1; i < 8; i++) {
        session.pppoe_session_id = i % 4 + 1;
        struct ecm_db_iface_instance *found =
            ecm_interface_pppoe_interface_establish(&session, "pppoe", 20 + i, 100 + i, 1492);
        assert(found == active[i]);
        ecm_db_iface_deref(found);
    }
    assert(allocated == 9 && !ecm_db_lock && !ecm_interface_lock);
    puts("PASS: eight PPPoE interface records, duplicate SID/peer on both WANs and isolated reconnect");
    return 0;
}
"""


def main():
    root = Path(__file__).resolve().parents[2]
    default = root / "build_dir/target-aarch64_cortex-a53_musl/linux-qualcommax_ipq50xx/qca-nss-ecm-12.5.2024.11.06~30fbfa4"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=default)
    source = parser.parse_args().source
    lookup = extract_function((source / "ecm_db/ecm_db_iface.c").read_text(),
                              "ecm_db_iface_find_and_ref_pppoe")
    establish = extract_function((source / "ecm_interface.c").read_text(),
                                 "ecm_interface_pppoe_interface_establish")
    with tempfile.TemporaryDirectory(prefix="pppoe-ecm-identity-") as folder:
        work = Path(folder)
        (work / "test.c").write_text("\n".join((MOCK, lookup, establish, CHECKS)))
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(work / "test.c"), "-o", str(work / "test")], check=True)
        return subprocess.run([str(work / "test")]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
