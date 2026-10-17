# Aesthetiocizer

Voice-controlled car firmware and hardware design for the Seeed XIAO RP2040. The firmware captures microphone audio, runs an Edge Impulse model, and uses recognized commands to control the car.

## Design

[![View PCB on KiCanvas](https://hack.club/pcb-badge)](https://kicanvas.org/?repo=https://github.com/ViratS-best/Aesthetiocizer/tree/main/design)

<img width="853" height="505" alt="Screenshot 2026-10-04 105420" src="https://github.com/user-attachments/assets/dfc26dc3-f346-4230-8837-f16f39944a36" />
<img width="1165" height="1135" alt="image" src="https://github.com/user-attachments/assets/87d59577-f363-44c9-96d6-24190596ba41" />

## Commands

The firmware responds to classifications with confidence of at least 0.85:
- `drive` starts the motors.
- `stop` stops the motors.
- `music` plays a melody through the speaker.
- `lights` flashes the LEDs.

## Project Contents

- `example-standalone-inferencing-pico-main/` contains the Pico application and its Edge Impulse inference sources.
- `viratsuper6-project-1-cpp-mcu-v1-impulse-#1/` contains the exported Edge Impulse model and inference SDK files.
- `design/` contains the KiCad schematic, PCB, project, BOM.csv, and 3D board export.
- `Screenshot *.png` files show project images.

## Bill of Materials (BOM)

| Part | Qty | Unit | Total | Vendor |
| :--- | :---: | :---: | :---: | :--- |
| [PCB Connection between all parts](https://www.jlcpcb.com) | 5 | $0.80 | $4.00 | [JLCPCB](https://www.jlcpcb.com) |
| [Seeed Studio XIAO RP2040 Microcontroller](https://www.amazon.com/dp/B0DRNTQ338) | 1 | $10.99 | $10.99 | [Amazon](https://www.amazon.com) |
| [Digital MEMS Microphone Module with PDM Output, MP34DT01](https://www.amazon.com/dp/B0HKML3MT8) | 1 | $9.29 | $9.29 | [Amazon](https://www.amazon.com) |
| Tax & shipping for the order | – | – | $5.00 | – |
| **Total** | | | **$29.28** | |
