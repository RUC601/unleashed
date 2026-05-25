# Decrypt Chain Verification

Source of truth: `../uc_extract/current_state.md` (UC pages 320-330, newest
posts win). The prompt path `uc_extract/current_state.md` was not present under
this repo; the local authoritative copy is one directory above the repo.

## Cross-Check Summary

| Chain item | UC current_state | Code locations | Status |
| --- | --- | --- | --- |
| Component bitmap base | `parent+0x110` | `DecryptComponent` | OK |
| Component index byte base | `parent+0x130` | `DecryptComponent` | OK |
| Component table pointer | `parent+0x80` | `DecryptComponent` | OK |
| Component bucket | `componentId >> 6` | `sub_E8D1A0` | Fixed from `/ 0x3F` to `>> 6` |
| ComponentXorQword | `0x3A86E30`, then `+0x10C` | `Offsets.hpp`, `Decrypt.hpp`, `dma_verify_constants.cpp` | OK |
| ComponentXorByte | `0x3772769` | `Offsets.hpp`, `Decrypt.hpp`, `dma_verify_constants.cpp` | OK |
| Component transform | `+Add1`, `^key qword`, `+Add2`, `^byte`, `^Xor1`, `^Xor2`, net `ROR64(3)` | `Decrypt.hpp`, `decrypt_emu.cpp`, `dma_verify_constants.cpp` | OK |
| Visibility input | `VisBase+0x98`, `ROR64(3)`, `^0x53DB07B6B873760C` | `DecryptVis` | OK |
| Visibility main transform | `(byte ^ (enc - 0x7A7DB4DE6CD03BBC)) + 0x5CE60F50EA1D337F` | `DecryptVis` | OK |
| Visibility final order | `ROR64(dec+0x78D75198F1D34D38, 0xC)`, then qword mix at `+0x6A` | `DecryptVis` | Fixed order to match UC p330 |
| Visibility direct key note | `GlobalKeyPtr=0x3B76970`, `VisQwordOffset=0x1B1` | `Offsets.hpp`, `dma_verify_constants.cpp` | Documented/verified as optional path |
| GetParent | Existing May 2026 rotate/xor/sub/rotate/add chain | `Decrypt.hpp`, `decrypt_emu.cpp` | Internally consistent; no newer UC contradiction found |
| GetGlobalKey | Optional/diagnostic; active component and visibility chains do not use `GlobalKey1/2` | `Decrypt.hpp`, `main.cpp`, `decrypt_emu.cpp` | Left unchanged except documentation |

## DecryptComponent Flow

1. Split the component id into `shift = id & 0x3F`, `bit = 1 << shift`, and
   `bucket = id >> 6`.
2. Read the component bitmap from `parent + 0x110 + bucket*8`.
3. If the requested bit is absent, return `0`.
4. Popcount all bitmap bits lower than the requested bit.
5. Add the per-bucket base byte at `parent + 0x130 + bucket` to get the table
   index.
6. Read the component table from `parent + 0x80`, then read the encrypted
   component qword at `table + index*8`.
7. Decode with the May 2026 component key material:
   `+0x4C8675CDE55BA1B2`, XOR `RPM(RPM(base+0x3A86E30)+0x10C)`,
   `+0x7BE57670994040F6`, XOR `RPM<uint8>(base+0x3772769)`,
   XOR `0x3864150DB528414C`, XOR `0xA4764E53CD34159B`, net `ROR64(3)`.
8. Mask by the presence bit and return the decoded component pointer.

## DecryptVis Flow

1. Read `enc = RPM<uint64>(VisBase + 0x98)`.
2. Compute `enc = ROR64(enc, 3) ^ 0x53DB07B6B873760C`.
3. Load cached `var_qword = RPM(base + 0x3A86E30)` and
   `var_byte = RPM<uint8>(base + 0x3772769)`.
4. Compute `dec = (var_byte ^ (enc - 0x7A7DB4DE6CD03BBC)) +
   0x5CE60F50EA1D337F`.
5. Compute `dec = ROR64(dec + 0x78D75198F1D34D38, 0xC)`.
6. Compute `dec = RPM<uint64>(var_qword + 0x6A) ^ ((2 * dec) | (dec >> 0x3F))`.
7. UC expects `dec == 1` for visible and `dec == 0` for occluded.

## GetParent Flow

1. Start from the encrypted parent qword.
2. `result = ROR64(result, 32)`.
3. `result ^= 0x4B920A7072A077C5`.
4. `result -= 0x107816B001CA79C8`.
5. `result = ROR64(result, 35)`.
6. `result += 0xFD2150D0AEF24514`.

## GetGlobalKey Flow

`GetGlobalKey` is no longer on the active May 2026 decrypt path. `main.cpp`
skips it because `DecryptComponent` and `DecryptVis` read their key material
directly from current game memory. The function remains as a diagnostic helper:
old pattern scan first, then IDA-derived `GetGlobalKey_RVA = 0x581D20` decoding
by extracting the live LEA, immediate constants, and RIP-relative global store.
