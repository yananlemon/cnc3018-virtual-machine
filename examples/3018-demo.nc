; v0.3 top-left machine XY0, X right, Y front. Home first.
; G2 negative/G3 positive numeric angles; Y-down view mirrors visual handedness.
G21 G90 G17 G54
; Wood top = machine Z -70; establish Z5 at 5 mm clearance.
G53 G0 X120 Y65 Z-65
G92 X0 Y0 Z5
M3 S8000
G1 Z-1 F300
G1 X60 F600
G1 Y50
G1 X0
G1 Y0
G0 Z5
G0 X15 Y25
G1 Z-2 F300
G2 X45 Y25 I15 J0 F450
G2 X15 Y25 I-15 J0
G0 Z5
M5
M30
