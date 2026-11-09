# P19-LINSOLVE-001 experiments

Four rounds of measurement, run BEFORE any production code existed, against the
already-built `debug-ext` libraries. Each is a standalone program compiled
outside the frozen surface:

```text
g++ -std=c++23 -O2 -o q_linsolveN.exe q_linsolveN.cpp \
    -I<repo>/include -I<build>/generated/include -I<build>/_deps/eigen3-src \
    -L<build>/lib -lbettercad_linalg -lbettercad_core
```

`q_linsolve.cpp`  rounds one: Q1 asymmetric input to the symmetric kernel, Q3 the
                  pressure shape, Q4 the momentum shape, Q2 SparseLU on singular
                  systems.
`q_linsolve2.cpp` round two: Q5 the pressure pivot ratio against scale, Q6 is
                  SparseLU's U diagonal reachable, and the CONTROL that proves
                  the residual loop measures anything.
`q_linsolve3.cpp` round three: Q5' both pin paths after round two's premise was
                  found false, Q7 SparseQR's R diagonal and its cost.
`q_linsolve4.cpp` round four: the same questions on a fixture whose rank is
                  CHECKED FIRST, after round three's momentum fixture was found
                  singular by its own output.

TWO OF THE FOUR ROUNDS CORRECTED A DEFECT IN THE ROUND BEFORE IT, and both
defects were in the measurement rather than in the production code. Round two
modelled the pressure pin as a unit diagonal and called that what the qualified
coupling writes; `CfdCoupling.hpp` says the rule is "the diagonal keeps its own
value" and the unit value is a reported exception. Round three's momentum chain
was anchored at one end only and was therefore singular, which its own output
announced by printing the SAME largest LU-vs-QR gap, `1.574122e+12`, for three
different system sizes -- one number for three different systems cannot be a
property of the systems.
