# Licensing

PowerRAID is released under three licences, one per artefact class. The full
texts are in [`LICENSES/`](LICENSES/).

| Artefact class | Licence | Scope |
| --- | --- | --- |
| Hardware design files | **CERN-OHL-P-2.0** | KiCad project, fabrication outputs, STEP models, DXF and laser-cut files, enclosure CAD |
| Firmware and software | **Apache-2.0** | `firmware/`, `analysis/`, `scripts/` |
| Documentation, data, and figures | **CC-BY-4.0** | `Docs/`, `README.md`, measurement CSVs, generated figures, this file |

SPDX-License-Identifier for each class:

```
CERN-OHL-P-2.0    hardware
Apache-2.0        software
CC-BY-4.0         documentation, data, figures
```

## Rationale

**CERN-OHL-P-2.0** is a hardware-specific permissive licence with an express
patent grant, written for designs whose functional elements sit largely outside
copyright. It permits manufacture, modification, and commercial distribution
of boards derived from these files. The P variant was chosen over S and W
because permissive licensing ensures seamless reproduction, manufacturing, and
commercial distribution without imposing reciprocal terms, and because the Onshape
public document for the enclosure carries an unconditional public-use grant under
Onshape's Terms of Use (see *Platform terms* below) that a reciprocal licence
would contradict.

**Apache-2.0** rather than GPL for the firmware: it supplies the explicit
patent grant MIT lacks, while avoiding the GPL-3 installation-information
obligation on a device firmware image, and it combines without question with
the LGPL-2.1 Arduino-ESP32 core that `firmware/esp32s3-pmbus-bridge/` links
against.

**CC-BY-4.0** for prose, data, and figures: attribution is the only
restriction that serves a purpose here, and it matches the licence the
manuscript itself is published under.

CC BY-NC-SA and other non-commercial or no-derivatives terms are deliberately
absent: they would fail the Open Source Hardware Definition, which requires
that anyone may make and sell hardware based on the design.

## Third-party components embedded in the software

`firmware/pramon/` bundles assets that keep their own licences:

- `@fontsource-variable/inter` — SIL Open Font License 1.1
- `svelte`, `vite`, `tailwindcss`, `bits-ui`, `shadcn-svelte`, `lucide` — MIT

`firmware/esp32s3-pmbus-bridge/` links the Arduino-ESP32 core (LGPL-2.1) and
ESP-IDF components (Apache-2.0, MIT, BSD-3-Clause). These are dependencies, not
contributions to this project, and are not relicensed by this file.

`firmware/pramon/node_modules/` and `firmware/pramon/dist/` are build artefacts
and are excluded from the deposit. Rebuild with `bun install && bun run build`.

## Platform terms

The enclosure was authored in Onshape. Onshape's Terms of Use grant, for any
Public Document owned by a Free Plan user created on or after 2018-08-07, "a
worldwide, royalty-free and non-exclusive license ... to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell" the contents. That grant
is broader than CERN-OHL-P-2.0 in some respects and narrower in none that
matter for a permissive licence, so the two are compatible. The archival record
of the enclosure is the STEP and DXF set deposited under CERN-OHL-P-2.0; the
Onshape document is a convenience copy, not the licenced artefact.

Exports from Onshape do not carry feature history, which is why the STEP and
DXF files, not the Onshape link, constitute the deposited source.

## Contributing

Contributions are accepted under the licence of the directory they modify. Do
not add files whose terms you cannot grant under that licence.
