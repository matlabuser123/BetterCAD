void f(Document& d) { d.modifyObject<thermal::ThermalAnalysis>(id, [](auto& o) { return o.setDefinition(x); }); }
