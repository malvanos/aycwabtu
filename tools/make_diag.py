#!/usr/bin/env python3
"""Generate tools/aycwabtu_diag.cl from src/aycwabtu.cl.

The diagnostic kernel reuses the real kernel's __constant tables and static
cipher helpers (stream + block, incl. the A1/A2 fusions) so that its
per-dispatch register/lookup profile faithfully mirrors the production kernel
`aycwabtu_search`.  It differs only in the main __kernel: instead of an
atomic "found" write it has EVERY work-item write a per-gid checksum of its
own computation.  The host differential harness then runs the same total
work-item count two ways -- G threadgroups in ONE dispatch vs. G sequential
1-threadgroup dispatches -- and compares the per-gid checksums.  A mismatch
proves cl2Metal is corrupting work-item state at that per-dispatch
threadgroup count (the M2 bug documented in PLAN.MD); a match proves it
clean so the dispatch ceiling can be raised.
"""
import os, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "aycwabtu.cl"
OUT = ROOT / "tools" / "aycwabtu_diag.cl"
MARKER = "__kernel void aycwabtu_search"

DIAG_KERNEL = r'''
/* ================================================================
   DIAGNOSTIC kernel - per-gid differential harness.
   Generated from aycwabtu.{cl,hpp}; DO NOT EDIT (run tools/make_diag.py).

   Every work-item writes a checksum of its own computation to
   checksum[ gid ].  The checksum is a non-gated fold over the inner key
   loop (build_cw + key_schedule_block + quick_check_pes), so it is
   deterministic and non-zero for every work-item even when no key is
   found.  This mirrors the production kernel's per-dispatch load closely
   enough to reproduce the cl2Metal per-dispatch threadgroup-corruption
   bug, while giving the host a per-gid observable to compare
   clean (sequential) vs. single big-dispatch geometries.
================================================================ */
__kernel void aycwabtu_diag(
     __global const u8 *probedata,    /* 3 x 16 bytes of encrypted TS      */
    __global u32       *checksum,     /* one 32-bit checksum per gid        */
    u32 key_start,
    u32 base,/* gid offset into checksum[] and key space */
    u32 inner_start,
    u32 inner_count)
{
    u32 gid       = base + get_global_id(0);
    u32 outer_key = key_start + gid;

    u8  cw[8], cws[8], kk[56];
    u32 inner, inner_end;
    u32 h = 0xDEAD0001u ^ (gid * 2654435769u);

    inner_end = inner_start + inner_count;
    for (inner = inner_start; inner < inner_end; inner++) {
        build_cw(outer_key, (u16)inner, cw, cws);
        key_schedule_block(cw, kk);

        /* Exercise the same 56-round block + trimmed stream path the
           production kernel pays, then fold its observables so the
           checksum is sensitive to state corruption. */
        h ^= quick_check_pes(cws, kk, probedata) ? 0x9E3779B9u : 0x00000001u;

        h = h * 1664525u + 1013904223u;
        h ^= h << 13;
        h ^= (((u32)cw[0])       )
            ^ (((u32)cw[2]) <<  8)
            ^ (((u32)cw[4]) << 16)
            ^ (((u32)cw[6]) << 24)
            ^ (((u32)cws[1]) <<  4)
            ^ (((u32)cws[3]) << 12)
            ^ (((u32)cws[5]) << 20)
            ^ ((u32)kk[3] ^ kk[20] ^ kk[55]);
    }

    checksum[gid] = h;
}
'''

def main():
    src = SRC.read_text()
    idx = src.find(MARKER)
    if idx < 0:
        sys.exit(f"marker not found in {SRC}: {MARKER!r}")
    prefix = src[:idx].rstrip() + "\n\n"
    header = ("// Generated diagnostic kernel (tools/make_diag.py) - do not edit.\n"
               "// Per-gid differential harness; reuses real tables + helpers.\n\n")
    OUT.write_text(header + prefix + DIAG_KERNEL)
    print(f"wrote {OUT.relative_to(ROOT)} "
          f"({len(prefix)} prefix bytes + {len(DIAG_KERNEL)} bytes diag kernel)")

if __name__ == "__main__":
    main()
