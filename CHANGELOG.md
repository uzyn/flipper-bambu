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
  builds both paths.

### Changed

- The plugin is now built in release mode (`DEBUG=0 COMPACT=1`). `furi_assert`
  is compiled out, shrinking `.text` from 3,884 to 3,408 bytes and `.rodata`
  from 8,468 to 7,596 bytes — roughly 1.7 KB less RAM occupied while the plugin
  is loaded.

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
