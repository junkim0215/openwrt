// Copyright 2026 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//go:build !arm || purego

package poly1305

func updateGeneric(state *macState, msg []byte) { updateGeneric64(state, msg) }
