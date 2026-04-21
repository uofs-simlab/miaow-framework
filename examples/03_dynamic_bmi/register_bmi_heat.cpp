#include "bmi_heat.hxx"

extern "C" int register_bmi(bmi::Bmi** model) {
    if (model == nullptr) {
        return 1;
    }
    *model = new BmiHeat();
    return 0;
}

extern "C" int unregister_bmi(bmi::Bmi** model) {
    if (model == nullptr || *model == nullptr) {
        return 1;
    }
    delete *model;
    *model = nullptr;
    return 0;
}
