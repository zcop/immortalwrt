#!/usr/bin/env python3
"""Offline acceptance tests for yt921x_capture.c (no host libpcap dev needed)."""
import argparse
import json
import os
import struct
import subprocess
import tempfile
from pathlib import Path

PH = struct.Struct("<IHHiIII")
MAGIC = 0xA1B2C3D4


def run(binary, *args):
    result = subprocess.run([binary, *map(str, args)], capture_output=True, text=True)
    assert not any(marker in result.stderr for marker in
                   ("AddressSanitizer", "LeakSanitizer", "runtime error:")), result.stderr
    return result


def finish(p):
    assert p.stdout.strip(), (p.returncode, p.stderr)
    return json.loads(p.stdout.strip().splitlines()[-1])


def ok(p):
    assert p.returncode == 0, (p.returncode, p.stderr, p.stdout)
    return finish(p)


def write_pcap(path, dlt, recs, snap=65535):
    with open(path, "wb") as f:
        f.write(PH.pack(MAGIC, 2, 4, 0, 0, snap, dlt))
        for (sec, usec), data, orig in recs:
            f.write(struct.pack("<IIII", sec, usec, len(data),
                                len(data) if orig is None else orig))
            f.write(data)


def read_pcap(path):
    b = Path(path).read_bytes()
    magic, _, _, _, _, _, dlt = PH.unpack_from(b)
    assert magic == MAGIC
    out, off = [], 24
    while off < len(b):
        sec, usec, cap, orig = struct.unpack_from("<IIII", b, off)
        off += 16
        out.append(((sec, usec), b[off:off + cap], orig))
        off += cap
    return dlt, out


def eth(payload, et=0x0800):
    return bytes.fromhex("020000000001020000000002") + struct.pack("!H", et) + payload


def tagged(frame, port=0, code=0, prio=0, mask=1, rx_ok=True, tx_ok=True,
           code_en=True, tpid=0x9988):
    rx = ((0x8000 if rx_ok else 0) | ((port & 15) << 11) | ((prio & 7) << 8) |
          (0x80 if code_en else 0) | ((code & 63) << 1))
    tx = (0x8000 if tx_ok else 0) | (mask & 0x7FF)
    return frame[:12] + struct.pack("!HHHH", tpid, 0, rx, tx) + frame[12:]


def jsonl(prefix):
    return [json.loads(x) for x in
            Path(str(prefix) + ".jsonl").read_text().splitlines()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary",
                    default=str(Path(__file__).with_name("yt921x_capture")))
    b = ap.parse_args().binary
    ip, arp = eth(b"\x45" + bytes(19)), eth(b"\x00" * 28, 0x0806)
    mac = bytes.fromhex("020000000001040000000002")
    vlan = mac + struct.pack("!HHH", 0x8100, 0x64, 0x0800) + b"\x45" + bytes(19)
    qinq = (mac + struct.pack("!HHHH", 0x88A8, 0x64, 0x8100, 0xC8) +
            struct.pack("!H", 0x0800) + b"\x45" + bytes(19))

    with tempfile.TemporaryDirectory() as td:
        t = Path(td)
        out = t / "outputs"
        out.mkdir()
        inp, pre = t / "in.pcap", out / "raw"
        ts1, ts2 = (100, 101), (102, 103)
        write_pcap(inp, 1, [(ts1, vlan, None), (ts2, qinq, None)])
        j = ok(run(b, "--mode", "raw", "--read", inp, "--prefix", pre))
        assert (j["matched"], j["seen"], j["malformed"], j["dropped"],
                j["timeout"]) == (2, 2, 0, 0, False)
        for suffix in (".raw.pcap", ".pcap"):
            dlt, recs = read_pcap(str(pre) + suffix)
            assert dlt == 1 and [r[0] for r in recs] == [ts1, ts2]
            assert [r[1] for r in recs] == [vlan, qinq]
            assert [r[2] for r in recs] == [len(vlan), len(qinq)]
        recs = jsonl(pre)
        assert [r["normalized_len"] for r in recs] == [len(vlan), len(qinq)]
        assert all(r["packet_type"] == "offline" and not r["aux_present"]
                   for r in recs)
        assert all((os.stat(str(pre) + s).st_mode & 0o777) == 0o600
                   for s in (".raw.pcap", ".pcap", ".jsonl"))

        p9988 = eth(b"\xde\xad" * 10, 0x9988)
        write_pcap(t / "r2.pcap", 1, [((1, 1), p9988, None)])
        ok(run(b, "--mode", "raw", "--read", t / "r2.pcap", "--prefix", out / "r2"))
        assert read_pcap(str(out / "r2") + ".pcap")[1][0][1] == p9988
        j = ok(run(b, "--mode", "qinq", "--read", inp, "--prefix", out / "qf",
                   "--filter", "vlan or ether proto 0x88a8"))
        assert j["matched"] == 2
        j = ok(run(b, "--mode", "qinq", "--read", inp, "--prefix", out / "qn",
                   "--filter", "arp"))
        assert (j["matched"], j["seen"]) == (0, 2)

        e0 = tagged(ip, port=0, code=0x18, prio=5, mask=0x5)
        e2 = tagged(arp, port=2, code=0x2A, prio=2, mask=0, tx_ok=False)
        inp3, pre3 = t / "enh.pcap", out / "enh"
        write_pcap(inp3, 303, [((7, 8), e0, None), ((9, 10), e2, None)])
        j = ok(run(b, "--mode", "enhanced", "--read", inp3, "--prefix", pre3))
        assert (j["matched"], j["seen"], j["malformed"]) == (2, 2, 0)
        dlt, rr = read_pcap(str(pre3) + ".raw.pcap")
        assert dlt == 303 and [r[1] for r in rr] == [e0, e2]
        dlt, nr = read_pcap(str(pre3) + ".pcap")
        assert dlt == 1
        assert [r[1] for r in nr] == [e0[:12] + e0[20:], e2[:12] + e2[20:]]
        assert [r[2] for r in nr] == [len(e0) - 8, len(e2) - 8]
        rs = jsonl(pre3)
        assert [r["tag"] for r in rs] == [e0[12:20].hex(), e2[12:20].hex()]
        assert [r["rx_port"] for r in rs] == [0, 2]
        assert [r["rx_valid"] for r in rs] == [True, True]
        assert [r["tx_mask"] for r in rs] == [5, 0]
        assert [r["tx_valid"] for r in rs] == [True, False]
        assert [r["code"] for r in rs] == [0x18, 0x2A]
        assert [r["priority"] for r in rs] == [5, 2]
        j = ok(run(b, "--mode", "enhanced", "--read", inp3, "--prefix", out / "ef",
                   "--filter", "ip"))
        assert j["matched"] == 1 and jsonl(out / "ef")[0]["rx_port"] == 0
        j = ok(run(b, "--mode", "enhanced", "--read", inp3, "--prefix", out / "ef2",
                   "--filter", "arp"))
        assert j["matched"] == 1 and jsonl(out / "ef2")[0]["rx_port"] == 2
        j = ok(run(b, "--mode", "enhanced", "--read", inp3, "--prefix", out / "ec",
                   "--count", "1"))
        assert (j["matched"], j["seen"]) == (1, 1) and len(jsonl(out / "ec")) == 1

        trunc = e0[:22]
        write_pcap(t / "tr.pcap", 303, [((1, 1), trunc, 100)])
        j = ok(run(b, "--mode", "enhanced", "--read", t / "tr.pcap",
                   "--prefix", out / "tr"))
        assert (j["matched"], j["malformed"]) == (1, 0)
        assert read_pcap(str(out / "tr") + ".raw.pcap")[1][0] == ((1, 1), trunc, 100)
        assert read_pcap(str(out / "tr") + ".pcap")[1][0] == (
            (1, 1), trunc[:12] + trunc[20:], 92)
        assert jsonl(out / "tr")[0]["normalized_len"] == 14

        bad = bytearray(e0)
        bad[12:14] = b"\x88\xa8"
        write_pcap(t / "bad.pcap", 303, [((1, 1), bytes(bad), None)])
        p = run(b, "--mode", "enhanced", "--read", t / "bad.pcap",
                "--prefix", out / "bad")
        assert p.returncode != 0 and finish(p)["malformed"] == 1
        write_pcap(t / "short.pcap", 303, [((1, 1), e0[:21], None)])
        assert run(b, "--mode", "enhanced", "--read", t / "short.pcap",
                   "--prefix", out / "short").returncode != 0
        write_pcap(t / "sr.pcap", 1, [((1, 1), b"x" * 13, None)])
        assert run(b, "--mode", "raw", "--read", t / "sr.pcap",
                   "--prefix", out / "sr").returncode != 0
        write_pcap(t / "big.pcap", 303,
                   [((1, 1), e0 + bytes(65536), None)], snap=262144)
        assert run(b, "--mode", "enhanced", "--read", t / "big.pcap",
                   "--prefix", out / "big").returncode != 0
        write_pcap(t / "bl.pcap", 1, [((1, 1), vlan, len(vlan) - 1)])
        assert run(b, "--mode", "raw", "--read", t / "bl.pcap",
                   "--prefix", out / "bl").returncode != 0
        assert run(b, "--mode", "enhanced", "--read", inp,
                   "--prefix", out / "dlt").returncode != 0

        for args in (["--mode", "raw", "--prefix", out / "x"],
                     ["--mode", "raw", "--prefix", out / "x", "--iface", "lo",
                      "--read", inp],
                     ["--mode", "raw", "--prefix", out / "x", "--read",
                      t / "missing"],
                     ["--mode", "raw", "--prefix", out / "x", "--read", inp,
                      "--timeout-ms", "10"],
                     ["--mode", "bogus", "--prefix", out / "x", "--read", inp]):
            assert run(b, *args).returncode != 0

        pre4 = out / "exists"
        Path(str(pre4) + ".pcap").write_bytes(b"keep")
        assert run(b, "--mode", "raw", "--read", inp,
                   "--prefix", pre4, "--filter", "arp").returncode != 0
        assert Path(str(pre4) + ".pcap").read_bytes() == b"keep"
        assert not Path(str(pre4) + ".raw.pcap").exists()
        assert not Path(str(pre4) + ".jsonl").exists()
    print("ok")


if __name__ == "__main__":
    main()
