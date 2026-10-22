// The viewer consumes the result: recoverSteadyBalance is the core's entry point and storedEnergyChange is a member.
int view() { auto b = recoverSteadyBalance(); return b.storedEnergyChange; }
