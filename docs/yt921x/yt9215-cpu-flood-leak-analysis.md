# YT921x L2 Offload CPU Port Leak Analysis

**Date:** June 16, 2026
**Target:** YT9215 Switch (e.g., CR881x)

## Problem Statement
When L2 bridge offloading is enabled on the YT921x switch, tests show that traffic between LAN ports (e.g., LAN1 to LAN2) still partially leaks to the CPU port.

## Root Cause Analysis
1. The flood filter registers (`YT921X_FILTER_UNK_UCAST`, `YT921X_FILTER_UNK_MCAST`, `YT921X_FILTER_MCAST`, `YT921X_FILTER_BCAST`) use **drop-mask logic**: `1` means drop/block, `0` means forward/allow.
2. In the driver (`yt921x_chip.c` and `yt921x_core.c`), the default base masks were hardcoded to `BIT(10)`.
3. Port 10 (`p10`) is the internal **MCU port**, not the **CPU port**. The CPU port is typically `p8` or `p4` (defined dynamically in `priv->cpu_ports_mask`).
4. Because the CPU port bit was `0` in these masks, the hardware switch was permitted to flood all Unknown Unicast, Unknown Multicast, and Broadcast traffic directly up the CPU conduit, completely bypassing the intended bridge offload isolation and causing the observed leak.

## Proposed Fix & Architectural Constraints

To fix the leak, we must include the CPU port(s) in the drop masks (`priv->cpu_ports_mask`). However, we must be very careful about *which* traffic types we block from reaching the CPU.

### 1. Unknown Unicast
*   **Action:** SAFE TO BLOCK
*   **Reason:** The CPU does not need to see unknown unicast traffic destined for other MAC addresses. Reducing this noise is beneficial.

### 2. Unknown Multicast
*   **Action:** SAFE TO BLOCK
*   **Reason:** Normally, blocking multicast would break control-plane protocols like IGMP/MLD and Spanning Tree (STP/BPDU). However, the YT921x hardware has dedicated bypasses:
    *   **IGMP/MLD:** `YT921X_ACT_UNK_MCAST_BYPASS_DROP_IGMP` is explicitly set in the driver. This ensures IGMP traffic is trapped to the CPU regardless of the drop mask.
    *   **STP/LLDP/LACP:** `YT921X_RMA_CTRLn` registers are programmed to trap specific Reserved Multicast Addresses (RMAs) to the CPU.
    *   Therefore, it is safe to set the CPU port bit in the Multicast drop masks.

### 3. Broadcast
*   **Action:** **UNSAFE TO BLOCK** (Do NOT include CPU port in the drop mask)
*   **Reason:** IPv4 ARP relies on standard data-plane broadcasts. The switch does not have a dedicated "ARP bypass" mechanism. If we block Broadcast traffic from reaching the CPU, the router will no longer receive ARP requests and will lose the ability to resolve IPs on the local LAN, effectively breaking routing.

## Required Code Changes (Pending)

1.  **`yt921x_chip.c` (`yt921x_chip_setup_dsa`)**:
    *   Update `priv->flood_unk_ucast_base_mask` to `priv->cpu_ports_mask | BIT(10)`.
    *   Update `priv->flood_mcast_base_mask` to `priv->cpu_ports_mask | BIT(10)`.
    *   **Keep** `priv->flood_bcast_base_mask` as `BIT(10)` (allow CPU to receive broadcasts).

2.  **`yt921x_core.c` (`yt921x_refresh_flood_masks_locked`)**:
    *   Update local `unk_ucast_mask` to `priv->cpu_ports_mask | BIT(10)`.
    *   Update local `mcast_mask` to `priv->cpu_ports_mask | BIT(10)`.
    *   **Keep** local `bcast_mask` as `BIT(10)`.

3.  **`yt921x_l2.c` (`yt921x_sync_mrouter_masks_locked`)**:
    *   Update the `YT921X_FILTER_UNK_MCAST` drop mask calculation to include `priv->cpu_ports_mask` alongside `BIT(10)`, so the dynamic mrouter sync maintains the CPU isolation.

## Additional Hypotheses & Areas to Monitor

If LAN-to-LAN traffic continues to appear on the CPU port even after correcting the flood masks, it implies an intentional (even if incorrect) routing decision by the switch's internal logic matrix. Other potential causes to investigate include:

1.  **FDB Misses:** Traffic for MAC addresses that have aged out or haven't been learned yet will hit the Unknown Unicast/Multicast rules. If those rules aren't strict enough or bypasses exist, it floods to the CPU.
    *   *Git History Note:* Recent commits by `zcop` (specifically `cc1ce14e79`) made FDB timeout recovery **non-destructive**. The driver no longer performs "hard flushes" of the dynamic FDB table on timeout. Therefore, unless a physical link is flapping (`YT921X_FDB_HW_FLUSH_ON_LINKDOWN`) or STP state is changing, continuous FDB flooding is unlikely to be the cause of a continuous leak. If the leak is continuous, investigate traps or isolation overrides.
2.  **Multicast/Broadcast Traps:** Ensure that control plane traps (e.g., ARP, IPv6 Neighbor Discovery, IGMP/MLD, STP) are not overly broad and inadvertently capturing regular data plane traffic.
3.  **Port Matrix Overrides:** A register (such as the `0x180294` Port Isolation gates) might be persistently tagging the CPU port as an allowed destination for specific flows, overriding the flood mask restrictions.
