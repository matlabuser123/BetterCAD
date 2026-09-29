# P15-REFMOD-001 — analytical error tables

Expected values from closed form, computed independently of BetterCAD. Actual values
from the reference-model runner's own 17-significant-digit output, which is the same
code path the tests and the CLI use.

### RM-MAT-01 — aluminium block 200 x 300 x 500 mm at 2700 kg/m^3

Closed form: `V = abc, m = rho abc, I = m(b^2+c^2)/12 etc.`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)                      30000000                 30000000          0         0  yes
m (kg)                              81                       81          0         0  yes
CM x (mm)                          100                      100          0         0  yes
CM y (mm)                          150                      150          0         0  yes
CM z (mm)                          250                      250          0         0  yes
Ixx (kg m^2)        2.2949999999999999       2.2950000000000026   2.66e-15   1.2e-15  yes
Iyy (kg m^2)        1.9575000000000002       1.9575000000000007   4.44e-16   2.3e-16  yes
Izz (kg m^2)       0.87750000000000006      0.87750000000000039   3.33e-16   3.8e-16  yes
Ixy (kg m^2)                         0                        0          0        0*  yes
Ixz (kg m^2)                         0  -3.2958984375000003e-16    3.3e-16  1.4e-16*  yes
Iyz (kg m^2)                         0                        0          0        0*  yes
```

### RM-MAT-02 — steel shaft r 50 mm, h 400 mm at 7800 kg/m^3

Closed form: `V = pi r^2 h, Iaxis = m r^2/2, Itrans = m(3r^2+h^2)/12`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            3141592.6535897939       3141592.6535897921   1.86e-09   5.9e-16  yes
m (kg)               24.50442269800039       24.504422698000379   1.07e-14   4.3e-16  yes
CM x (mm)                            0  -3.7352597023507247e-14   3.74e-14  3.7e-14*  yes
CM y (mm)                            0  -4.9655238106646536e-15   4.97e-15    5e-15*  yes
CM z (mm)                          200                      200          0         0  yes
Ixx (kg m^2)       0.34204090015958882      0.34204090015958422   4.61e-15   1.3e-14  yes
Iyy (kg m^2)       0.34204090015958882      0.34204090015959321   4.39e-15   1.3e-14  yes
Izz (kg m^2)      0.030630528372500489     0.030630528372500472   1.73e-17   5.7e-16  yes
Ixy (kg m^2)                         0  -3.2230294891633083e-18   3.22e-18  9.4e-18*  yes
Ixz (kg m^2)                         0   4.5910477638244629e-18   4.59e-18  1.3e-17*  yes
Iyz (kg m^2)                         0   4.7672074288129966e-19   4.77e-19  1.4e-18*  yes
```

### RM-MAT-03 — steel tube Ro 60, Ri 40, h 300 mm at 7800 kg/m^3

Closed form: `V = pi(Ro^2-Ri^2)h, Iaxis = m(Ro^2+Ri^2)/2, Itrans = m[3(Ro^2+Ri^2)+h^2]/12`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            1884955.5921538759       1884955.5921538761   2.33e-10   1.2e-16  yes
m (kg)              14.702653618800232       14.702653618800234   1.78e-15   1.2e-16  yes
CM x (mm)                            0  -2.4704098560520654e-15   2.47e-15  2.5e-15*  yes
CM y (mm)                            0  -4.1519290647125049e-15   4.15e-15  4.2e-15*  yes
CM z (mm)                          150       150.00000000000003   2.84e-14   1.9e-16  yes
Ixx (kg m^2)       0.12938335184544206      0.12938335184543781   4.25e-15   3.3e-14  yes
Iyy (kg m^2)       0.12938335184544206      0.12938335184544586    3.8e-15   2.9e-14  yes
Izz (kg m^2)      0.038226899408880605     0.038226899408880605          0         0  yes
Ixy (kg m^2)                         0  -2.7875819796463475e-18   2.79e-18  2.2e-17*  yes
Ixz (kg m^2)                         0   8.9496374130248973e-18   8.95e-18  6.9e-17*  yes
Iyz (kg m^2)                         0   3.0646333470940091e-19   3.06e-19  2.4e-18*  yes
```

### RM-MAT-04 — part A 100^3 mm at 2700 kg/m^3

Closed form: `V = abc`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            1000000.0000000002       1000000.0000000002          0         0  yes
m (kg)              2.7000000000000006       2.7000000000000006          0         0  yes
CM x (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
CM y (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
CM z (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
Ixx (kg m^2)     0.0045000000000000014    0.0045000000000000031   1.73e-18   3.9e-16  yes
Iyy (kg m^2)     0.0045000000000000014    0.0045000000000000031   1.73e-18   3.9e-16  yes
Izz (kg m^2)     0.0045000000000000014    0.0045000000000000083   6.94e-18   1.5e-15  yes
Ixy (kg m^2)                         0  -2.5749206542968752e-18   2.57e-18  5.7e-16*  yes
Ixz (kg m^2)                         0  -2.5749206542968752e-18   2.57e-18  5.7e-16*  yes
Iyz (kg m^2)                         0  -2.5749206542968752e-18   2.57e-18  5.7e-16*  yes
```

### RM-MAT-04 — part B 200 x 100 x 50 mm at 7800 kg/m^3 (the SAME volume as A)

Closed form: `V = abc`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            1000000.0000000002       1000000.0000000002          0         0  yes
m (kg)              7.8000000000000016       7.8000000000000016          0         0  yes
CM x (mm)                          100       99.999999999999986   1.42e-14   1.4e-16  yes
CM y (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
CM z (mm)                           25       24.999999999999996   3.55e-15   1.4e-16  yes
Ixx (kg m^2)     0.0081250000000000037    0.0081250000000000107   6.94e-18   8.5e-16  yes
Iyy (kg m^2)      0.027625000000000011     0.027625000000000025   1.39e-17     5e-16  yes
Izz (kg m^2)      0.032500000000000015     0.032500000000000043   2.78e-17   8.5e-16  yes
Ixy (kg m^2)                         0    -1.48773193359375e-17   1.49e-17  4.6e-16*  yes
Ixz (kg m^2)                         0  -7.4386596679687499e-18   7.44e-18  2.3e-16*  yes
Iyz (kg m^2)                         0  -3.7193298339843749e-18   3.72e-18  1.1e-16*  yes
```

### RM-MAT-04 — assembly cube 80^3 mm at 7800 kg/m^3

Closed form: `V = abc`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            512000.00000000006       512000.00000000017   1.16e-10   2.3e-16  yes
m (kg)              3.9936000000000007       3.9936000000000016   8.88e-16   2.2e-16  yes
CM x (mm)                           40                       40          0         0  yes
CM y (mm)                           40                       40          0         0  yes
CM z (mm)                           40                       40          0         0  yes
Ixx (kg m^2)     0.0042598400000000008    0.0042598399999999939   6.94e-18   1.6e-15  yes
Iyy (kg m^2)     0.0042598400000000008    0.0042598399999999974   3.47e-18   8.1e-16  yes
Izz (kg m^2)     0.0042598400000000008    0.0042598399999999974   3.47e-18   8.1e-16  yes
Ixy (kg m^2)                         0   1.8596649169921875e-18   1.86e-18  4.4e-16*  yes
Ixz (kg m^2)                         0   1.8596649169921875e-18   1.86e-18  4.4e-16*  yes
Iyz (kg m^2)                         0   1.8596649169921875e-18   1.86e-18  4.4e-16*  yes
```

### RM-MAT-05 — custom body 120 x 80 x 60 mm at 2700 kg/m^3

Closed form: `V = abc`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            575999.99999999988                   576000   1.16e-10     2e-16  yes
m (kg)              1.5551999999999997       1.5552000000000001   4.44e-16   2.9e-16  yes
CM x (mm)                           60                       60          0         0  yes
CM y (mm)                           40                       40          0         0  yes
CM z (mm)                           30                       30          0         0  yes
Ixx (kg m^2)     0.0012959999999999998    0.0012959999999999998          0         0  yes
Iyy (kg m^2)     0.0023327999999999995    0.0023328000000000012   1.73e-18   7.4e-16  yes
Izz (kg m^2)     0.0026956799999999993    0.0026956800000000011   1.73e-18   6.4e-16  yes
Ixy (kg m^2)                         0                        0          0        0*  yes
Ixz (kg m^2)                         0                        0          0        0*  yes
Iyz (kg m^2)                         0                        0          0        0*  yes
```

### RM-MAT-06 — incomplete-material body 100^3 mm at 2700 kg/m^3

Closed form: `V = abc`

```text
Quantity                      Expected                   Actual    Abs err   Rel err  PASS
V (mm^3)            1000000.0000000002       1000000.0000000002          0         0  yes
m (kg)              2.7000000000000006       2.7000000000000006          0         0  yes
CM x (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
CM y (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
CM z (mm)                           50       49.999999999999993   7.11e-15   1.4e-16  yes
Ixx (kg m^2)     0.0045000000000000014    0.0045000000000000031   1.73e-18   3.9e-16  yes
Iyy (kg m^2)     0.0045000000000000014    0.0045000000000000031   1.73e-18   3.9e-16  yes
Izz (kg m^2)     0.0045000000000000014    0.0045000000000000083   6.94e-18   1.5e-15  yes
Ixy (kg m^2)                         0  -2.5749206542968752e-18   2.57e-18  5.7e-16*  yes
Ixz (kg m^2)                         0  -2.5749206542968752e-18   2.57e-18  5.7e-16*  yes
Iyz (kg m^2)                         0  -2.5749206542968752e-18   2.57e-18  5.7e-16*  yes
```

**Worst relative error anywhere in the suite: 3.28e-14** (RM-MAT-03 Ixx (kg m^2)), against a
tolerance of 1e-10 — four orders of margin. Entries marked `*` are quantities closed form
puts at exactly zero; the figure shown is the absolute error as a fraction of the largest
diagonal moment, and their gate is 1e-9 of it.

Every quantity in every table passes: **True**.
