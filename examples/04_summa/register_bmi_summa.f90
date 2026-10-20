! Export the model factory independently of the SUMMA module's private procedures
function register_bmi(model) result(bmi_status) bind(C, name="register_bmi")
    use, intrinsic :: iso_c_binding, only: c_int, c_loc, c_ptr
    use bmif_2_0_iso, only: BMI_FAILURE, BMI_SUCCESS
    use iso_c_bmif_2_0, only: box
    use summabmi, only: summa_bmi
    implicit none

    type(c_ptr) :: model
    integer(kind=c_int) :: bmi_status
    type(summa_bmi), pointer :: bmi_model
    type(box), pointer :: bmi_box

    allocate(bmi_model)
    allocate(bmi_box)
    bmi_box%ptr => bmi_model

    if (associated(bmi_box%ptr)) then
        model = c_loc(bmi_box)
        bmi_status = BMI_SUCCESS
    else
        bmi_status = BMI_FAILURE
    end if
end function register_bmi
