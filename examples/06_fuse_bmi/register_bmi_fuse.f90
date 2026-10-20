! C-callable entry point that creates the FUSE BMI model handle
! used by MIAOW's dynamic Fortran BMI loader.
function register_bmi(this) result(bmi_status) bind(C, name="register_bmi")
    use, intrinsic :: iso_c_binding, only: c_int, c_loc, c_ptr
    use bmif_2_0_iso,    only: BMI_FAILURE, BMI_SUCCESS
    use fuse_bmi_module, only: fuse_bmi
    use iso_c_bmif_2_0,  only: box
    implicit none
    type(c_ptr) :: this
    integer(kind=c_int) :: bmi_status
    type(fuse_bmi), pointer :: bmi_model
    type(box),      pointer :: bmi_box
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
