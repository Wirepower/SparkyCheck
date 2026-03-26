Boot / splash assets for SparkyCheck.

**Boot logo on device**

1. Export your artwork as a **24-bit uncompressed BMP** (e.g. from Paint, GIMP, or Inkscape export → PNG → save as BMP).
2. Save or replace **`assets/boot_logo.bmp`**.
3. From the project root run: **`python tools/embed_boot_logo.py`**  
   (This regenerates **`include/boot_logo_embedded.h`**. Commit both files when the logo changes.)

To generate a placeholder BMP and header for a clean clone:  
**`python tools/embed_boot_logo.py --placeholder`**

Reference SVGs live under **`assets/boot-logo-samples/`** (export to BMP yourself for embedding).

The splash also shows **SparkyCheck**, creator credit, and **Tap to continue** (see `src/BootScreen.cpp`).  
`drawIndustryGraphic()` remains available as a code-drawn fallback motif if needed elsewhere.
