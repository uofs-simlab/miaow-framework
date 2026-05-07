#include "bmi_heat.hxx"

extern "C" bmi::Bmi* bmi_model_create() {
    return new BmiHeat();
}

extern "C" void bmi_model_destroy(bmi::Bmi* model) {
    delete model;
}
