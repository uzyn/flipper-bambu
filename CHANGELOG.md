# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Second build path using [ufbt](https://github.com/flipperdevices/flipperzero-ufbt).
  `make ufbt-build` produces `dist/bambu_parser.fal` against the downloaded
  Flipper SDK, with no firmware submodule and no firmware toolchain, and
  `make ufbt-deploy` writes it straight to a connected Flipper Zero. CI now
  builds both paths and fails if they ship different app metadata.

### Changed

- The read path no longer re-detects the MIFARE Classic card type. The NFC app's
  own poller has already determined the type (and the UID) before it hands the
  device to a supported-card plugin, so `bambu_read` now gates on the type it is
  given instead of calling `mf_classic_poller_sync_detect_type`. The card-presence
  fast-fail that `detect_type` incidentally provided is retained as a single
  `mf_classic_poller_sync_collect_nt` probe, so the read drops from three full NFC
  poller cycles to two rather than one, removing one cycle of heap churn and RF
  time from every scan.
  ([#3](https://github.com/uzyn/flipper-bambu/issues/3))
- Peak stack use in the key-derivation path dropped from 1,624 to 880 bytes
  (-46%). `bambu_read` runs on the NFC app's main thread, which has a 5 KB stack
  (`applications/main/nfc/application.fam`: `stack_size=5 * 1024`), so the chain
  `bambu_read` → `bambu_hmac_sha256` → `bambu_sha256_update`/`_final` →
  `bambu_sha256_transform` was using about a third of it. `bambu_read` no longer
  holds the 496-byte `MfClassicDeviceKeys` on the stack (768 → 272 bytes), and
  `bambu_hmac_sha256` reuses one SHA-256 context for the inner and outer hashes
  and one 64-byte pad buffer instead of separate `ipad`/`opad` (504 → 256 bytes).
  The derived keys are bit-identical: verified against RFC 4231 vectors, against
  an independent Python HKDF reference for every `test/data` fixture UID, and by
  a differential test of the old and new code over ~237,000 input pairs.
  `mf_classic_poller_sync_read` takes `MfClassicDeviceKeys` **by value**
  (`mf_classic_poller_sync.c:475`), copying it into its own 544-byte frame, so
  as a stack local the same 496 bytes were resident on that 5 KB stack twice
  concurrently for the whole of that call, on every read. This removes one of
  the two copies; the firmware's is not reachable from here.
- **This trades stack for heap and does not reduce peak heap.** The
  `MfClassicDeviceKeys` that used to live on the stack is now a 496-byte
  `malloc`, held only across `mf_classic_poller_sync_read` and freed on the
  single path out — which is also precisely the window in which the poller's own
  allocations peak. It does not address the out-of-memory crash in
  [#3](https://github.com/uzyn/flipper-bambu/issues/3), which is a heap problem,
  and it moves transient heap use slightly the wrong way.
- `bambu_hmac_sha256` no longer supports keys longer than the 64-byte SHA-256
  block size. Both call sites pass 16 (the master key) or 32 (the PRK), so the
  RFC 2104 key-hashing branch was unreachable; dropping it removes a third
  `BambuSha256Context`. The assumption is now enforced with `furi_check`, which
  is unconditional, rather than `furi_assert`, which is compiled out of every
  shipped build. The guard currently costs nothing: the compiler proves both
  call sites pass 16 or 32 and deletes the comparison, and it reappears if a
  call site it cannot bound is ever added.
- The plugin is now built in release mode (`DEBUG=0 COMPACT=1`). `furi_assert`
  is compiled out, shrinking `.text` from 3,884 to 3,408 bytes and `.rodata`
  from 8,468 to 7,596 bytes. The `.fal` is 1,736 bytes smaller on disk, of which
  1,348 bytes (~1.3 KB) is resident RAM while the plugin is loaded — only the
  allocated sections stay mapped; the rest is relocation and symbol data that is
  not retained.
- **For contributors:** `furi_assert` is a no-op in every shipped `.fal`. It is
  gated on `#ifdef FURI_DEBUG` (`furi/core/check.h:77`), and `FURI_DEBUG` is
  defined only by `DEBUG=1` builds, which neither build path here uses. Note
  that `NDEBUG` is *not* the switch — fbt defines `NDEBUG` in every
  configuration, `DEBUG=1` included, so its presence says nothing about whether
  asserts survive. Any invariant that must hold in production has to use
  `furi_check` or an explicit `if`, or the guard silently disappears from the
  released artifact.

### Fixed

- `bambu_parse` no longer reports spool data from blocks that were never read.
  It validated blocks 1, 2, 4 and 5 and then read blocks 6, 8, 10, 12 and 14
  unconditionally, so a card whose first two sectors were recovered but whose
  later sectors were not — a partial dictionary attack, or a saved dump whose
  required *data* blocks are `??` — passed validation and rendered the unread
  blocks as though they were real, printing `Nozzle: >= 0.00mm` and
  `Spool Width: 0.00mm` next to a correct type, colour and weight, with a blank
  production date. (`??` in the sector trailers is normal — every Flipper-saved
  dump has it — and does not trigger the rejection.) The read path already
  performed this check; `parse` now does too, and such cards fall through to the
  NFC app's generic MIFARE Classic view instead. The check is skipped when
  `block_read_mask` is entirely zero, which is how the firmware represents dumps
  saved before `Data format version: 2` — those load with an empty mask whether
  their data is complete or not, so enforcing it there would reject complete
  saved dumps that parse correctly today. The cost of that exemption is that a
  pre-v2 dump which is itself partial is still rendered with zeros; the mask
  carries no information on those files, so there is nothing to gate on. That
  residual case is tracked in
  [#8](https://github.com/uzyn/flipper-bambu/issues/8).
- Sectors are no longer authenticated with key B. `bambu_derive_keys_from_uid`
  wrote the derived key into both the key A and the key B slot, so the poller
  was offered 16 key A entries plus 16 byte-identical key B entries. Only key A
  is derivable — the HKDF context is `"RFID-A"`, and upstream
  [RFID-Tag-Guide](https://github.com/Bambu-Research-Group/RFID-Tag-Guide)
  publishes that derivation alone, describing what it recovers as "all required
  A-Keys" — and key A on its own reads every block this plugin parses, so the
  key B entries could not unlock anything key A could not. That guide's
  `BambuLabRfid.md` also documents the trailer's key B as "always
  `00 00 00 00 00 00` for Bambu tags", which the non-zero derived key never
  matches, so each of those 16 offers failed; and because a failed
  authentication halts the tag, the poller retried each one once per block in
  the sector: 64 failed authentications and 64 card re-selections per scan, so
  the read path drops from 80 authentications to 16. The parsed result is
  unchanged. ([#3](https://github.com/uzyn/flipper-bambu/issues/3))

## [1.1.0] - 2026-05-13

### Added

- Automatic Mifare Classic key derivation from the tag UID using HKDF over
  HMAC-SHA256. Reading a Bambu Lab spool tag no longer requires the
  "skip key matching" step on the Flipper Zero — keys are derived on the
  fly and the read completes straight to the parsed view.
  (Thanks to @amilham — [#1](https://github.com/uzyn/flipper-bambu/pull/1))
- Additional filament entries across PLA Basic, PLA Metal, PLA Tough+,
  PLA Translucent, ASA, ASA-CF, PC FR, and PETG-CF.
  ([#1](https://github.com/uzyn/flipper-bambu/pull/1))
- Variant-ID normalization so tags written with zero-padded codes
  (for example `A00-K00` or `A00-G06`) resolve to their canonical entries
  (`A00-K0`, `A00-G6`) in the lookup table.
  ([#1](https://github.com/uzyn/flipper-bambu/pull/1))

### Changed

- For unrecognized filaments, the parsed output now prints the raw
  `Variant:` alongside `Material ID:` and shows a clearer
  "Filament Code: Unknown (update lookup table)" hint, making it easier
  to report new variants for the lookup table.
  ([#1](https://github.com/uzyn/flipper-bambu/pull/1))
- Plugin now reports `fap_version` 1.1 on-device (previously defaulted to 0.1).

## [1.0.0] - 2026-01-15

### Added

- Initial release: Bambu Lab filament NFC parser plugin for Flipper Zero.

[Unreleased]: https://github.com/uzyn/flipper-bambu/compare/1.1.0...HEAD
[1.1.0]: https://github.com/uzyn/flipper-bambu/compare/1.0.0...1.1.0
[1.0.0]: https://github.com/uzyn/flipper-bambu/releases/tag/1.0.0
