// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

namespace Text::InlineTexture {

// Called from FrameScript_Initialize_h before the /reload teardown: the reload
// frees every fontstring and text node, so the node→owner map and the per-node
// icon records are dropped (they rebuild as the reloaded UI re-emits its text).
void PrepareForReload();

// Debug (for _classicapi_InlineTexFsDump): classify the fs → node → records
// chain for a fontstring whose live text has |T but whose queued want is empty.
//   0 = fs/block/node unreadable
//   1 = node not in the owner map (RebuildString hook missed it)
//   2 = node mapped to a DIFFERENT fs (owner-map corruption)
//   3 = authoritative, but no icon records (emitter never recorded this text)
//   4 = authoritative WITH records (flush-side gate dropped the queue: rect
//       validity, K, or suppression)
//   5 = blockless, rebuild PENDING (dirty bit set — engine will rebuild)
//   6 = blockless and STUCK (dirty bit clear — the flush nudges these)
int DebugChainState(void *fs);

// Debug: the fs's current text node's flags word (+0x5C), or 0xFFFFFFFF when
// the chain is unreadable. Bit 6 (0x40) = editable (flush queues {} for these).
unsigned int DebugNodeFlags(void *fs);

// Debug: flush calls elapsed since the fs's CURRENT node was last walked by
// FlushLayout (-1 = never seen / chain unreadable). A large, growing age means
// the node's layout is not being painted — the flush can never queue for it.
int DebugNodeSeenAge(void *fs);

} // namespace Text::InlineTexture
