G21
G90
G0 X0

; Fast forward (snap out)
G1 X55 F5000

; Jerk backward (quick pullback)
G1 X45 F5000

; Proceed forward to full extension
G1 X120 F8000

; Return home
G1 X0 F8000