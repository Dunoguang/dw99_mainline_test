#!/usr/bin/env python3
"""
Package kernel Image + DTB + ramdisk → SPRD boot.img (AOSP v0 header)
for the DW99 (SL8541e / SC9832e) smartwatch.

Re-implemented for dw99_mainline_test; all paths are parameterised via
command-line arguments or computed from the script's own location so the
script works from any working directory.
"""
import argparse, os, struct, sys, tempfile

# ── defaults tied to the kernel tree this script lives in ────────────────
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_TOOLS_DIR  = _SCRIPT_DIR
_KERNEL_DEF = 'arch/arm64/boot/Image'
_DTB_DEF    = 'arch/arm64/boot/dts/sprd/dw99.dtb'
_RAMDISK_DEF = os.path.join(_TOOLS_DIR, 'ramdisk.gz')
_HDR_DEF     = os.path.join(_TOOLS_DIR, 'sprd_header.bin')
_OUT_DEF     = 'boot.img'

# ── SPRD boot.img constants ─────────────────────────────────────────────
PAGE_SIZE    = 2048
KERNEL_ADDR  = 0x00008000
RAMDISK_ADDR = 0x05400000
SECOND_ADDR  = 0x00F00000
TAGS_ADDR    = 0x00000100
UNK2         = 0x12000000
ANDROID_MAGIC = b'ANDROID!'


def roundup(n: int, p: int) -> int:
    return ((n + p - 1) // p) * p


def wrap_dtb(raw_dtb: bytes, hdr_path: str) -> bytes:
    """Wrap a plain FDT with the SPRD header (sprd_header.bin)."""
    with open(hdr_path, 'rb') as f:
        hdr = bytearray(f.read())
    dtb_size = len(raw_dtb)
    struct.pack_into('<I', hdr, 0x1c, dtb_size)
    return bytes(hdr) + raw_dtb


def main() -> None:
    ap = argparse.ArgumentParser(description='Create DW99 boot.img')
    ap.add_argument('--kernel',   default=_KERNEL_DEF,
                    help=f'Kernel image path (default: {_KERNEL_DEF})')
    ap.add_argument('--dtb',      default=_DTB_DEF,
                    help=f'DTB path (default: {_DTB_DEF})')
    ap.add_argument('--ramdisk',  default=_RAMDISK_DEF,
                    help=f'Ramdisk path (default: {_RAMDISK_DEF})')
    ap.add_argument('--sprd-hdr', default=_HDR_DEF,
                    help=f'SPRD header binary (default: {_HDR_DEF})')
    ap.add_argument('--output', '-o', default=_OUT_DEF,
                    help=f'Output boot.img path (default: {_OUT_DEF})')
    args = ap.parse_args()

    # ── resolve paths relative to CWD if not absolute ───────────────
    kernel  = args.kernel
    dtb     = args.dtb
    ramdisk = args.ramdisk
    sprd_hdr = args.sprd_hdr
    out     = args.output

    # ── wrap DTB with SPRD header if it is a plain FDT ─────────────
    with open(dtb, 'rb') as f:
        magic = f.read(4)
    fdt_magic = b'\xd0\x0d\xfe\xed'
    if magic == fdt_magic:
        raw_dtb = open(dtb, 'rb').read()
        wrapped = wrap_dtb(raw_dtb, sprd_hdr)
        tmp = tempfile.NamedTemporaryFile(delete=False, suffix='.dtb')
        tmp.write(wrapped)
        tmp.close()
        dtb = tmp.name
        print(f'SPRD-wrapped DTB: {len(raw_dtb)} → {len(wrapped)} bytes')
    elif magic == b'\x12\x00\x00\x12':
        print(f'DTB seems already wrapped (magic={magic.hex()}), using as-is')
    else:
        print(f'DTB magic={magic.hex()}, assuming already wrapped')

    # ── sizes & offsets ─────────────────────────────────────────────
    kernel_sz  = os.path.getsize(kernel)
    ramdisk_sz = os.path.getsize(ramdisk)
    dt_sz      = os.path.getsize(dtb)

    k_ofs = PAGE_SIZE
    r_ofs = k_ofs + roundup(kernel_sz, PAGE_SIZE)
    d_ofs = r_ofs + roundup(ramdisk_sz, PAGE_SIZE)

    # ── build AOSP v0 header ────────────────────────────────────────
    hdr = bytearray(PAGE_SIZE)
    hdr[0:8] = ANDROID_MAGIC
    struct.pack_into('<I', hdr,  8, kernel_sz)
    struct.pack_into('<I', hdr, 12, KERNEL_ADDR)
    struct.pack_into('<I', hdr, 16, ramdisk_sz)
    struct.pack_into('<I', hdr, 20, RAMDISK_ADDR)
    struct.pack_into('<I', hdr, 24, 0)           # second_size
    struct.pack_into('<I', hdr, 28, SECOND_ADDR)
    struct.pack_into('<I', hdr, 32, TAGS_ADDR)
    struct.pack_into('<I', hdr, 36, PAGE_SIZE)
    struct.pack_into('<I', hdr, 40, dt_sz)       # SPRD dt_size field
    struct.pack_into('<I', hdr, 44, UNK2)         # SPRD unknown

    # ── write boot.img ──────────────────────────────────────────────
    with open(out, 'wb') as f:
        f.write(bytes(hdr))
        with open(kernel, 'rb') as kf:
            f.write(kf.read())
        f.write(b'\x00' * (r_ofs - f.tell()))
        with open(ramdisk, 'rb') as rf:
            f.write(rf.read())
        f.write(b'\x00' * (d_ofs - f.tell()))
        with open(dtb, 'rb') as df:
            f.write(df.read())

    sz = os.path.getsize(out)
    print(f'Created {out}: kernel={kernel_sz} ramdisk={ramdisk_sz} '
          f'dtb={dt_sz} → {sz} bytes ({sz/1024/1024:.1f} MB)')

    # clean up temp file
    if dtb != args.dtb:
        os.unlink(dtb)


if __name__ == '__main__':
    main()
