#include "internal.hpp"

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <caf/actor_system.hpp>
#include <caf/io/middleman.hpp>
#include <caf/ipv4_address.hpp>
#include <caf/ipv4_endpoint.hpp>
#include <caf/ipv4_subnet.hpp>
#include <caf/ipv6_address.hpp>
#include <caf/ipv6_endpoint.hpp>
#include <caf/ipv6_subnet.hpp>
#include <caf/json_array.hpp>
#include <caf/json_object.hpp>
#include <caf/json_value.hpp>
#include <caf/scoped_actor.hpp>
#include <caf/type_id.hpp>

extern char** environ;

namespace miaow::core {

namespace {

constexpr const char* subprocess_file_prefix = "subprocess";
constexpr const char* subprocess_temp_dir_prefix = "miaow_subprocess";
constexpr std::chrono::seconds subprocess_startup_timeout{5};
constexpr std::chrono::milliseconds subprocess_exit_poll_interval{10};
std::atomic<unsigned long long> next_control_dir_id{0};

// Registration state belongs exclusively to one Runtime-local registration actor
struct SubprocessRegistrationState {
    SubprocessLaunchId next_launch_id_{0};
    std::unordered_map<SubprocessLaunchId, caf::actor> launch_owners_;
};

// CAF actor messages require core and MIAOW meta objects
void initialize_subprocess_caf() {
    caf::init_global_meta_objects<caf::id_block::core_module>();
    caf::init_global_meta_objects<caf::id_block::miaow_module>();
    caf::init_global_meta_objects<caf::id_block::miaow_internal_module>();
    caf::io::middleman::init_global_meta_objects();
}

// Write setup failures to the stderr file configured by posix_spawn
void write_subprocess_error(const std::string& message) {
    std::cerr << subprocess_error_prefix << ": subprocess setup error: " << message << "\n";
}

// Reuse the job directory when the runtime has assigned result output
std::filesystem::path make_control_dir(const Task& task) {
    if (!task.task_output_dir_.empty()) {
        const std::filesystem::path results_directory =
            std::filesystem::absolute(task.task_output_dir_).lexically_normal();
        const std::filesystem::path job_directory = results_directory.parent_path();
        std::filesystem::create_directories(job_directory);
        return job_directory;
    }

    // Generate a unique temporary control directory when no output directory exists
    const unsigned long long id =
        next_control_dir_id.fetch_add(1, std::memory_order_relaxed);
    const std::chrono::nanoseconds::rep stamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    const std::filesystem::path control_dir =
        std::filesystem::temp_directory_path() /
        (std::string(subprocess_temp_dir_prefix) + "_" + std::to_string(stamp) + "_" +
         std::to_string(id));
    std::filesystem::create_directories(control_dir);
    return control_dir;
}

// Include a bounded stderr prefix in parent-side failure messages
std::string read_file_prefix(const std::filesystem::path& path, std::size_t max_chars) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return "";
    }

    std::string text(max_chars, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

// Attempt final child termination and reaping when actor-driven cleanup cannot continue
void try_to_terminate_and_reap_child_process(pid_t subprocess_pid) noexcept {
    // Send SIGKILL to the child when actor-driven cleanup can no longer run
    if (::kill(subprocess_pid, SIGKILL) != 0 && errno != ESRCH) {
        return;
    }

    // Retry an interrupted blocking wait until the child is reaped or waitpid fails
    int wait_status = 0;
    while (::waitpid(subprocess_pid, &wait_status, 0) < 0 && errno == EINTR) {}
}

// Parsed command-line contract used by the child to reach its parent Runtime
struct SubprocessRegistrationInfo {
    std::string host_;
    std::uint16_t port_{0};
    SubprocessLaunchId launch_id_{0};
};

// Parse one unsigned command-line value without accepting suffix characters
std::uint64_t parse_unsigned_argument(const std::string& value,
                                      const std::string& argument_name,
                                      std::uint64_t maximum) {
    std::uint64_t parsed_value = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed_value);
    if (value.empty() || result.ec != std::errc() || result.ptr != end ||
        parsed_value > maximum) {
        throw std::runtime_error("invalid " + argument_name + " value '" + value + "'");
    }
    return parsed_value;
}

// Read the registration endpoint and launch ID passed directly through argv
SubprocessRegistrationInfo parse_subprocess_registration_info(int argc, char* argv[]) {
    std::string host;
    std::string port;
    std::string launch_id;

    // argv[0] is the executable path; the remaining pairs configure registration
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for " + argument);
        }

        ++index;
        if (argument == "--parent-registration-host") {
            host = argv[index];
        } else if (argument == "--parent-registration-port") {
            port = argv[index];
        } else if (argument == "--launch-id") {
            launch_id = argv[index];
        } else {
            throw std::runtime_error("unknown argument '" + argument + "'");
        }
    }

    if (host.empty()) {
        throw std::runtime_error("missing --parent-registration-host");
    }
    if (port.empty()) {
        throw std::runtime_error("missing --parent-registration-port");
    }
    if (launch_id.empty()) {
        throw std::runtime_error("missing --launch-id");
    }

    const std::uint64_t port_value =
        parse_unsigned_argument(port, "--parent-registration-port",
                                std::numeric_limits<std::uint16_t>::max());
    if (port_value == 0) {
        throw std::runtime_error("invalid --parent-registration-port value '0'");
    }

    return SubprocessRegistrationInfo{
        std::move(host), static_cast<std::uint16_t>(port_value),
        parse_unsigned_argument(launch_id, "--launch-id",
                                std::numeric_limits<SubprocessLaunchId>::max())};
}

} // namespace

// Allocate launch IDs before children can send their actor handles back to the parent
caf::behavior subprocess_registration_actor(caf::event_based_actor*) {
    const std::shared_ptr<SubprocessRegistrationState> state =
        std::make_shared<SubprocessRegistrationState>();

    return {
        // simulation_unit_actor -> subprocess_registration_actor: reserve a launch ID
        [state](reserve_subprocess_launch_id_atom,
                const caf::actor& owner) -> SubprocessLaunchId {
            const SubprocessLaunchId launch_id = state->next_launch_id_++;
            state->launch_owners_.emplace(launch_id, owner);
            return launch_id;
        },
        // simulation_unit_actor -> subprocess_registration_actor: release a launch ID
        [state](release_subprocess_launch_id_atom, SubprocessLaunchId launch_id) {
            state->launch_owners_.erase(launch_id);
        },
        // subprocess_scoped_actor -> subprocess_registration_actor: register its worker
        [state](register_subprocess_worker_atom, SubprocessLaunchId launch_id,
                const caf::actor& subprocess_worker) {
            const std::unordered_map<SubprocessLaunchId, caf::actor>::iterator
                subprocess_owner = state->launch_owners_.find(launch_id);
            if (subprocess_owner == state->launch_owners_.end()) {
                return;
            }

            // subprocess_registration_actor -> simulation_unit_actor: deliver the child
            caf::anon_mail(register_subprocess_worker_atom_v, launch_id,
                           subprocess_worker)
                .send(subprocess_owner->second);
            state->launch_owners_.erase(subprocess_owner);
        }};
}

namespace {
// Create the diagnostic files used by one child process
SubprocessPaths make_subprocess_paths(const Task& task, SubprocessLifetime lifetime) {
    const std::filesystem::path control_dir = make_control_dir(task);

    // Persistent subprocess logs use a distinct name because one child may execute
    // multiple tasks before the worker stops and decides whether to retain diagnostics
    const std::string task_name = task.task_name_.empty() ? "task" : task.task_name_;
    const std::string output_name = lifetime == SubprocessLifetime::persistent
                                        ? "persistent_" + task_name
                                        : task_name;
    const std::string control_stem =
        std::string(subprocess_file_prefix) + "_" + output_name;

    SubprocessPaths paths;
    paths.stdout_path_ = control_dir / (control_stem + "_stdout.log");
    paths.stderr_path_ = control_dir / (control_stem + "_stderr.log");
    paths.temporary_control_dir_ = task.task_output_dir_.empty();
    return paths;
}

// Remove parent-subprocess control files after a succeeded task result
void remove_subprocess_files(const SubprocessPaths& paths) {
    std::error_code ignored_error;
    std::filesystem::remove(paths.stdout_path_, ignored_error);
    std::filesystem::remove(paths.stderr_path_, ignored_error);
    if (paths.temporary_control_dir_) {
        std::filesystem::remove(paths.stdout_path_.parent_path(), ignored_error);
    }
}

// Format a subprocess failure with termination details and retained diagnostics
std::string format_subprocess_failure(const std::string& message,
                                      const SubprocessPaths& paths,
                                      std::optional<int> wait_status) {
    std::string failure_message = message;

    // Decode the POSIX status only after waitpid has reaped the child
    if (wait_status.has_value()) {
        if (WIFEXITED(*wait_status)) {
            failure_message += "; subprocess exited with code " +
                               std::to_string(WEXITSTATUS(*wait_status));
        } else if (WIFSIGNALED(*wait_status)) {
            failure_message += "; subprocess was terminated by signal " +
                               std::to_string(WTERMSIG(*wait_status));
        }
    }

    const std::string stderr_text = read_file_prefix(paths.stderr_path_, 2000);
    if (!stderr_text.empty()) {
        failure_message += ": " + stderr_text;
    }
    failure_message += "; subprocess stdout log: " + paths.stdout_path_.string();
    failure_message += "; subprocess stderr log: " + paths.stderr_path_.string();
    return failure_message;
}

pid_t spawn_subprocess_worker(const std::string& executable,
                              std::uint16_t subprocess_registration_port,
                              SubprocessLaunchId launch_id,
                              const SubprocessPaths& subprocess_paths) {
    // Keep the complete parent-to-child command-line contract beside its child parser
    // posix_spawn receives these strings directly as argv without invoking a shell
    std::vector<std::string> argument_storage{
        executable,
        "--parent-registration-host",
        subprocess_registration_host,
        "--parent-registration-port",
        std::to_string(subprocess_registration_port),
        "--launch-id",
        std::to_string(launch_id),
    };

    std::vector<char*> argument_pointers;
    argument_pointers.reserve(argument_storage.size() + 1);
    for (std::string& argument : argument_storage) {
        argument_pointers.push_back(argument.data());
    }
    argument_pointers.push_back(nullptr);

    posix_spawn_file_actions_t file_actions;
    int error_number = posix_spawn_file_actions_init(&file_actions);
    if (error_number != 0) {
        throw std::system_error(error_number, std::generic_category(),
                                "failed to initialize subprocess file actions");
    }

    // Redirect stdout and stderr without executing library code after a fork
    error_number = posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO,
                                                    subprocess_paths.stdout_path_.c_str(),
                                                    O_WRONLY | O_CREAT | O_TRUNC, 0666);
    // A zero return means stdout setup succeeded, so stderr setup can proceed
    if (error_number == 0) {
        error_number =
            posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO,
                                             subprocess_paths.stderr_path_.c_str(),
                                             O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
    if (error_number != 0) {
        posix_spawn_file_actions_destroy(&file_actions);
        throw std::system_error(error_number, std::generic_category(),
                                "failed to configure subprocess diagnostics");
    }

    // posix_spawn returns only after creating the child or reporting a launch error
    pid_t subprocess_pid = -1;
    error_number = ::posix_spawn(&subprocess_pid, executable.c_str(), &file_actions,
                                 nullptr, argument_pointers.data(), ::environ);
    const int destroy_error = posix_spawn_file_actions_destroy(&file_actions);
    if (error_number != 0) {
        throw std::system_error(error_number, std::generic_category(),
                                "failed to launch subprocess");
    }
    if (destroy_error != 0) {
        // Release the created child before reporting the local file-action error
        try_to_terminate_and_reap_child_process(subprocess_pid);
        throw std::system_error(destroy_error, std::generic_category(),
                                "failed to destroy subprocess file actions");
    }

    // Ownership of this process identifier transfers to the simulation_unit_actor
    return subprocess_pid;
}

std::optional<int> poll_for_child_process_exit_status(pid_t subprocess_pid) {
    int wait_status = 0;
    while (true) {
        const pid_t wait_result = ::waitpid(subprocess_pid, &wait_status, WNOHANG);
        // The matching process identifier means waitpid reaped the exact child
        if (wait_result == subprocess_pid) {
            return wait_status;
        }
        // A zero result means the child is still running
        if (wait_result == 0) {
            return std::nullopt;
        }
        // Retry when a signal interrupted waitpid before it observed the child
        if (errno == EINTR) {
            continue;
        }
        // Report ECHILD and other wait failures to the owning lifecycle state
        throw std::system_error(errno, std::generic_category(),
                                "failed to poll subprocess exit");
    }
}

void send_sigkill_to_child_process(pid_t subprocess_pid) {
    // ESRCH also permits waitpid polling because no running process received the signal
    if (::kill(subprocess_pid, SIGKILL) == 0 || errno == ESRCH) {
        return;
    }
    throw std::system_error(errno, std::generic_category(),
                            "failed to force subprocess termination");
}

// Polymorphic simulation unit that delegates one task to actor-owned subprocess state
class SubprocessSimulationUnit final : public SimulationUnit {
  public:
    SubprocessSimulationUnit(SubprocessState& subprocess_state,
                             std::string subprocess_executable,
                             SubprocessLifetime lifetime)
        : subprocess_state_(subprocess_state),
          subprocess_executable_(std::move(subprocess_executable)),
          lifetime_(lifetime) {}

    caf::result<TaskResult> execute(const Task& task) override {
        return subprocess_state_.execute(task, subprocess_executable_, lifetime_);
    }

  private:
    SubprocessState& subprocess_state_;
    std::string subprocess_executable_;
    SubprocessLifetime lifetime_;
};

// Host the public Task-to-TaskResult handler behind the child CAF message protocol
caf::behavior subprocess_worker_actor(caf::event_based_actor*,
                                      SubprocessTaskHandler task_handler) {
    return {
        // simulation_unit_actor -> subprocess_worker_actor: execute one Task;
        // caf::get_atom matches the caf::get_atom_v value sent by simulation_unit_actor
        [task_handler = std::move(task_handler)](caf::get_atom,
                                                 const Task& task) -> TaskResult {
            try {
                return task_handler(task);
            } catch (...) {
                return make_failed_task_result(std::current_exception(),
                                               "Subprocess task error");
            }
        }};
}

} // namespace

bool SimulationUnitRegistryAccess::uses_subprocess(const SimulationUnitRegistry& registry,
                                                   SimulationUnitId simulation_unit_id) {
    const auto registration = registry.registrations_.find(simulation_unit_id);
    return registration != registry.registrations_.end() &&
           std::holds_alternative<SimulationUnitRegistry::SubprocessRegistration>(
               registration->second);
}

std::unique_ptr<SimulationUnit>
SimulationUnitRegistryAccess::construct(const SimulationUnitRegistry& registry,
                                        SimulationUnitId simulation_unit_id,
                                        SubprocessState& subprocess_state) {
    const auto registration = registry.registrations_.find(simulation_unit_id);
    if (registration == registry.registrations_.end()) {
        return nullptr;
    }

    if (const auto* factory = std::get_if<SimulationUnitRegistry::SimulationUnitFactory>(
            &registration->second)) {
        return (*factory)();
    }

    const auto& subprocess =
        std::get<SimulationUnitRegistry::SubprocessRegistration>(registration->second);
    return std::make_unique<SubprocessSimulationUnit>(subprocess_state,
                                                      subprocess.executable_,
                                                      subprocess.lifetime_);
}

SubprocessState::SubprocessState(caf::event_based_actor* owner,
                                 caf::actor subprocess_registration_actor,
                                 std::uint16_t subprocess_registration_port,
                                 std::chrono::milliseconds task_request_timeout)
    : owner_(owner),
      subprocess_registration_actor_(std::move(subprocess_registration_actor)),
      subprocess_registration_port_(subprocess_registration_port),
      task_request_timeout_(task_request_timeout) {}

SubprocessState::~SubprocessState() {
    if (!subprocess_pid_.has_value()) {
        return;
    }

    // Attempt final termination and reaping during actor-system teardown
    try_to_terminate_and_reap_child_process(*subprocess_pid_);
    subprocess_pid_.reset();
}

// Respond when CAF reports that the actor running in the child has terminated
void SubprocessState::handle_worker_termination(SubprocessLaunchId launch_id,
                                                const caf::error& reason) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id) {
        return;
    }

    if (phase_ == SubprocessPhase::stopping_subprocess) {
        return;
    }

    if (phase_ == SubprocessPhase::waiting_for_task_result) {
        fail_task("subprocess worker actor terminated: " + caf::to_string(reason));
        return;
    }

    if (phase_ == SubprocessPhase::idle) {
        keep_logs_ = paths_.has_value();
        begin_stop();
    }
}

// Deliver one task result from the local worker to its waiting job actor
void SubprocessState::complete_task_response(TaskResult task_result) {
    // simulation_unit_actor -> job_actor: deliver the retained task response
    response_promise_.deliver(std::move(task_result));
    active_task_.reset();
    requested_subprocess_executable_.clear();
    requested_subprocess_lifetime_ = SubprocessLifetime::single_task;
    result_waiting_for_stop_.reset();
}

// Finish cleanup, then respond, relaunch retained work, or quit
void SubprocessState::finish_stop(std::optional<int> wait_status) {
    // Complete failure diagnostics after the child process has been reaped
    if (result_waiting_for_stop_.has_value() && paths_.has_value() &&
        result_waiting_for_stop_->status_ == TaskStatus::failed) {
        result_waiting_for_stop_->message_ = format_subprocess_failure(
            result_waiting_for_stop_->message_, *paths_, wait_status);
    }

    // Remove diagnostics that no failure path requested to retain
    if (!keep_logs_ && paths_.has_value()) {
        remove_subprocess_files(*paths_);
    }

    // Clear all child resources before returning to idle without a child
    subprocess_pid_.reset();
    subprocess_worker_ = caf::actor{};
    active_subprocess_executable_.clear();
    paths_.reset();
    keep_logs_ = false;
    launch_id_.reset();
    phase_ = SubprocessPhase::idle;

    // Deliver a completed result only after its child process has been reaped
    if (result_waiting_for_stop_.has_value()) {
        TaskResult task_result = std::move(*result_waiting_for_stop_);
        complete_task_response(std::move(task_result));
        if (shutdown_requested_) {
            owner_->quit();
        }
        return;
    }

    // Relaunch a task retained while an incompatible or failed child was reaped
    if (active_task_.has_value()) {
        if (shutdown_requested_) {
            complete_task_response(make_failed_task_result(
                "worker shut down before task execution", subprocess_error_prefix));
            owner_->quit();
        } else {
            request_subprocess_launch_id();
        }
        return;
    }

    // Quit only after every owned child process has been released
    if (shutdown_requested_) {
        owner_->quit();
    }
}

// Convert one subprocess lifecycle error into a failed task result
void SubprocessState::fail_task(std::string message) {
    keep_logs_ = paths_.has_value();
    result_waiting_for_stop_ =
        make_failed_task_result(std::move(message), subprocess_error_prefix);
    begin_stop();
}

// Request child termination, then poll its process identifier without blocking
void SubprocessState::begin_stop() {
    if (phase_ == SubprocessPhase::stopping_subprocess) {
        return;
    }

    if (launch_id_.has_value()) {
        // simulation_unit_actor -> subprocess_registration_actor: release the launch ID;
        // reject future registrations before terminating the exact owned child
        caf::anon_mail(release_subprocess_launch_id_atom_v, *launch_id_)
            .send(subprocess_registration_actor_);
    }
    phase_ = SubprocessPhase::stopping_subprocess;

    if (!subprocess_pid_.has_value()) {
        finish_stop(std::nullopt);
        return;
    }

    // Send SIGKILL to the process identified by subprocess_pid_ because cleanup starts
    // only after task completion, failure, timeout, or cancellation
    send_sigkill_to_child_process(*subprocess_pid_);

    // simulation_unit_actor -> simulation_unit_actor: begin non-blocking exit polling
    owner_->mail(poll_subprocess_exit_atom_v, *launch_id_).send(owner_);
}

// Handle a task result returned by the current child actor
void SubprocessState::handle_task_result(SubprocessLaunchId launch_id,
                                         TaskResult task_result) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::waiting_for_task_result) {
        return;
    }

    const bool keep_persistent_child =
        requested_subprocess_lifetime_ == SubprocessLifetime::persistent &&
        task_result.status_ == TaskStatus::succeeded;
    if (keep_persistent_child) {
        phase_ = SubprocessPhase::idle;
        complete_task_response(std::move(task_result));
        return;
    }

    keep_logs_ = task_result.status_ == TaskStatus::failed;
    result_waiting_for_stop_ = std::move(task_result);
    begin_stop();
}

// Handle an error returned by the current child actor request
void SubprocessState::handle_task_error(SubprocessLaunchId launch_id,
                                        const caf::error& error) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::waiting_for_task_result) {
        return;
    }

    fail_task("subprocess actor request failed: " + caf::to_string(error));
}

// Send the retained task to the registered child actor
void SubprocessState::request_task_result() {
    Task task_payload = *active_task_;
    if (!task_payload.task_output_dir_.empty()) {
        task_payload.task_output_dir_ =
            std::filesystem::absolute(task_payload.task_output_dir_)
                .lexically_normal()
                .string();
    }

    const SubprocessLaunchId launch_id = *launch_id_;
    phase_ = SubprocessPhase::waiting_for_task_result;
    const std::shared_ptr<SubprocessState> state = shared_from_this();

    // simulation_unit_actor -> subprocess_worker_actor: request one task result
    owner_->mail(caf::get_atom_v, std::move(task_payload))
        .request(subprocess_worker_, task_request_timeout_)
        .then(
            // subprocess_worker_actor -> simulation_unit_actor: return the task result
            [state, launch_id](TaskResult task_result) {
                state->handle_task_result(launch_id, std::move(task_result));
            },
            // subprocess_worker_actor -> simulation_unit_actor: report a task error
            [state, launch_id](const caf::error& error) {
                state->handle_task_error(launch_id, error);
            });
}

// Start a child process with an ID reserved by the registration actor
void SubprocessState::start_subprocess(SubprocessLaunchId launch_id) {
    if (phase_ != SubprocessPhase::starting_subprocess) {
        // simulation_unit_actor -> subprocess_registration_actor: release the stale ID
        caf::anon_mail(release_subprocess_launch_id_atom_v, launch_id)
            .send(subprocess_registration_actor_);
        return;
    }

    launch_id_ = launch_id;
    if (shutdown_requested_) {
        begin_stop();
        return;
    }

    try {
        paths_ = make_subprocess_paths(*active_task_, requested_subprocess_lifetime_);
        keep_logs_ = false;
        subprocess_pid_ =
            spawn_subprocess_worker(requested_subprocess_executable_,
                                    subprocess_registration_port_, launch_id, *paths_);
        active_subprocess_executable_ = requested_subprocess_executable_;

        // simulation_unit_actor -> simulation_unit_actor: enforce the startup deadline
        owner_->mail(subprocess_startup_timeout_atom_v, launch_id)
            .delay(subprocess_startup_timeout)
            .send(owner_);
    } catch (const std::exception& exception) {
        fail_task(exception.what());
    } catch (...) {
        fail_task("unknown subprocess launch error");
    }
}

// Handle an error returned by the launch ID reservation request
void SubprocessState::handle_launch_id_request_error(const caf::error& error) {
    if (phase_ != SubprocessPhase::starting_subprocess) {
        return;
    }

    if (shutdown_requested_) {
        begin_stop();
        return;
    }

    fail_task("failed to reserve subprocess launch ID: " + caf::to_string(error));
}

// Reserve an ID before launching a child that can register with the parent
void SubprocessState::request_subprocess_launch_id() {
    phase_ = SubprocessPhase::starting_subprocess;
    const caf::actor owner = owner_;
    const std::shared_ptr<SubprocessState> state = shared_from_this();

    // simulation_unit_actor -> subprocess_registration_actor: reserve one launch ID
    owner_->mail(reserve_subprocess_launch_id_atom_v, owner)
        .request(subprocess_registration_actor_, caf::infinite)
        .then(
            // subprocess_registration_actor -> simulation_unit_actor: return a launch ID
            [state](SubprocessLaunchId launch_id) { state->start_subprocess(launch_id); },
            // subprocess_registration_actor -> simulation_unit_actor: report an ID error
            [state](const caf::error& error) {
                state->handle_launch_id_request_error(error);
            });
}

caf::result<TaskResult> SubprocessState::execute(const Task& task,
                                                 const std::string& subprocess_executable,
                                                 SubprocessLifetime lifetime) {
    if (response_promise_.pending()) {
        return caf::make_error(
            caf::sec::runtime_error,
            "simulation unit worker already has an active subprocess task");
    }

    // Retain the job actor request while the asynchronous lifecycle continues
    response_promise_ = owner_->make_response_promise<TaskResult>();
    active_task_ = task;
    requested_subprocess_executable_ = subprocess_executable;
    requested_subprocess_lifetime_ = lifetime;

    if (phase_ == SubprocessPhase::stopping_subprocess) {
        // Returning the promise marks the CAF request as delegated
        return response_promise_;
    }

    // A retained actor and matching executable identify a reusable persistent child
    const bool reusable_persistent_child =
        requested_subprocess_lifetime_ == SubprocessLifetime::persistent &&
        subprocess_worker_ &&
        active_subprocess_executable_ == requested_subprocess_executable_;
    if (reusable_persistent_child) {
        request_task_result();
    } else if (subprocess_pid_.has_value()) {
        begin_stop();
    } else {
        request_subprocess_launch_id();
    }

    // The member retains the reply while caf::result carries only a delegated marker
    return response_promise_;
}

void SubprocessState::accept_registration(SubprocessLaunchId launch_id,
                                          const caf::actor& subprocess_worker) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::starting_subprocess) {
        return;
    }

    subprocess_worker_ = subprocess_worker;
    const std::shared_ptr<SubprocessState> state = shared_from_this();

    // subprocess_worker_actor -> simulation_unit_actor: report actor termination
    owner_->monitor(subprocess_worker, [state, launch_id](const caf::error& reason) {
        state->handle_worker_termination(launch_id, reason);
    });

    request_task_result();
}

void SubprocessState::expire_startup(SubprocessLaunchId launch_id) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::starting_subprocess) {
        return;
    }

    fail_task("subprocess did not register before its startup deadline");
}

void SubprocessState::poll_for_subprocess_exit(SubprocessLaunchId launch_id) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::stopping_subprocess) {
        return;
    }

    try {
        // Poll and reap the process identified by subprocess_pid_ without blocking
        const std::optional<int> wait_status =
            poll_for_child_process_exit_status(*subprocess_pid_);
        if (wait_status.has_value()) {
            finish_stop(wait_status);
            return;
        }

        // simulation_unit_actor -> simulation_unit_actor: poll the same child again; a
        // zero waitpid result means the child is still running after SIGKILL
        owner_->mail(poll_subprocess_exit_atom_v, launch_id)
            .delay(subprocess_exit_poll_interval)
            .send(owner_);
    } catch (const std::system_error& exception) {
        if (exception.code().value() == ECHILD) {
            finish_stop(std::nullopt);
            return;
        }
        throw;
    }
}

void SubprocessState::shutdown() {
    shutdown_requested_ = true;

    if (response_promise_.pending() && !result_waiting_for_stop_.has_value()) {
        keep_logs_ = paths_.has_value();
        result_waiting_for_stop_ = make_failed_task_result(
            "worker shut down before subprocess task completed", subprocess_error_prefix);
    }

    if (phase_ == SubprocessPhase::starting_subprocess && !launch_id_.has_value()) {
        return;
    }

    if (subprocess_pid_.has_value()) {
        begin_stop();
        return;
    }

    if (result_waiting_for_stop_.has_value()) {
        TaskResult task_result = std::move(*result_waiting_for_stop_);
        complete_task_response(std::move(task_result));
    }

    owner_->quit();
}

} // namespace miaow::core

namespace miaow {

SimulationUnitId SimulationUnitRegistry::add_subprocess(std::string subprocess_executable,
                                                        SubprocessLifetime lifetime) {
    if (subprocess_executable.empty()) {
        throw std::invalid_argument("subprocess executable must be non-empty");
    }

    const SimulationUnitId simulation_unit_id = next_simulation_unit_id_++;
    registrations_.emplace(simulation_unit_id,
                           SubprocessRegistration{std::move(subprocess_executable),
                                                  lifetime});
    return simulation_unit_id;
}

int run_subprocess_worker(int argc, char* argv[], SubprocessTaskHandler task_handler) {
    try {
        // argv carries IPC settings; task data arrives over CAF
        core::initialize_subprocess_caf();
        const core::SubprocessRegistrationInfo registration =
            core::parse_subprocess_registration_info(argc, argv);

        // Start a single-threaded subprocess actor system with CAF middleman communication
        caf::actor_system_config config;
        config.set("caf.scheduler.max-threads", 1u);
        config.load<caf::io::middleman>();
        caf::actor_system actor_system(config);

        // The worker remains local until CAF serializes its handle to the parent
        const caf::actor worker =
            actor_system.spawn(core::subprocess_worker_actor, std::move(task_handler));
        caf::expected<caf::actor> parent_registration_actor =
            actor_system.middleman().remote_actor(registration.host_, registration.port_);
        if (!parent_registration_actor) {
            // subprocess_main -> subprocess_worker_actor: stop after connection failure;
            // prevent actor_system from waiting for the local worker during unwinding
            caf::anon_send_exit(worker, caf::exit_reason::user_shutdown);
            throw std::runtime_error("failed to connect to parent registration actor: " +
                                     caf::to_string(parent_registration_actor.error()));
        }

        // Keep the subprocess alive until the worker has returned
        caf::scoped_actor self{actor_system};

        // subprocess_scoped_actor -> subprocess_registration_actor: register the worker;
        // sending the local handle creates its corresponding proxy in the parent system
        self->mail(core::register_subprocess_worker_atom_v, registration.launch_id_, worker)
            .send(*parent_registration_actor);

        // This blocks only the child main thread, never its CAF scheduler thread
        self->wait_for(worker);
        return 0;
    } catch (const std::exception& exception) {
        core::write_subprocess_error(exception.what());
        return 1;
    } catch (...) {
        core::write_subprocess_error("unknown exception");
        return 1;
    }
}

} // namespace miaow
