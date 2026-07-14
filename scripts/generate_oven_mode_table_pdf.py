#!/usr/bin/env python3
"""Generate a 3-column oven function reference table as PDF."""

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Spacer, Paragraph, Table, TableStyle, Flowable


class OvenSymbol(Flowable):
    """Simple vector redraw of oven mode symbols."""

    def __init__(self, kind: str, size: float = 18 * mm):
        super().__init__()
        self.kind = kind
        self.size = size
        self.width = size + 2 * mm
        self.height = size + 2 * mm

    def _fan(self, c, x, y, s, ring=False):
        cx = x + s / 2
        cy = y + s / 2
        blade = s * 0.18
        if ring:
            c.circle(cx, cy, s * 0.24)
        c.line(cx - blade, cy, cx + blade, cy)
        c.line(cx, cy - blade, cx, cy + blade)
        c.line(cx - blade * 0.75, cy - blade * 0.75, cx + blade * 0.75, cy + blade * 0.75)
        c.line(cx - blade * 0.75, cy + blade * 0.75, cx + blade * 0.75, cy - blade * 0.75)
        c.circle(cx, cy, s * 0.04, stroke=1, fill=1)

    def _zigzag_top(self, c, x, y, s):
        top = y + s * 0.82
        left = x + s * 0.18
        step = s * 0.12
        pts = [
            (left, top),
            (left + step, top - step * 0.8),
            (left + 2 * step, top),
            (left + 3 * step, top - step * 0.8),
            (left + 4 * step, top),
            (left + 5 * step, top - step * 0.8),
        ]
        for p1, p2 in zip(pts, pts[1:]):
            c.line(p1[0], p1[1], p2[0], p2[1])

    def draw(self):
        c = self.canv
        s = self.size
        x = 1 * mm
        y = 1 * mm
        c.setStrokeColor(colors.black)
        c.setLineWidth(1.1)

        if self.kind == "off":
            c.setFont("Helvetica-Bold", 13)
            c.drawCentredString(x + s / 2, y + s / 2 - 4, "0")
            return

        c.rect(x, y, s, s, stroke=1, fill=0)

        if self.kind == "light":
            cx = x + s / 2
            cy = y + s / 2 + s * 0.02
            c.circle(cx, cy, s * 0.16)
            c.rect(cx - s * 0.05, cy - s * 0.28, s * 0.1, s * 0.08, stroke=1, fill=0)
            c.line(cx, cy + s * 0.27, cx, cy + s * 0.35)
            c.line(cx + s * 0.22, cy, cx + s * 0.30, cy)
            c.line(cx - s * 0.22, cy, cx - s * 0.30, cy)
            return

        if self.kind in {"convection", "multi", "fast", "top", "top_bottom"}:
            c.line(x + s * 0.18, y + s * 0.82, x + s * 0.82, y + s * 0.82)
        if self.kind in {"convection", "multi", "fast", "delicate", "pizza", "bottom", "top_bottom"}:
            c.line(x + s * 0.18, y + s * 0.18, x + s * 0.82, y + s * 0.18)
        if self.kind in {"grill", "fan_grill"}:
            self._zigzag_top(c, x, y, s)
        if self.kind in {"defrost", "fan_forced", "fan_grill", "delicate", "fast", "pizza", "multi"}:
            self._fan(c, x, y, s, ring=self.kind in {"fan_forced", "pizza", "multi"})


def build_pdf(output_path: str):
    doc = SimpleDocTemplate(
        output_path,
        pagesize=A4,
        leftMargin=16 * mm,
        rightMargin=16 * mm,
        topMargin=14 * mm,
        bottomMargin=14 * mm,
    )

    styles = getSampleStyleSheet()
    title_style = styles["Heading2"]
    title_style.spaceAfter = 4
    subtitle_style = ParagraphStyle(
        "subtitle",
        parent=styles["Normal"],
        fontSize=9.5,
        textColor=colors.grey,
        spaceAfter=8,
    )
    cell_style = ParagraphStyle(
        "cell",
        parent=styles["BodyText"],
        fontSize=9.3,
        leading=12,
    )
    header_style = ParagraphStyle(
        "header",
        parent=styles["BodyText"],
        fontSize=10,
        leading=12,
        textColor=colors.white,
    )

    rows = [
        ("convection", "Convection mode", "Top and bottom heating elements operate together.", "General baking and roasting on one shelf; adjust shelf height to favor top or bottom browning."),
        ("grill", "Grill mode", "Top inner grill element provides intense direct heat.", "Browning and searing meats, chops, steaks, or finishing a crust; use with the door closed."),
        ("fan_grill", "Fan assisted grill mode", "Top grill element plus fan circulation.", "Thicker cuts and mixed grill items where you want a browned surface and better heat penetration."),
        ("multi", "Multi-cooking mode", "Top, bottom, and circular heating elements alternate with fan circulation.", "Even multi-rack cooking for casseroles, roasts, pasta bakes, fish, vegetables, and cakes."),
        ("top", "Top heat mode", "Top heating element only.", "Final browning, gratin finishing, or adding color to the surface at the end of cooking."),
        ("defrost", "Defrost mode", "Fan circulates room-temperature air with no direct heating element.", "Gentle thawing of meat, bread, and delicate desserts while reducing defrost time."),
        ("delicate", "Delicate cooking mode", "Bottom element and fan operate together.", "Pastries, cakes, and sweets that benefit from bottom-focused heat for better base structure."),
        ("fan_forced", "Fan forced mode", "Rear fan element and fan provide uniform circulating heat.", "Even baking across multiple shelves, especially small pastries, biscuits, puffs, and light items."),
        ("fast", "Fast cooking mode", "Top and bottom elements plus fan deliver quick, even heat.", "Fast cooking of frozen or pre-cooked foods and simple homemade dishes, usually without preheating."),
        ("pizza", "Pizza mode", "Bottom and circular elements plus fan create strong bottom-biased heat.", "Pizza and high-temperature dishes requiring crisp bases; best results using one tray/rack at a time."),
        ("light", "Oven light", "Turns on interior lamp only (no heating).", "Check food progress safely without opening the door or applying heat."),
        ("off", "Off", "No element or fan operation.", "Use when the oven is not in use."),
    ]

    table_data = [[
        Paragraph("<b>Symbol</b>", header_style),
        Paragraph("<b>What the function does</b>", header_style),
        Paragraph("<b>What the function is used for</b>", header_style),
    ]]

    for symbol, fn_name, does, used_for in rows:
        table_data.append([
            OvenSymbol(symbol),
            Paragraph(f"<b>{fn_name}</b><br/>{does}", cell_style),
            Paragraph(used_for, cell_style),
        ])

    table = Table(
        table_data,
        colWidths=[28 * mm, 65 * mm, 86 * mm],
        repeatRows=1,
        hAlign="LEFT",
    )
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#37474F")),
                ("GRID", (0, 0), (-1, -1), 0.6, colors.HexColor("#B0BEC5")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 1), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 1), (-1, -1), 6),
                ("ALIGN", (0, 1), (0, -1), "CENTER"),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#FAFCFD")]),
            ]
        )
    )

    story = [
        Paragraph("Oven Function Reference Table", title_style),
        Paragraph(
            "Symbols are simplified redraws based on the supplied oven manual and control-knob image.",
            subtitle_style,
        ),
        Spacer(1, 2 * mm),
        table,
    ]
    doc.build(story)


if __name__ == "__main__":
    build_pdf("/workspace/docs/oven_function_reference_table.pdf")
    print("Generated /workspace/docs/oven_function_reference_table.pdf")
