# Tandem Controls — Mechanical CAD & Enclosures (`mechanical/`)

This directory holds open-source 3D-printable enclosures, gimbals, grips, mounting adapters, and parametric CAD sources for **Tandem Controls**.

## License

All files in `mechanical/` are licensed under the **Creative Commons Attribution-ShareAlike 4.0 International (`CC-BY-SA-4.0`)** license. See [LICENSE](LICENSE) for the full legal text and [../LICENSE](../LICENSE) for the repository Trademark Notice.

## Repository Hygiene & Release Assets

- **Keep parametric CAD sources lightweight in Git:** Commit native parametric design sources (for example FreeCAD, OpenSCAD, STEP, or Fusion source exports) and print/assembly notes directly under `mechanical/`.
- **Publish compiled STL packs on GitHub Releases:** Do **not** commit generated/high-poly `.stl` or `.3mf` mesh packs to Git — attach ready-to-slice `.stl` archives to versioned **GitHub Releases**.
