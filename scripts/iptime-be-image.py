#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
Image helper for ipTIME BE-series (Airoha AN7563, EcoNet "ECNT" U-Boot).

The stock U-Boot boots a firmware slot (tclinux / tclinux_slave) laid out as

  0x0000  TF-A FIP table of contents (certificate entries are not checked)
  0x2000  "HDR2" header, 0x100 bytes
  0x2100  FIT with in-tree data: /images/fdt@1, kernel@1, filesystem@1 and
          /configurations/conf@1, in exactly this order. U-Boot reads the
          FIT 0x800 bytes at a time (start, end of fdt data, end of kernel
          data) and looks the nodes up by these fixed names.

OpenWrt puts its squashfs after the FIT on an erase block boundary, where
mtdsplit_fit (openwrt,fit-offset = <0x2100>) finds it, so filesystem@1
only holds a small placeholder.

  kernel <lzma kernel> <dtb> <loadaddr> <out>   build the slot head
  webhdr <in> <out> <model> <version>           prepend the stock web-UI
                                                upgrade header
"""
import hashlib
import struct
import sys
import time
import zlib

FIT_OFFSET = 0x2100
FS_PLACEHOLDER = 0x1000


def crc32_inv(data):
    return (zlib.crc32(data) & 0xffffffff) ^ 0xffffffff


class Node:
    def __init__(self, name, props=(), kids=()):
        self.name, self.props, self.kids = name, list(props), list(kids)


def s(v):
    return v.encode() + b"\0"


def u32(v):
    return struct.pack(">I", v)


def fdt_serialize(root):
    strs, soff, st = bytearray(), {}, bytearray()

    def pad():
        st.extend(b"\0" * (-len(st) % 4))

    def emit(n):
        st.extend(u32(1) + s(n.name)); pad()
        for k, v in n.props:
            if k not in soff:
                soff[k] = len(strs); strs.extend(s(k))
            st.extend(u32(3) + u32(len(v)) + u32(soff[k]) + v); pad()
        for c in n.kids:
            emit(c)
        st.extend(u32(2))

    emit(root); st.extend(u32(9))
    off_rsv = 40; rsv = b"\0" * 16
    off_st = off_rsv + len(rsv); off_str = off_st + len(st)
    total = off_str + len(strs)
    hdr = struct.pack(">10I", 0xd00dfeed, total, off_st, off_str, off_rsv,
                      17, 16, 0, len(strs), len(st))
    return hdr + rsv + bytes(st) + bytes(strs)


def build_fit(kernel, dtb, loadaddr):
    fs = b"\0" * FS_PLACEHOLDER
    root = Node("", [("timestamp", u32(int(time.time()))),
                     ("description", s("Kernel image with single Linux kernel, rootfs and FDT blob")),
                     ("#address-cells", u32(1))], [
        Node("images", [], [
            Node("fdt@1", [("description", s("OpenWrt device tree")),
                           ("data", dtb), ("type", s("flat_dt")),
                           ("arch", s("arm")), ("compression", s("none"))]),
            Node("kernel@1", [("description", s("OpenWrt Linux kernel")),
                              ("data", kernel), ("type", s("kernel")),
                              ("arch", s("arm")), ("os", s("linux")),
                              ("compression", s("lzma")),
                              ("load", u32(loadaddr)), ("entry", u32(loadaddr))],
                 [Node("hash@1", [("value", hashlib.sha1(kernel).digest()),
                                  ("algo", s("sha1"))])]),
            Node("filesystem@1", [("description", s("placeholder, rootfs follows the FIT")),
                                  ("data", fs), ("type", s("filesystem")),
                                  ("arch", s("arm")), ("os", s("linux")),
                                  ("compression", s("none"))]),
        ]),
        Node("configurations", [("default", s("conf@1"))], [
            Node("conf@1", [("description", s("Boot Linux kernel with FDT blob")),
                            ("fdt", s("fdt@1")), ("kernel", s("kernel@1")),
                            ("filesystem", s("filesystem@1"))]),
        ]),
    ])
    return fdt_serialize(root), len(kernel), len(fs)


def check_uboot_reads(fit):
    """Mimic the stock parser: each node must be found in the bytes read."""
    def node(n):
        i = fit.find(u32(1) + s(n)); assert i > 0, n; return i

    def data_prop(n):
        i = node(n) + 4 + ((len(n) + 1 + 3) & ~3)      # first prop: description
        assert struct.unpack(">I", fit[i:i + 4])[0] == 3
        j = (i + 12 + struct.unpack(">I", fit[i + 4:i + 8])[0] + 3) & ~3
        assert struct.unpack(">I", fit[j:j + 4])[0] == 3  # second prop: data
        return j + 12, j + 12 + struct.unpack(">I", fit[j + 4:j + 8])[0]

    f0, f1 = data_prop("fdt@1"); k0, k1 = data_prop("kernel@1"); s0, _ = data_prop("filesystem@1")
    assert f0 <= 0x800, "fdt@1 not in the first 0x800 bytes"
    assert k0 <= f1 + 0x800, "kernel@1 too far after fdt@1"
    assert node("filesystem@1") >= k1 and s0 <= k1 + 0x800, "filesystem@1 too far after kernel@1"


# TF-A FIP: trusted key cert, non-trusted fw key cert, non-trusted fw content
# cert (left empty, U-Boot ignores them), then BL33 = HDR2 + FIT.
FIP_UUIDS = [bytes.fromhex(x) for x in (
    "827ee890f860e411a1b4777a21b4f94c",
    "8ad5832afb60e4118aafdf30bbc49859",
    "8ec4c1f35d63e411a7a987ee40b23fa7",
    "d6d0eea7fcead54b97829934f234b6e4")]
FIP_CERTS = [(0x400, 0xa0e), (0x1000, 0x7ea), (0x1800, 0x640)]


def build_slot_head(kernel, dtb, loadaddr):
    fit, ksize, fsize = build_fit(kernel, dtb, loadaddr)
    check_uboot_reads(fit)
    h2 = bytearray(0x100)
    h2[0:4] = b"HDR2"
    struct.pack_into("<IIi", h2, 4, 0x100, 0x100 + len(fit), 0)
    struct.pack_into("<I", h2, 0x0c, crc32_inv(fit))
    h2[0x10:0x18] = b"7.0.1.0\n"
    h2[0x30] = 0x0a
    struct.pack_into("<II", h2, 0x50, ksize, fsize)
    h2[0x5c:0x69] = b"3 6035 122 0\n"
    bl33 = bytes(h2) + fit
    toc = bytearray(0x2000)
    struct.pack_into("<IIQ", toc, 0, 0xaa640001, 0x12345678, 0)
    ents = FIP_CERTS + [(0x2000, len(bl33))]
    for i, (uuid, (off, size)) in enumerate(zip(FIP_UUIDS, ents)):
        o = 0x10 + i * 40
        toc[o:o + 16] = uuid
        struct.pack_into("<QQQ", toc, o + 16, off, size, 0)
    struct.pack_into("<QQQ", toc, 0x10 + 4 * 40 + 16,
                     (0x2000 + len(bl33) + 0xfff) & ~0xfff, 0, 0)
    return bytes(toc) + bl33


def webhdr(data, model, version):
    hdr = bytearray(0x38)
    hdr[0:len(model)] = model.encode()
    hdr[0x24:0x24 + len(version)] = version.encode()
    struct.pack_into("<I", hdr, 0x30, len(data))
    full = bytearray(bytes(hdr) + data)
    struct.pack_into("<I", full, 0x34, crc32_inv(bytes(full)))
    return bytes(full)


def main(argv):
    if len(argv) == 6 and argv[1] == "kernel":
        out = build_slot_head(open(argv[2], "rb").read(), open(argv[3], "rb").read(),
                              int(argv[4], 0))
        open(argv[5], "wb").write(out)
    elif len(argv) == 6 and argv[1] == "webhdr":
        open(argv[3], "wb").write(webhdr(open(argv[2], "rb").read(), argv[4], argv[5]))
    else:
        sys.stderr.write(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
