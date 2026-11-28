# Tandem Controls — Electronics Hardware (`hardware/`)

This directory holds open-source electrical schematics, KiCad projects, PCB layouts, analog front-end conditioning circuits, power-path designs, and bills of materials (BOMs) for **Tandem Controls** (tracked in GitHub Issue `#64`).

## License

All files in `hardware/` are licensed under the **CERN Open Hardware Licence Version 2 — Strongly Reciprocal (`CERN-OHL-S-2.0`)**. See [LICENSE](LICENSE) for the full legal text and [../LICENSE](../LICENSE) for the repository Trademark Notice.

## Repository Hygiene & Release Assets

- **Keep source files lightweight in Git:** Commit native KiCad schematics (`.kicad_sch`), PCB sources (`.kicad_pcb`, `.kicad_pro`), custom symbols/footprints, and human-readable BOM/documentation files directly under `hardware/`.
- **Publish manufacturing packages on GitHub Releases:** Do **not** commit generated manufacturing Gerber `.zip` archives, drill files, pick-and-place bundles, or large rendered 3D STEP/binary previews to Git — attach them to versioned **GitHub Releases** alongside firmware binaries.
