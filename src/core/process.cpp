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
#include <caf/scoped_actor.hpp>
#include <caf/type_id.hpp>

extern char** environ;

namespace miaow::core {

namespace {

// Prefixes for subprocess-related files and directories
constexpr const char* subprocess_file_prefix = "subprocess";
constexpr const char* subprocess_temp_dir_prefix = "miaow_subprocess";

// Allow up to 5 seconds for the subprocess to start up before timing out
constexpr std::chrono::seconds subprocess_startup_timeout{5};

// Polling interval for checking if the subprocess has exited
constexpr std::chrono::milliseconds subprocess_exit_poll_interval{10};

// Process-wide counter for temporary control directories
std::atomic<unsigned long long> next_control_dir_id{0};

// CAF actor messages require core and MIAOW meta objects
void initialize_subprocess_caf() {
    caf::core::init_global_meta_objects();
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
caf::behavior SubprocessRegistration::make_behavior() {
    return {// simulation_unit_actor -> subprocess_registration_actor: reserve a launch ID
            [this](reserve_subprocess_launch_id_atom,
                   const caf::actor& simulation_unit_actor) -> SubprocessLaunchId {
                const SubprocessLaunchId launch_id = next_launch_id_++;
                launch_owner_actors_.emplace(launch_id, simulation_unit_actor);
                return launch_id;
            },
            // simulation_unit_actor -> subprocess_registration_actor: release a launch ID
            [this](release_subprocess_launch_id_atom, SubprocessLaunchId launch_id) {
                launch_owner_actors_.erase(launch_id);
            },
            // subprocess_scoped_actor -> subprocess_registration_actor: register its worker
            [this](register_subprocess_worker_atom, SubprocessLaunchId launch_id,
                   const caf::actor& subprocess_worker_actor) {
                const std::unordered_map<SubprocessLaunchId, caf::actor>::iterator
                    subprocess_owner = launch_owner_actors_.find(launch_id);
                if (subprocess_owner == launch_owner_actors_.end()) {
                    return;
                }

                // subprocess_registration_actor -> simulation_unit_actor: deliver the child
                caf::anon_mail(register_subprocess_worker_atom_v, launch_id,
                               subprocess_worker_actor)
                    .send(subprocess_owner->second);
                launch_owner_actors_.erase(subprocess_owner);
            }};
}

namespace {
// Create the diagnostic files used by one child process
SubprocessPaths make_subprocess_paths(const Task& task) {
    const std::filesystem::path control_dir = make_control_dir(task);

    // One child may serve multiple operations; name its logs after the first task
    const std::string control_stem =
        std::string(subprocess_file_prefix) + "_unit_" + task.task_name_;

    SubprocessPaths paths;
    paths.stdout_path_ = control_dir / (control_stem + "_stdout.log");
    paths.stderr_path_ = control_dir / (control_stem + "_stderr.log");
    paths.temporary_control_dir_ = task.task_output_dir_.empty();
    return paths;
}

// Remove parent-subprocess control files after successful unit shutdown
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

// Task executor that delegates one task to actor-owned subprocess state
class SubprocessTaskExecutor final : public TaskExecutor {
  public:
    SubprocessTaskExecutor(SubprocessState& subprocess_state,
                           std::string subprocess_executable)
        : subprocess_state_(subprocess_state),
          subprocess_executable_(std::move(subprocess_executable)) {}

    caf::result<TaskResult> execute(const Task& task) override {
        return subprocess_state_.execute(task, subprocess_executable_);
    }

  private:
    SubprocessState& subprocess_state_;
    std::string subprocess_executable_;
};

} // namespace

SubprocessWorker::SubprocessWorker(
    std::function<caf::result<TaskResult>(const Task&)> task_handler)
    : task_handler_(std::move(task_handler)) {}

// Host the public Task-to-TaskResult handler behind the child CAF message protocol
caf::behavior SubprocessWorker::make_behavior() {
    return {// simulation_unit_actor -> subprocess_worker_actor: execute one Task;
            // caf::get_atom matches the caf::get_atom_v value sent by simulation_unit_actor
            [this](caf::get_atom, const Task& task) -> caf::result<TaskResult> {
                try {
                    return task_handler_(task);
                } catch (...) {
                    return make_failed_task_result(std::current_exception(),
                                                   "Subprocess task error");
                }
            }};
}

SubprocessState::SubprocessState(caf::event_based_actor* owner,
                                 caf::actor subprocess_registration_actor,
                                 std::uint16_t subprocess_registration_port,
                                 std::chrono::milliseconds task_request_timeout,
                                 std::chrono::milliseconds shutdown_grace_period)
    : owner_(owner),
      subprocess_registration_actor_(std::move(subprocess_registration_actor)),
      subprocess_registration_port_(subprocess_registration_port),
      task_request_timeout_(task_request_timeout),
      shutdown_grace_period_(shutdown_grace_period) {}

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

    if (phase_ == SubprocessPhase::waiting_for_task_result ||
        phase_ == SubprocessPhase::idle) {
        fail_task("subprocess worker actor terminated: " + caf::to_string(reason));
    }
}

// Deliver one operation result from the unit to its waiting job actor
void SubprocessState::complete_task_response(TaskResult task_result) {
    // simulation_unit_actor -> job_actor: deliver the retained task response
    response_promise_.deliver(std::move(task_result));
    active_task_.reset();
    result_waiting_for_stop_.reset();
}

// Release the child, then deliver any pending failure and finish shutdown
void SubprocessState::finish_stop(std::optional<int> wait_status) {
    // Complete failure diagnostics after the child process has been reaped
    if (result_waiting_for_stop_.has_value() && paths_.has_value() &&
        result_waiting_for_stop_->status_ == TaskStatus::failed) {
        result_waiting_for_stop_->message_ =
            format_subprocess_failure(result_waiting_for_stop_->message_, *paths_,
                                      wait_status);
    }

    // Remove diagnostics that no failure path requested to retain
    if (!keep_logs_ && paths_.has_value()) {
        remove_subprocess_files(*paths_);
    }

    // Release child resources before delivering the pending failure
    subprocess_pid_.reset();
    subprocess_worker_actor_ = caf::actor{};
    shutdown_deadline_.reset();
    paths_.reset();
    keep_logs_ = false;
    launch_id_.reset();
    phase_ = SubprocessPhase::idle;

    // Context loss uses the monitor path; ordinary shutdown may still settle a request
    caf::error reason;
    if (result_waiting_for_stop_.has_value()) {
        if (context_failed_) {
            reason = caf::make_error(caf::sec::request_receiver_down,
                                     result_waiting_for_stop_->message_);
            if (response_promise_.pending()) {
                response_promise_.deliver(reason);
            }
            active_task_.reset();
            result_waiting_for_stop_.reset();
        } else {
            TaskResult task_result = std::move(*result_waiting_for_stop_);
            complete_task_response(std::move(task_result));
        }
    }

    // Every stop path quits after releasing the child
    owner_->quit(std::move(reason));
}

// Mark the execution context as lost and retain its failure until cleanup finishes
void SubprocessState::fail_task(std::string message) {
    context_failed_ = true;
    shutdown_requested_ = true;
    keep_logs_ = paths_.has_value();
    result_waiting_for_stop_ =
        make_failed_task_result(std::move(message), subprocess_error_prefix);
    begin_stop();
}

// Allow orderly child shutdown before forcing termination, then reap without blocking
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

    // Worker exit lets child-owned executors clean up before the process returns
    if (subprocess_worker_actor_ && shutdown_grace_period_.count() > 0) {
        shutdown_deadline_ = std::chrono::steady_clock::now() + shutdown_grace_period_;
        caf::anon_send_exit(subprocess_worker_actor_, caf::exit_reason::user_shutdown);
    } else {
        send_sigkill_to_child_process(*subprocess_pid_);
    }

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

    if (task_result.status_ == TaskStatus::failed) {
        keep_logs_ = true;
        if (paths_) {
            task_result.message_ =
                format_subprocess_failure(task_result.message_, *paths_, std::nullopt);
        }
    }
    phase_ = SubprocessPhase::idle;
    complete_task_response(std::move(task_result));
}

// Handle an error returned by the current child actor request
void SubprocessState::handle_task_error(SubprocessLaunchId launch_id,
                                        const caf::error& error) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::waiting_for_task_result) {
        return;
    }

    if (error == caf::sec::request_timeout || error == caf::sec::request_receiver_down) {
        fail_task("subprocess actor request failed: " + caf::to_string(error));
    } else {
        phase_ = SubprocessPhase::idle;
        complete_task_response(make_failed_task_result(caf::to_string(error), "CAF error"));
    }
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

    // simulation_unit_actor -> subprocess_worker_actor: request one task result
    owner_->mail(caf::get_atom_v, std::move(task_payload))
        .request(subprocess_worker_actor_, task_request_timeout_)
        .then(
            // subprocess_worker_actor -> simulation_unit_actor: return the task result
            [this, launch_id](TaskResult task_result) {
                handle_task_result(launch_id, std::move(task_result));
            },
            // subprocess_worker_actor -> simulation_unit_actor: report a task error
            [this, launch_id](const caf::error& error) {
                handle_task_error(launch_id, error);
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
        paths_ = make_subprocess_paths(*active_task_);
        keep_logs_ = false;
        subprocess_pid_ =
            spawn_subprocess_worker(subprocess_executable_, subprocess_registration_port_,
                                    launch_id, *paths_);

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
    const caf::actor simulation_unit_actor = owner_;

    // simulation_unit_actor -> subprocess_registration_actor: reserve one launch ID
    owner_->mail(reserve_subprocess_launch_id_atom_v, simulation_unit_actor)
        .request(subprocess_registration_actor_, caf::infinite)
        .then(
            // subprocess_registration_actor -> simulation_unit_actor: return a launch ID
            [this](SubprocessLaunchId launch_id) { start_subprocess(launch_id); },
            // subprocess_registration_actor -> simulation_unit_actor: report an ID error
            [this](const caf::error& error) { handle_launch_id_request_error(error); });
}

caf::result<TaskResult> SubprocessState::execute(const Task& task,
                                                 const std::string& subprocess_executable) {
    if (response_promise_.pending() || shutdown_requested_) {
        return caf::make_error(caf::sec::runtime_error,
                               "subprocess unit is busy or shutting down");
    }

    // Retain the job actor request while the asynchronous lifecycle continues
    response_promise_ = owner_->make_response_promise<TaskResult>();
    active_task_ = task;
    subprocess_executable_ = subprocess_executable;

    if (subprocess_worker_actor_) {
        request_task_result();
    } else {
        request_subprocess_launch_id();
    }

    // The member retains the reply while caf::result carries only a delegated marker
    return response_promise_;
}

void SubprocessState::accept_registration(SubprocessLaunchId launch_id,
                                          const caf::actor& subprocess_worker_actor) {
    if (!launch_id_.has_value() || *launch_id_ != launch_id ||
        phase_ != SubprocessPhase::starting_subprocess) {
        return;
    }

    subprocess_worker_actor_ = subprocess_worker_actor;

    // subprocess_worker_actor -> simulation_unit_actor: report actor termination
    owner_->monitor(subprocess_worker_actor, [this, launch_id](const caf::error& reason) {
        handle_worker_termination(launch_id, reason);
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

        // One deadline covers the active call, executor destruction, and process exit
        if (shutdown_deadline_ && std::chrono::steady_clock::now() >= *shutdown_deadline_) {
            send_sigkill_to_child_process(*subprocess_pid_);
            shutdown_deadline_.reset();
        }

        // simulation_unit_actor -> simulation_unit_actor: poll the same child again
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
        result_waiting_for_stop_ =
            make_failed_task_result("worker shut down before subprocess task completed",
                                    subprocess_error_prefix);
    }

    if (phase_ == SubprocessPhase::starting_subprocess && !launch_id_.has_value()) {
        return;
    }

    begin_stop();
}

} // namespace miaow::core

namespace miaow {

bool TaskExecutorRegistry::is_subprocess(TaskExecutorId task_executor_id) const {
    const auto registration = registrations_.find(task_executor_id);
    return registration != registrations_.end() &&
           std::holds_alternative<TaskExecutorRegistry::SubprocessRegistration>(
               registration->second);
}

std::unique_ptr<TaskExecutor>
TaskExecutorRegistry::make_executor(TaskExecutorId task_executor_id,
                                    core::SubprocessState& subprocess_state) const {
    const auto registration = registrations_.find(task_executor_id);
    if (registration == registrations_.end()) {
        return nullptr;
    }

    if (const auto* factory =
            std::get_if<TaskExecutorRegistry::TaskExecutorFactory>(&registration->second)) {
        return (*factory)();
    }

    const auto& subprocess =
        std::get<TaskExecutorRegistry::SubprocessRegistration>(registration->second);
    return std::make_unique<core::SubprocessTaskExecutor>(subprocess_state,
                                                          subprocess.executable_);
}

TaskExecutorId TaskExecutorRegistry::add_subprocess(std::string subprocess_executable) {
    if (subprocess_executable.empty()) {
        throw std::invalid_argument("subprocess executable must be non-empty");
    }

    const TaskExecutorId task_executor_id = next_task_executor_id_++;
    registrations_.emplace(task_executor_id,
                           SubprocessRegistration{std::move(subprocess_executable)});
    return task_executor_id;
}

int run_subprocess_worker(
    int argc, char* argv[],
    std::function<caf::result<TaskResult>(const Task&)> task_handler) {
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
        const caf::actor subprocess_worker_actor =
            actor_system.spawn(caf::actor_from_state<core::SubprocessWorker>,
                               std::move(task_handler));
        caf::expected<caf::actor> subprocess_registration_actor =
            actor_system.middleman().remote_actor(registration.host_, registration.port_);
        if (!subprocess_registration_actor) {
            // subprocess_main -> subprocess_worker_actor: stop after connection failure;
            // prevent actor_system from waiting for the local worker during unwinding
            caf::anon_send_exit(subprocess_worker_actor, caf::exit_reason::user_shutdown);
            throw std::runtime_error(
                "failed to connect to parent registration actor: " +
                caf::to_string(subprocess_registration_actor.error()));
        }

        // Keep the subprocess alive until the worker has returned
        caf::scoped_actor self{actor_system};

        // subprocess_scoped_actor -> subprocess_registration_actor: register the worker;
        // sending the local handle creates its corresponding proxy in the parent system
        self->mail(core::register_subprocess_worker_atom_v, registration.launch_id_,
                   subprocess_worker_actor)
            .send(*subprocess_registration_actor);

        // This blocks only the child main thread, never its CAF scheduler thread
        self->wait_for(subprocess_worker_actor);
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
