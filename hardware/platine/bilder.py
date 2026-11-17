#!/usr/bin/env python3
# Bilder der Platine und Bestueckungsseite 1:1 als PDF, aus den SVG von kicad-cli.
# Aufruf: python3 bilder.py [Platine.kicad_pcb]
# Ergebnis neben der Platine: bestueckung_klein_usbfrei.png, kupfer_oben_klein_usbfrei.png,
# kupfer_unten_klein_usbfrei.png, esp_venus_klein_usbfrei_1zu1.pdf. Braucht kicad-cli und PyMuPDF.
import os
import subprocess
import sys
import tempfile

import fitz

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'esp_venus_klein_usbfrei.kicad_pcb')
OUT = os.path.dirname(os.path.abspath(PCB))
SUF = '_klein_usbfrei'
TMP = tempfile.mkdtemp()


def svg(layers, name, bw=False):
    path = os.path.join(TMP, name + '.svg')
    cmd = ['kicad-cli', 'pcb', 'export', 'svg', '-o', path, '-l', layers, '--page-size-mode', '2',
           '--exclude-drawing-sheet']
    if bw:
        cmd.append('--black-and-white')
    subprocess.run(cmd + [PCB], capture_output=True, check=True)
    return fitz.open(path)


def png(layers, name, zoom=5):
    pix = svg(layers, name)[0].get_pixmap(matrix=fitz.Matrix(zoom, zoom), alpha=False)
    pix.save(os.path.join(OUT, f'{name}{SUF}.png'))


png('F.SilkS,F.Mask,Edge.Cuts,Dwgs.User', 'bestueckung')
png('F.Cu,Edge.Cuts', 'kupfer_oben')
png('B.Cu,Edge.Cuts', 'kupfer_unten')

# Bestueckungsseite 1:1 auf A4 quer, mit Kontrolllinie 100 mm
d = svg('F.SilkS,F.Mask,Edge.Cuts,Dwgs.User', 'eins', bw=True)
src = fitz.open('pdf', d.convert_to_pdf())
r = src[0].rect
mm = 72 / 25.4
doc = fitz.open()
page = doc.new_page(width=297 * mm, height=210 * mm)
x0, y0 = 40 * mm, 35 * mm
page.show_pdf_page(fitz.Rect(x0, y0, x0 + r.width, y0 + r.height), src, 0)
page.insert_text((x0, 18 * mm), 'ESP_venus Platine v1 klein usbfrei, Ansicht von oben, Maßstab 1:1. '
                 'Ohne Skalierung drucken (100 %).', fontsize=10)
page.insert_text((x0, 24 * mm), 'Kontrolle: Die Linie unten ist 100 mm lang.', fontsize=10)
yl = y0 + r.height + 12 * mm
page.draw_line((x0, yl), (x0 + 100 * mm, yl), width=0.6)
for k in range(0, 101, 10):
    page.draw_line((x0 + k * mm, yl - 1.5 * mm), (x0 + k * mm, yl + 1.5 * mm), width=0.4)
page.insert_text((x0 + 102 * mm, yl + 1 * mm), '100 mm', fontsize=9)
doc.save(os.path.join(OUT, f'esp_venus{SUF}_1zu1.pdf'))
print('Bilder und 1:1-PDF in', OUT)
