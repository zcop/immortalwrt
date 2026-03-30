# YT9215 egress-tagging probe: VTU (`0x188150/0x188154`) vs `PORTn_VLAN_CTRL1` (`0x230080`) (2026-03-29)

## Goal
Determine whether LAN egress tag/untag behavior is encoded in:
- per-port `PORTn_VLAN_CTRL1` (`0x230080 + 4*p`), or
- VLAN-table untag bitmap (`YT921X_VLANn_CTRL` word1).

## Method
UART-only runtime test (no `bridge` utility available):
- Baseline read:
  - `0x230080 + 4*p` for `p0..p10`
  - `VID42` VTU words: `0x188150`, `0x188154`
- Apply temporary VLAN configs via UCI + `network restart`:
  1. `VID42` with `lan1:t` (tagged)
  2. `VID42` with `lan1:u` (untagged, non-PVID)
  3. `VID42` with `lan1:u*` (untagged + PVID)
- Read the same registers after each state.

## Observations
- `0x230080 + 4*p` stayed `0x00000000` for all ports in all tested states.
- `VID42` VTU words:
  - Tagged (`lan1:t`): `0x188150 = 0x15008080`, `0x188154 = 0x00000000`
  - Untagged + PVID (`lan1:u*`): `0x188150 = 0x15008080`, `0x188154 = 0x00000100`
  - Untagged non-PVID (`lan1:u`): observed as all-zero VTU in this run
    (`0x188150=0`, `0x188154=0`) and treated as non-authoritative for untag decode.

## Conclusion
- Egress tag/untag semantics for this path are encoded in VTU `word1`
  (`0x188154`) via untag bitmap, not in `0x230080` per-port control words.
- `0x230080` remains a candidate for drop-policy controls, but showed no direct
  coupling to VLAN tagged/untagged membership in these tests.
