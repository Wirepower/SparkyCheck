# Standards PDFs (local reference)

Licensed AS/NZS PDFs used as the source of truth when aligning SparkyCheck verification text with the Wiring Rules, test & verification Standard, in-service inspection, and safe working on or near low-voltage electrical installations.

| File | Typical edition in firmware copy |
|------|----------------------------------|
| `AS-NZS-3000-2018.pdf` | AS/NZS 3000 (incl. Section 8 verification) |
| `AS-NZS-3017-2022.pdf` | AS/NZS 3017 |
| `AS-NZS-3760-2022.pdf` | AS/NZS 3760 |
| `AS-NZS-4836-2023.pdf` | AS/NZS 4836 |

## Populating this folder

PDFs are **not committed** to git (large binaries; see repo `.gitignore`). Copy from your licensed files, for example:

`OneDrive - Wirepower\Desktop\unprotected pdfs\`

Use the filenames above so docs and tooling stay consistent.

## Use

- Human reference while editing `src/VerificationSteps.cpp`, limits, and admin copy.
- Cursor / IDE search across the repo; assistants can use paths under `docs/standards-pdfs/` when the files are present locally.

Respect your licence: these standards remain copyright to Standards Australia / other rights holders.
