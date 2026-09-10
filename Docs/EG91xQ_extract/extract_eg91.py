from pathlib import Path
from pypdf import PdfReader
import re

base = Path(r"d:/workbench-zephyrprojs/zephyr-v3.7.0/applications/stm32_modem_demo/Docs")
files = [
    "Quectel_EG800Q&EG91xQ_Series_AT_Commands_Manual_V1.1.pdf",
    "Quectel_EG800Q&EG91xQ_Series_MUX_Application_Note_V1.1.pdf",
    "Quectel_EG800Q&EG91xQ_Series_PPP_Application_Note_V1.0.pdf",
]
keys = [
    r"AT\\+CMUX", r"ATD\\*99", r"AT\\+CGDCONT", r"AT\\+CGACT", r"AT\\+CFUN", r"AT\\+CMEE",
    r"AT\\+CREG", r"AT\\+CGREG", r"AT\\+CEREG", r"AT\\+CGSN", r"AT\\+CGMM", r"AT\\+CGMI", r"AT\\+CGMR",
    r"AT\\+CIMI", r"AT\\+QCCID", r"PPP", r"LCP", r"PDP", r"DLCI", r"MUX", r"escape", r"\+\+\+",
]
pat = re.compile("|".join(keys), re.IGNORECASE)

out = base / "EG91xQ_extract"
out.mkdir(exist_ok=True)

for name in files:
    p = base / name
    r = PdfReader(str(p))
    lines = []
    for i, page in enumerate(r.pages, start=1):
        t = page.extract_text() or ""
        for raw in t.splitlines():
            s = " ".join(raw.strip().split())
            if not s:
                continue
            if pat.search(s):
                lines.append(f"[p{i:03d}] {s}")
    (out / (p.stem + ".keylines.txt")).write_text("\n".join(lines), encoding="utf-8")
    print(f"{name}: {len(lines)} lines")
