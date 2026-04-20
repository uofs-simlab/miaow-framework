! This C-callable entry point creates the concrete Fortran BMI heat model that
! DynamicBmiModel loads at runtime. The CSDMS Fortran heat example implements
! the BMI methods, but it does not define the register_bmi plugin symbol used
! by the NOAA ISO C Fortran BMI bridge.
function register_bmi(this) result(bmi_status) bind(C, name="register_bmi")
    use, intrinsic :: iso_c_binding, only: c_int, c_loc, c_ptr
    use bmif_2_0_iso, only: BMI_FAILURE, BMI_SUCCESS
    use bmiheatf, only: bmi_heat
    use iso_c_bmif_2_0, only: box
    implicit none

    type(c_ptr) :: this
    integer(kind=c_int) :: bmi_status
    type(bmi_heat), pointer :: bmi_model
    type(box), pointer :: bmi_box

    allocate(bmi_model)
    allocate(bmi_box)
    bmi_box%ptr => bmi_model

    if (associated(bmi_box) .and. associated(bmi_box%ptr)) then
        this = c_loc(bmi_box)
        bmi_status = BMI_SUCCESS
    else
        bmi_status = BMI_FAILURE
    end if
end function register_bmi
