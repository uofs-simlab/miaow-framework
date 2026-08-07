#include "internal.hpp"

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

// Subprocess executable resolution checks these paths in order
// 1. Task::subprocess_path_ on the task being executed
// 2. Build-tree helper when it exists
// 3. Helper under the configured installation prefix
#ifndef MIAOW_SUBPROCESS_PATH
#define MIAOW_SUBPROCESS_PATH ""
#endif
#ifndef MIAOW_INSTALLED_SUBPROCESS_PATH
#define MIAOW_INSTALLED_SUBPROCESS_PATH ""
#endif

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
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
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

// Attempt to terminate and reap a child when ordinary lifecycle cleanup cannot continue
void terminate_and_reap_subprocess(pid_t subprocess_pid) noexcept {
    if (::kill(subprocess_pid, SIGKILL) != 0 && errno != ESRCH) {
        return;
    }

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
            const auto owner = state->launch_owners_.find(launch_id);
            if (owner == state->launch_owners_.end()) {
                return;
            }

            caf::anon_mail(register_subprocess_worker_atom_v, launch_id,
                           subprocess_worker)
                .send(owner->second);
            state->launch_owners_.erase(owner);
        }};
}

namespace {

// Prefer the task-local path, then available build and installed helpers
std::string resolve_subprocess_path(const Task& task) {
    if (!task.subprocess_path_.empty()) {
        return task.subprocess_path_;
    }

    // Prefer the build helper when it exists, then use the installed helper
    const std::filesystem::path build_subprocess_path{MIAOW_SUBPROCESS_PATH};
    if (!build_subprocess_path.empty() && std::filesystem::exists(build_subprocess_path)) {
        return build_subprocess_path.string();
    }
    return MIAOW_INSTALLED_SUBPROCESS_PATH;
}

// Create the control files used by one subprocess process
SubprocessPaths make_subprocess_paths(const Task& task) {
    const std::filesystem::path control_dir = make_control_dir(task);

    // Persistent subprocess logs use a distinct name because one child may execute
    // multiple tasks before the worker stops and decides whether to retain diagnostics
    const std::string task_name = task.task_name_.empty() ? "task" : task.task_name_;
    const std::string output_name =
        task.execution_mode_ == ExecutionMode::persistent_subprocess
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
    if (executable.empty()) {
        throw std::runtime_error("subprocess executable path is not configured");
    }

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
        // Attempt to terminate and reap the created PID before reporting the local error
        terminate_and_reap_subprocess(subprocess_pid);
        throw std::system_error(destroy_error, std::generic_category(),
                                "failed to destroy subprocess file actions");
    }

    // Ownership of this exact PID transfers to the calling simulation_unit_actor
    return subprocess_pid;
}

std::optional<int> poll_subprocess_exit(pid_t subprocess_pid) {
    int wait_status = 0;
    while (true) {
        const pid_t wait_result = ::waitpid(subprocess_pid, &wait_status, WNOHANG);
        if (wait_result == subprocess_pid) {
            return wait_status;
        }
        if (wait_result == 0) {
            return std::nullopt;
        }
        if (errno == EINTR) {
            continue;
        }
        throw std::system_error(errno, std::generic_category(),
                                "failed to poll subprocess exit");
    }
}

void force_terminate_subprocess(pid_t subprocess_pid) {
    if (::kill(subprocess_pid, SIGKILL) == 0 || errno == ESRCH) {
        return;
    }
    throw std::system_error(errno, std::generic_category(),
                            "failed to force subprocess termination");
}

} // namespace

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
    terminate_and_reap_subprocess(*subprocess_pid_);
    subprocess_pid_.reset();
}

bool SubprocessState::shutdown_requested() const { return shutdown_requested_; }

// Respond when CAF reports that the actor running in the child has terminated
void SubprocessState::handle_worker_termination(
    SubprocessLaunchId launch_id, const caf::actor_addr& subprocess_worker_address,
    const caf::error& reason) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id || !subprocess_worker_ ||
        subprocess_worker_.address() != subprocess_worker_address) {
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
    if (!response_promise_.has_value()) {
        return;
    }

    response_promise_->deliver(std::move(task_result));
    response_promise_.reset();
    active_task_.reset();
    result_waiting_for_stop_.reset();
}

// Finish cleanup, then respond, relaunch retained work, or quit
void SubprocessState::finish_stop(std::optional<int> wait_status) {
    if (result_waiting_for_stop_.has_value() && paths_.has_value() &&
        result_waiting_for_stop_->status_ == TaskStatus::failed) {
        result_waiting_for_stop_->message_ = format_subprocess_failure(
            result_waiting_for_stop_->message_, *paths_, wait_status);
    }

    if (!keep_logs_ && paths_.has_value()) {
        remove_subprocess_files(*paths_);
    }

    // Clear the complete launch only after its exact PID has been reaped
    subprocess_pid_.reset();
    subprocess_worker_ = caf::actor{};
    subprocess_path_.clear();
    paths_.reset();
    keep_logs_ = false;
    launch_id_.reset();
    phase_ = SubprocessPhase::idle;

    if (result_waiting_for_stop_.has_value()) {
        TaskResult task_result = std::move(*result_waiting_for_stop_);
        complete_task_response(std::move(task_result));
        if (shutdown_requested_) {
            owner_->quit();
        }
        return;
    }

    if (active_task_.has_value() && response_promise_.has_value()) {
        if (shutdown_requested_) {
            complete_task_response(make_failed_task_result(
                "worker shut down before task execution", subprocess_error_prefix));
            owner_->quit();
        } else {
            request_launch_id();
        }
        return;
    }

    if (shutdown_requested_) {
        owner_->quit();
    }
}

// Convert one subprocess lifecycle error into a failed task result
void SubprocessState::fail_task(std::string message) {
    if (!response_promise_.has_value()) {
        return;
    }

    keep_logs_ = paths_.has_value();
    result_waiting_for_stop_ =
        make_failed_task_result(std::move(message), subprocess_error_prefix);
    begin_stop();
}

// Request child termination, then poll its PID without blocking the owner actor
void SubprocessState::begin_stop() {
    if (phase_ == SubprocessPhase::stopping_subprocess) {
        return;
    }

    // Reject future registrations before terminating the exact owned child
    if (launch_id_.has_value()) {
        caf::anon_mail(release_subprocess_launch_id_atom_v, *launch_id_)
            .send(subprocess_registration_actor_);
    }
    phase_ = SubprocessPhase::stopping_subprocess;

    if (!subprocess_pid_.has_value()) {
        finish_stop(std::nullopt);
        return;
    }

    force_terminate_subprocess(*subprocess_pid_);
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
        active_task_->execution_mode_ == ExecutionMode::persistent_subprocess &&
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
    if (!active_task_.has_value() || !launch_id_.has_value() || !subprocess_worker_) {
        fail_task("subprocess worker is unavailable");
        return;
    }

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

// Launch the retained task with an ID reserved by the registration actor
void SubprocessState::handle_launch_id(SubprocessLaunchId launch_id) {
    if (phase_ != SubprocessPhase::starting_subprocess) {
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
        const std::string subprocess_path = resolve_subprocess_path(*active_task_);
        if (subprocess_path.empty()) {
            throw std::runtime_error("subprocess executable path is not configured");
        }

        paths_ = make_subprocess_paths(*active_task_);
        keep_logs_ = false;
        subprocess_pid_ =
            spawn_subprocess_worker(subprocess_path, subprocess_registration_port_,
                                    launch_id, *paths_);
        subprocess_path_ = subprocess_path;

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
void SubprocessState::handle_launch_error(const caf::error& error) {
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
void SubprocessState::request_launch_id() {
    phase_ = SubprocessPhase::starting_subprocess;
    const caf::actor owner = owner_;
    const std::shared_ptr<SubprocessState> state = shared_from_this();

    owner_->mail(reserve_subprocess_launch_id_atom_v, owner)
        .request(subprocess_registration_actor_, caf::infinite)
        .then(
            // subprocess_registration_actor -> simulation_unit_actor: return a launch ID
            [state](SubprocessLaunchId launch_id) {
                state->handle_launch_id(launch_id);
            },
            // subprocess_registration_actor -> simulation_unit_actor: report an ID error
            [state](const caf::error& error) { state->handle_launch_error(error); });
}

caf::result<TaskResult> SubprocessState::execute(const Task& task) {
    if (response_promise_.has_value()) {
        return caf::make_error(
            caf::sec::runtime_error,
            "simulation unit worker already has an active subprocess task");
    }

    std::string subprocess_path;
    try {
        subprocess_path = resolve_subprocess_path(task);
        if (subprocess_path.empty()) {
            throw std::runtime_error("subprocess executable path is not configured");
        }
    } catch (const std::exception& exception) {
        return make_failed_task_result(exception.what(), subprocess_error_prefix);
    } catch (...) {
        return make_failed_task_result("unknown subprocess setup error",
                                       subprocess_error_prefix);
    }

    response_promise_ = owner_->make_response_promise<TaskResult>();
    active_task_ = task;

    if (phase_ == SubprocessPhase::stopping_subprocess) {
        return *response_promise_;
    }

    const bool reusable_persistent_child =
        task.execution_mode_ == ExecutionMode::persistent_subprocess &&
        subprocess_worker_ && subprocess_path_ == subprocess_path;
    if (reusable_persistent_child) {
        request_task_result();
    } else if (subprocess_pid_.has_value()) {
        begin_stop();
    } else {
        request_launch_id();
    }
    return *response_promise_;
}

void SubprocessState::accept_registration(SubprocessLaunchId launch_id,
                                          const caf::actor& subprocess_worker) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::starting_subprocess) {
        return;
    }

    subprocess_worker_ = subprocess_worker;
    const caf::actor_addr subprocess_worker_address = subprocess_worker.address();
    const std::shared_ptr<SubprocessState> state = shared_from_this();

    // subprocess_worker_actor -> simulation_unit_actor: report actor termination
    owner_->monitor(
        subprocess_worker,
        [state, launch_id, subprocess_worker_address](const caf::error& reason) {
            state->handle_worker_termination(launch_id, subprocess_worker_address,
                                             reason);
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

void SubprocessState::poll_exit(SubprocessLaunchId launch_id) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::stopping_subprocess ||
        !subprocess_pid_.has_value()) {
        return;
    }

    try {
        const std::optional<int> wait_status =
            poll_subprocess_exit(*subprocess_pid_);
        if (wait_status.has_value()) {
            finish_stop(wait_status);
            return;
        }

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

    if (response_promise_.has_value() && !result_waiting_for_stop_.has_value()) {
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

int run_subprocess_worker(int argc, char* argv[], SubprocessWorker worker_factory) {
    try {
        // argv carries IPC settings; task data arrives over CAF
        initialize_subprocess_caf();
        const SubprocessRegistrationInfo registration =
            parse_subprocess_registration_info(argc, argv);

        // Start a single-threaded subprocess actor system with CAF middleman communication
        caf::actor_system_config config;
        config.set("caf.scheduler.max-threads", 1u);
        config.load<caf::io::middleman>();
        caf::actor_system actor_system(config);

        // The worker remains local until CAF serializes its handle to the parent
        const caf::actor worker = actor_system.spawn(worker_factory);
        caf::expected<caf::actor> parent_registration_actor =
            actor_system.middleman().remote_actor(registration.host_, registration.port_);
        if (!parent_registration_actor) {
            // Stop the local worker before actor_system waits for all actors during unwind
            caf::anon_send_exit(worker, caf::exit_reason::user_shutdown);
            throw std::runtime_error("failed to connect to parent registration actor: " +
                                     caf::to_string(parent_registration_actor.error()));
        }

        // Keep the subprocess alive until the worker has returned
        caf::scoped_actor self{actor_system};

        // Sending the local handle creates its corresponding proxy in the parent system
        self->mail(register_subprocess_worker_atom_v, registration.launch_id_, worker)
            .send(*parent_registration_actor);

        // This blocks only the child main thread, never its CAF scheduler thread
        self->wait_for(worker);
        return 0;
    } catch (const std::exception& exception) {
        write_subprocess_error(exception.what());
        return 1;
    } catch (...) {
        write_subprocess_error("unknown exception");
        return 1;
    }
}

} // namespace miaow::core
