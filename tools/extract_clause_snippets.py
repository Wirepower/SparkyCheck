"""One-off helper: find clause text in local standards PDFs (run from repo root)."""
from pathlib import Path

try:
    from pypdf import PdfReader
except ImportError:
    raise SystemExit("pip install pypdf")

BASE = Path(__file__).resolve().parents[1] / "docs" / "standards-pdfs"


def find_snippets(pdf_name: str, needles: list[str], max_pages: int | None = None) -> None:
    path = BASE / pdf_name
    if not path.exists():
        print("Missing:", path)
        return
    r = PdfReader(str(path))
    n = len(r.pages) if max_pages is None else min(max_pages, len(r.pages))
    print(f"\n=== {pdf_name} ({len(r.pages)} pages), scan first {n} ===")
    for i in range(n):
        t = r.pages[i].extract_text() or ""
        for nd in needles:
            if nd in t:
                j = t.find(nd)
                lo = max(0, j - 120)
                hi = min(len(t), j + 900)
                print(f"\n--- page {i + 1} hit {nd!r} ---")
                print(t[lo:hi])
                return


if __name__ == "__main__":
    find_snippets("AS-NZS-3000-2018.pdf", ["8.3.7", "8.3.8", "Polarity"])
    find_snippets("AS-NZS-3017-2022.pdf", ["4.4", "4.5", "4.7", "4.8", "4.9"])
    find_snippets("AS-NZS-3760-2022.pdf", ["2.4", "2.4.2", "In-service"])
    find_snippets("AS-NZS-4836-2023.pdf", ["3.2.3", "3.2.6", "Disconnect"])
