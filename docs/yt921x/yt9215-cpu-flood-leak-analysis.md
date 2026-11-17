# YT921x CPU delivery and multicast filters

The filter registers use destination **drop masks**: a set bit blocks the
port. Port 10 is the internal MCU destination; the host CPU ports come from
`priv->cpu_ports_mask` and are commonly ports 4 and 8 on CR881x.

| Register | Traffic controlled | Host CPU policy |
| --- | --- | --- |
| `YT921X_FILTER_UNK_UCAST` | Unknown unicast | Enhanced mode suppresses CPU flooding. |
| `YT921X_FILTER_UNK_MCAST` | Unknown multicast | Preserve flood flags and the static mrouter-port exemptions. |
| `YT921X_FILTER_MCAST` | General multicast, including known MDB hits | Allow CPU destinations selected by a host MDB entry; retain explicit storm blocks. |
| `YT921X_FILTER_BCAST` | Broadcast | Allow CPU delivery required by ARP and DHCP. |

The MCU bit stays blocked. `BR_MCAST_FLOOD` controls unknown multicast on
user ports; disabling it must not suppress a known offloaded MDB group.
The same distinction applies to the CPU flood-suppression bits in enhanced
mode. `yt921x_apply_flood_filters_locked()` removes both user-port policy bits
and CPU-port policy bits from the general multicast mask, then applies the
storm mask. The unknown multicast mask still uses the preserved base and
mrouter policy. Broadcast and unknown-unicast programming are unchanged.

## Host-joined multicast

Linux installs host MDB entries for groups it joins, including each local
IPv6 address's solicited-node multicast group. In a VLAN-unaware bridge,
legacy MDB entries use FID 4095. A VLAN-aware bridge uses its resolved VID.

For example, a host entry selecting CPU4 and CPU8 has destination mask
`0x110`. A general multicast drop mask of `0x510` blocks both destinations,
even though the MDB entry is correct. This prevents Neighbor Solicitations
from reaching the local IPv6 stack. The general filter must leave those
CPU bits clear; with the usual CR881x layout its baseline is `0x400`.
The implementation derives the CPU bits from the switch configuration.

This does not flood every multicast packet to Linux. A known group's MDB
entry selects its destinations, while unknown traffic still passes through
the unknown-multicast policy. IGMP/MLD and reserved control-frame traps have
separate bypass rules; they do not replace correct host MDB delivery.

Private tag_8021q/QinQ mode already omits the enhanced CPU-suppression bits
from its base mask, so this correction leaves its normal filter values
unchanged. MDB membership, FDB aging, VLAN domains and conduit assignment
remain responsible for their existing roles.

## Reproducible checks

`tools/nss/test_yt921x_mcast_filters.py` extracts the actual C function and
checks host MDB reachability, varied CPU layouts, flood flags, mrouter
exemptions, explicit storm blocks and register-error propagation. The
`--source` option supports an older source file as a negative control;
`--sanitize` adds address and undefined-behavior checks.

For hardware verification, use an isolated bridge and a wired peer. Enable
snooping and a querier, add a temporary IPv6 address, and inspect its
solicited-node MAC entry with `fdb lookup <mac> 4095` through `yt921x_cmd`.
Read `get_flood_filter` and compare the MDB CPU destinations with the
actual general drop mask. Clear only that peer neighbor entry before each
probe, or verify that a fresh target has no resolved MAC. Check IPv6 ping
and HTTP, then restore all temporary addresses and settings.

Exercise both conduits with their user ports linked. Check known IPv4 and
IPv6 UDP multicast groups for payload loss and duplicates, and separately
verify unknown-multicast suppression at the CPU conduits. Collect captures
at each layer and distinguish control-frame copies from duplicate delivery
to an application. Restore temporary masks after each check.
