# CR881x 2.5G LAN CPU SerDes with NSS

## Source change

The board uses the native GMAC/UNIPHY PCS stack on kernel 6.18.55. The
experimental image enables the LAN CPU trunk at 2500 Mbit/s while retaining
the existing 1G WAN CPU path and the YT921x single-802.1Q NSS transport.

| Physical path | Current DTS | Link rate |
| --- | --- | ---: |
| LAN switch port 8 -> GMAC1 | `gmac1`, UNIPHY PCS, `2500base-x` | 2500 Mbit/s |
| WAN switch port 4 -> GMAC0 | Existing internal/GE PHY path | 1000 Mbit/s |

Historical commit `f03fff4fb1` enabled a working 2.5G SerDes profile in the old
ESS/`dp1`/`dp2` stack. Commit `4a79e00da6` returned that DTS to 1G for
compatibility/stability. Neither old DTS patch applies directly to today's
GMAC nodes. The current change updates `gmac1` and switch port 8 only; it
does not restore ESS mode properties, the old `dp2` node or legacy PHY bindings.

Both fixed links select `2500base-x`, speed 2500 and full duplex. Port 8 uses
the explicit `motorcomm,serdes-mode = "2500base-x"` override in place of
`motorcomm,serdes-revsgmii`, which the current driver rejects for 2500BASEX.
The explicit mode also avoids the driver's missing-property error path.
The inherited `pcs-handle` already connects GMAC1 to UNIPHY0. Native drivers
provide the SGMII+ configuration and 312.5 MHz RX/TX clocks.

The old profile's disabled pause/asymmetric-pause settings are retained for
the 2.5G trunk; their removal alone is not established as a necessary fix on
the current stack. WAN pause settings are unchanged. The existing fixed-PHY
2500 support and CPU trap queue-shaper removal were already present.

## Image and recovery

The full image build passed after rerunning packaging outside the sandbox;
the first attempt failed because fakeroot could not create its local socket.
The image's embedded DT, eleven modules, firmware, manual controller and
activation defaults passed inspection.

Sysupgrade SHA256:
`13bec869ac6256b8a273dc0019a941919a141c7cd91cc778b5861bedc120ac3a`.

Export: `bin/targets/qualcommax/ipq50xx/nss-serdes-2500-20261102/`.
Evidence: `logs/nss-serdes-2500-20261102/`.

NSS was stopped before booting the verified default-mode slot 0. Normal
sysupgrade then wrote only experimental slot 1. Both slots, configuration and
boot environment were backed up. The fallback slot and saved network,
firewall, wireless, DHCP and system settings were hash-verified unchanged.
UART capture is retained with the evidence. NSS startup remains manual.

## Validation

The new image booted with host Ethernet at 2500 Mbit/s, both host Wi-Fi PHYs
present, working management and WAN connectivity, and no NSS autoload.
With NSS active, switch register `0x8008c` reads `0x0000021c`, and the native
GMAC1/UNIPHY RX and TX clocks read 312500000 Hz.

The traffic and same-boot NSS restart tests passed:

| Test | LAN -> WAN | WAN -> LAN |
| --- | ---: | ---: |
| TCP, single stream, 30 seconds | 939.02 Mbit/s | 938.79 Mbit/s |
| TCP, single stream, 180 seconds | 939.02 Mbit/s | 938.76 Mbit/s |
| UDP, 900 Mbit/s target, 10 seconds | 899.87 Mbit/s | 899.32 Mbit/s |
| UDP reported loss | 0% | 0% |
| TCP after NSS restart, 10 seconds | 939.10 Mbit/s | 939.00 Mbit/s |

The two 180-second TCP runs increased firmware special IPv4 RX/TX counters
by 15,466,722 and 15,345,963 packets respectively. Both NSS physical
interfaces remained started, and no nft flowtable was enabled.

There were 263 link samples over 546.33 seconds. Carrier, up and down counters
stayed unchanged on both CPU conduits, LAN3 and WAN throughout the traffic
window. Both GMACs had zero increase in the checked hardware MAC error
counters: CRC, alignment, runt, jabber, length, FIFO overflow, RX watchdog/error,
TX underflow and carrier error. No unexpected link transition or fatal kernel
signature appeared. The SerDes register remained `0x21c`.

Router CPU was about 96% idle with the additional two-second carrier polling.
That extra diagnostic workload differs from the initial 1G comparison runs;
these CPU figures do not isolate the effect of changing the SerDes rate.

A controlled `cr881x-nss restart` passed in the same kernel boot, retaining
both host PHYs and restoring DSA user-port states. Its deliberate conduit
close/open is outside the no-flap traffic window. Final installed module,
controller and firmware hashes match the inspected image; configuration,
fallback slot 0 and management access remain intact. NSS is active manually.
Continue to run `cr881x-nss stop` before an ordinary system reboot.

The current LAN client and WAN port are each 1G. This topology can establish
correct operation at the faster internal SerDes rate, but cannot measure the
full aggregate capacity of a 2.5G trunk. This is a bounded nine-minute monitored
test, including six minutes of sustained TCP, rather than a long-duration or
cold-power-cycle qualification. The board DTS rate change also applies
to builds using the original YT tagger; the current new hardware tests use
the experimental 802.1Q mode. QinQ and Wi-Fi acceleration remain separate work.
