module simple_fortran_bmi_fixture
    use, intrinsic :: iso_c_binding, only: c_char, c_int
    use bmif_2_0_iso, only: bmi, BMI_FAILURE, BMI_MAX_COMPONENT_NAME, &
        BMI_MAX_VAR_NAME, BMI_SUCCESS
    implicit none

    ! Minimal scalar model used to exercise the procedural Fortran BMI bridge
    type, extends(bmi) :: simple_fortran_bmi
        double precision :: current_time = 0.0d0
        double precision :: temperature = 0.0d0
    contains
        procedure :: initialize => simple_initialize
        procedure :: update => simple_update
        procedure :: update_until => simple_update_until
        procedure :: finalize => simple_finalize
        procedure :: get_component_name => simple_component_name
        procedure :: get_input_item_count => simple_item_count
        procedure :: get_output_item_count => simple_item_count
        procedure :: get_input_var_names => simple_var_names
        procedure :: get_output_var_names => simple_var_names
        procedure :: get_var_grid => simple_var_grid
        procedure :: get_var_type => simple_var_type
        procedure :: get_var_units => simple_var_units
        procedure :: get_var_itemsize => simple_var_itemsize
        procedure :: get_var_nbytes => simple_var_nbytes
        procedure :: get_var_location => simple_var_location
        procedure :: get_current_time => simple_current_time
        procedure :: get_start_time => simple_start_time
        procedure :: get_end_time => simple_end_time
        procedure :: get_time_units => simple_time_units
        procedure :: get_time_step => simple_time_step
        procedure :: get_value_int => simple_get_value_int
        procedure :: get_value_float => simple_get_value_float
        procedure :: get_value_double => simple_get_value_double
        procedure :: get_value_ptr_int => simple_get_value_ptr_int
        procedure :: get_value_ptr_float => simple_get_value_ptr_float
        procedure :: get_value_ptr_double => simple_get_value_ptr_double
        procedure :: get_value_at_indices_int => simple_get_at_indices_int
        procedure :: get_value_at_indices_float => simple_get_at_indices_float
        procedure :: get_value_at_indices_double => simple_get_at_indices_double
        procedure :: set_value_int => simple_set_value_int
        procedure :: set_value_float => simple_set_value_float
        procedure :: set_value_double => simple_set_value_double
        procedure :: set_value_at_indices_int => simple_set_at_indices_int
        procedure :: set_value_at_indices_float => simple_set_at_indices_float
        procedure :: set_value_at_indices_double => simple_set_at_indices_double
        procedure :: get_grid_rank => simple_grid_rank
        procedure :: get_grid_size => simple_grid_size
        procedure :: get_grid_type => simple_grid_type
        procedure :: get_grid_shape => simple_grid_shape
        procedure :: get_grid_spacing => simple_grid_spacing
        procedure :: get_grid_origin => simple_grid_origin
        procedure :: get_grid_x => simple_grid_x
        procedure :: get_grid_y => simple_grid_y
        procedure :: get_grid_z => simple_grid_z
        procedure :: get_grid_node_count => simple_grid_count
        procedure :: get_grid_edge_count => simple_grid_count
        procedure :: get_grid_face_count => simple_grid_count
        procedure :: get_grid_edge_nodes => simple_grid_edge_nodes
        procedure :: get_grid_face_edges => simple_grid_face_edges
        procedure :: get_grid_face_nodes => simple_grid_face_nodes
        procedure :: get_grid_nodes_per_face => simple_grid_nodes_per_face
    end type simple_fortran_bmi

    character(len=BMI_MAX_COMPONENT_NAME), target :: component_name = &
        "MIAOW test scalar"
    character(len=BMI_MAX_VAR_NAME), target, dimension(1) :: variable_names = &
        ["temperature"]

contains

    ! Implement the lifecycle and clock surface used by the test pipeline
    function simple_initialize(this, config_file) result(bmi_status)
        class(simple_fortran_bmi), intent(out) :: this
        character(kind=c_char, len=*), intent(in) :: config_file
        integer(kind=c_int) :: bmi_status

        this%current_time = 0.0d0
        this%temperature = 0.0d0
        bmi_status = BMI_SUCCESS
    end function simple_initialize

    function simple_update(this) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        integer :: bmi_status

        this%current_time = this%current_time + 1.0d0
        bmi_status = BMI_SUCCESS
    end function simple_update

    function simple_update_until(this, time) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        double precision, intent(in) :: time
        integer :: bmi_status

        this%current_time = time
        bmi_status = BMI_SUCCESS
    end function simple_update_until

    function simple_finalize(this) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        integer :: bmi_status

        bmi_status = BMI_SUCCESS
    end function simple_finalize

    function simple_component_name(this, name) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), pointer, intent(out) :: name
        integer :: bmi_status

        name => component_name
        bmi_status = BMI_SUCCESS
    end function simple_component_name

    function simple_current_time(this, time) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        double precision, intent(out) :: time
        integer :: bmi_status

        time = this%current_time
        bmi_status = BMI_SUCCESS
    end function simple_current_time

    function simple_start_time(this, time) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        double precision, intent(out) :: time
        integer :: bmi_status

        time = 0.0d0
        bmi_status = BMI_SUCCESS
    end function simple_start_time

    function simple_end_time(this, time) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        double precision, intent(out) :: time
        integer :: bmi_status

        time = 2.0d0
        bmi_status = BMI_SUCCESS
    end function simple_end_time

    function simple_time_units(this, units) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(out) :: units
        integer :: bmi_status

        units = "s"
        bmi_status = BMI_SUCCESS
    end function simple_time_units

    function simple_time_step(this, time_step) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        double precision, intent(out) :: time_step
        integer :: bmi_status

        time_step = 1.0d0
        bmi_status = BMI_SUCCESS
    end function simple_time_step

    ! Expose one scalar variable as both forcing input and observable output
    function simple_item_count(this, count) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(out) :: count
        integer :: bmi_status

        count = 1
        bmi_status = BMI_SUCCESS
    end function simple_item_count

    function simple_var_names(this, names) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), pointer, intent(out) :: names(:)
        integer :: bmi_status

        names => variable_names
        bmi_status = BMI_SUCCESS
    end function simple_var_names

    ! Describe the scalar temperature variable used by the forcing pipeline
    function simple_var_grid(this, name, grid) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        integer, intent(out) :: grid
        integer :: bmi_status

        if (name == "temperature") then
            grid = 0
            bmi_status = BMI_SUCCESS
        else
            grid = -1
            bmi_status = BMI_FAILURE
        end if
    end function simple_var_grid

    function simple_var_type(this, name, type) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        character(len=*), intent(out) :: type
        integer :: bmi_status

        if (name == "temperature") then
            type = "double"
            bmi_status = BMI_SUCCESS
        else
            type = ""
            bmi_status = BMI_FAILURE
        end if
    end function simple_var_type

    function simple_var_units(this, name, units) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        character(len=*), intent(out) :: units
        integer :: bmi_status

        if (name == "temperature") then
            units = "K"
            bmi_status = BMI_SUCCESS
        else
            units = ""
            bmi_status = BMI_FAILURE
        end if
    end function simple_var_units

    function simple_var_itemsize(this, name, size) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        integer, intent(out) :: size
        integer :: bmi_status

        if (name == "temperature") then
            size = storage_size(this%temperature) / 8
            bmi_status = BMI_SUCCESS
        else
            size = 0
            bmi_status = BMI_FAILURE
        end if
    end function simple_var_itemsize

    function simple_var_nbytes(this, name, nbytes) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        integer, intent(out) :: nbytes
        integer :: bmi_status

        if (name == "temperature") then
            nbytes = storage_size(this%temperature) / 8
            bmi_status = BMI_SUCCESS
        else
            nbytes = 0
            bmi_status = BMI_FAILURE
        end if
    end function simple_var_nbytes

    function simple_var_location(this, name, location) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        character(len=*), intent(out) :: location
        integer :: bmi_status

        if (name == "temperature") then
            location = "node"
            bmi_status = BMI_SUCCESS
        else
            location = ""
            bmi_status = BMI_FAILURE
        end if
    end function simple_var_location

    function simple_get_value_int(this, name, dest) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        integer, intent(inout) :: dest(:)
        integer :: bmi_status

        dest = 0
        bmi_status = BMI_FAILURE
    end function simple_get_value_int

    function simple_get_value_float(this, name, dest) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        real, intent(inout) :: dest(:)
        integer :: bmi_status

        dest = 0.0
        bmi_status = BMI_FAILURE
    end function simple_get_value_float

    function simple_get_value_double(this, name, dest) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        double precision, intent(inout) :: dest(:)
        integer :: bmi_status

        if (name == "temperature") then
            dest(1) = this%temperature
            bmi_status = BMI_SUCCESS
        else
            dest = 0.0d0
            bmi_status = BMI_FAILURE
        end if
    end function simple_get_value_double

    function simple_get_value_ptr_int(this, name, dest_ptr) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        integer, pointer, intent(inout) :: dest_ptr(:)
        integer :: bmi_status

        nullify(dest_ptr)
        bmi_status = BMI_FAILURE
    end function simple_get_value_ptr_int

    function simple_get_value_ptr_float(this, name, dest_ptr) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        real, pointer, intent(inout) :: dest_ptr(:)
        integer :: bmi_status

        nullify(dest_ptr)
        bmi_status = BMI_FAILURE
    end function simple_get_value_ptr_float

    function simple_get_value_ptr_double(this, name, dest_ptr) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        double precision, pointer, intent(inout) :: dest_ptr(:)
        integer :: bmi_status

        nullify(dest_ptr)
        bmi_status = BMI_FAILURE
    end function simple_get_value_ptr_double

    ! Reject indexed access because the pipeline transfers whole scalar values
    function simple_get_at_indices_int(this, name, dest, inds) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        integer, intent(inout) :: dest(:)
        integer, intent(in) :: inds(:)
        integer :: bmi_status

        dest = 0
        bmi_status = BMI_FAILURE
    end function simple_get_at_indices_int

    function simple_get_at_indices_float(this, name, dest, inds) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        real, intent(inout) :: dest(:)
        integer, intent(in) :: inds(:)
        integer :: bmi_status

        dest = 0.0
        bmi_status = BMI_FAILURE
    end function simple_get_at_indices_float

    function simple_get_at_indices_double(this, name, dest, inds) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        character(len=*), intent(in) :: name
        double precision, intent(inout) :: dest(:)
        integer, intent(in) :: inds(:)
        integer :: bmi_status

        dest = 0.0d0
        bmi_status = BMI_FAILURE
    end function simple_get_at_indices_double

    function simple_set_value_int(this, name, src) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        character(len=*), intent(in) :: name
        integer, intent(in) :: src(:)
        integer :: bmi_status

        bmi_status = BMI_FAILURE
    end function simple_set_value_int

    function simple_set_value_float(this, name, src) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        character(len=*), intent(in) :: name
        real, intent(in) :: src(:)
        integer :: bmi_status

        bmi_status = BMI_FAILURE
    end function simple_set_value_float

    function simple_set_value_double(this, name, src) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        character(len=*), intent(in) :: name
        double precision, intent(in) :: src(:)
        integer :: bmi_status

        if (name == "temperature") then
            this%temperature = src(1)
            bmi_status = BMI_SUCCESS
        else
            bmi_status = BMI_FAILURE
        end if
    end function simple_set_value_double

    function simple_set_at_indices_int(this, name, inds, src) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        character(len=*), intent(in) :: name
        integer, intent(in) :: inds(:)
        integer, intent(in) :: src(:)
        integer :: bmi_status

        bmi_status = BMI_FAILURE
    end function simple_set_at_indices_int

    function simple_set_at_indices_float(this, name, inds, src) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        character(len=*), intent(in) :: name
        integer, intent(in) :: inds(:)
        real, intent(in) :: src(:)
        integer :: bmi_status

        bmi_status = BMI_FAILURE
    end function simple_set_at_indices_float

    function simple_set_at_indices_double(this, name, inds, src) result(bmi_status)
        class(simple_fortran_bmi), intent(inout) :: this
        character(len=*), intent(in) :: name
        integer, intent(in) :: inds(:)
        double precision, intent(in) :: src(:)
        integer :: bmi_status

        bmi_status = BMI_FAILURE
    end function simple_set_at_indices_double

    ! Describe the scalar grid containing the test variable
    function simple_grid_rank(this, grid, rank) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: rank
        integer :: bmi_status

        if (grid == 0) then
            rank = 0
            bmi_status = BMI_SUCCESS
        else
            rank = -1
            bmi_status = BMI_FAILURE
        end if
    end function simple_grid_rank

    function simple_grid_size(this, grid, size) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: size
        integer :: bmi_status

        if (grid == 0) then
            size = 1
            bmi_status = BMI_SUCCESS
        else
            size = 0
            bmi_status = BMI_FAILURE
        end if
    end function simple_grid_size

    function simple_grid_type(this, grid, type) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        character(len=*), intent(out) :: type
        integer :: bmi_status

        if (grid == 0) then
            type = "scalar"
            bmi_status = BMI_SUCCESS
        else
            type = ""
            bmi_status = BMI_FAILURE
        end if
    end function simple_grid_type

    function simple_grid_shape(this, grid, shape) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: shape(:)
        integer :: bmi_status

        shape = 0
        bmi_status = BMI_FAILURE
    end function simple_grid_shape

    function simple_grid_spacing(this, grid, spacing) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        double precision, intent(out) :: spacing(:)
        integer :: bmi_status

        spacing = 0.0d0
        bmi_status = BMI_FAILURE
    end function simple_grid_spacing

    function simple_grid_origin(this, grid, origin) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        double precision, intent(out) :: origin(:)
        integer :: bmi_status

        origin = 0.0d0
        bmi_status = BMI_FAILURE
    end function simple_grid_origin

    function simple_grid_x(this, grid, x) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        double precision, intent(out) :: x(:)
        integer :: bmi_status

        x = 0.0d0
        bmi_status = BMI_FAILURE
    end function simple_grid_x

    function simple_grid_y(this, grid, y) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        double precision, intent(out) :: y(:)
        integer :: bmi_status

        y = 0.0d0
        bmi_status = BMI_FAILURE
    end function simple_grid_y

    function simple_grid_z(this, grid, z) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        double precision, intent(out) :: z(:)
        integer :: bmi_status

        z = 0.0d0
        bmi_status = BMI_FAILURE
    end function simple_grid_z

    function simple_grid_count(this, grid, count) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: count
        integer :: bmi_status

        count = 0
        bmi_status = BMI_FAILURE
    end function simple_grid_count

    function simple_grid_edge_nodes(this, grid, edge_nodes) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: edge_nodes(:)
        integer :: bmi_status

        edge_nodes = 0
        bmi_status = BMI_FAILURE
    end function simple_grid_edge_nodes

    function simple_grid_face_edges(this, grid, face_edges) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: face_edges(:)
        integer :: bmi_status

        face_edges = 0
        bmi_status = BMI_FAILURE
    end function simple_grid_face_edges

    function simple_grid_face_nodes(this, grid, face_nodes) result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: face_nodes(:)
        integer :: bmi_status

        face_nodes = 0
        bmi_status = BMI_FAILURE
    end function simple_grid_face_nodes

    function simple_grid_nodes_per_face(this, grid, nodes_per_face) &
        result(bmi_status)
        class(simple_fortran_bmi), intent(in) :: this
        integer, intent(in) :: grid
        integer, intent(out) :: nodes_per_face(:)
        integer :: bmi_status

        nodes_per_face = 0
        bmi_status = BMI_FAILURE
    end function simple_grid_nodes_per_face

end module simple_fortran_bmi_fixture

! Allocate the concrete model behind the procedural bridge handle
function register_bmi(model) result(bmi_status) bind(C, name="register_bmi")
    use, intrinsic :: iso_c_binding, only: c_int, c_loc, c_ptr
    use bmif_2_0_iso, only: BMI_FAILURE, BMI_SUCCESS
    use iso_c_bmif_2_0, only: box
    use simple_fortran_bmi_fixture, only: simple_fortran_bmi
    implicit none

    type(c_ptr) :: model
    integer(kind=c_int) :: bmi_status
    type(simple_fortran_bmi), pointer :: bmi_model
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
