// Copyright 2026 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//go:build arm && !purego

package poly1305

import (
	"encoding/binary"
	"math/bits"
)

// updateGeneric on 32-bit ARM uses five 26-bit limbs (poly1305-donna-32):
// every product is one 32x32->64 multiply, where updateGeneric64 needs
// bits.Mul64, which takes four multiplies on a 32-bit CPU. The state stays
// in macState's 64-bit form between calls.
func updateGeneric(state *macState, msg []byte) {
	const mask = 1<<26 - 1

	t0, t1 := state.r[0], state.r[1]
	r0 := uint32(t0) & mask
	r1 := uint32(t0>>26) & mask
	r2 := uint32(t0>>52|t1<<12) & mask
	r3 := uint32(t1>>14) & mask
	r4 := uint32(t1>>40) & mask
	s1, s2, s3, s4 := r1*5, r2*5, r3*5, r4*5

	h := state.h
	h0 := uint32(h[0]) & mask
	h1 := uint32(h[0]>>26) & mask
	h2 := uint32(h[0]>>52|h[1]<<12) & mask
	h3 := uint32(h[1]>>14) & mask
	h4 := uint32(h[1]>>40 | h[2]<<24)

	var block [TagSize]byte
	for len(msg) > 0 {
		var hibit uint32 = 1 << 24
		m := msg
		if len(msg) >= TagSize {
			msg = msg[TagSize:]
		} else {
			block = [TagSize]byte{}
			copy(block[:], msg)
			block[len(msg)] = 1
			m = block[:]
			hibit = 0
			msg = nil
		}

		h0 += binary.LittleEndian.Uint32(m[0:]) & mask
		h1 += binary.LittleEndian.Uint32(m[3:]) >> 2 & mask
		h2 += binary.LittleEndian.Uint32(m[6:]) >> 4 & mask
		h3 += binary.LittleEndian.Uint32(m[9:]) >> 6 & mask
		h4 += binary.LittleEndian.Uint32(m[12:])>>8 | hibit

		d0 := uint64(h0)*uint64(r0) + uint64(h1)*uint64(s4) + uint64(h2)*uint64(s3) + uint64(h3)*uint64(s2) + uint64(h4)*uint64(s1)
		d1 := uint64(h0)*uint64(r1) + uint64(h1)*uint64(r0) + uint64(h2)*uint64(s4) + uint64(h3)*uint64(s3) + uint64(h4)*uint64(s2)
		d2 := uint64(h0)*uint64(r2) + uint64(h1)*uint64(r1) + uint64(h2)*uint64(r0) + uint64(h3)*uint64(s4) + uint64(h4)*uint64(s3)
		d3 := uint64(h0)*uint64(r3) + uint64(h1)*uint64(r2) + uint64(h2)*uint64(r1) + uint64(h3)*uint64(r0) + uint64(h4)*uint64(s4)
		d4 := uint64(h0)*uint64(r4) + uint64(h1)*uint64(r3) + uint64(h2)*uint64(r2) + uint64(h3)*uint64(r1) + uint64(h4)*uint64(r0)

		d1 += d0 >> 26
		h0 = uint32(d0) & mask
		d2 += d1 >> 26
		h1 = uint32(d1) & mask
		d3 += d2 >> 26
		h2 = uint32(d2) & mask
		d4 += d3 >> 26
		h3 = uint32(d3) & mask
		c := uint32(d4 >> 26)
		h4 = uint32(d4) & mask
		h0 += c * 5
		h1 += h0 >> 26
		h0 &= mask
	}

	// Back to h[0] + h[1]<<64 + h[2]<<128.
	lo, c := bits.Add64(uint64(h0)+uint64(h1)<<26, uint64(h2)<<52, 0)
	mid, c2 := bits.Add64(uint64(h2)>>12+uint64(h3)<<14, uint64(h4)<<40, c)
	state.h = [3]uint64{lo, mid, uint64(h4)>>24 + c2}
}
