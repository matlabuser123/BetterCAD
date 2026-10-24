void f(Document& d) { d.modifyObject<bettercad::thermal::ThermalAnalysis>(id, [](auto& o) { return touch(o); }); }
