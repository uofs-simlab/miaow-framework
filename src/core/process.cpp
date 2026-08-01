#include "internal.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include <utility>
#include <vector>

#include <sys/select.h>
#include <unistd.h>

#include <boost/json.hpp>
#include <boost/process/v2/posix/bind_fd.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/system/error_code.hpp>
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
constexpr auto subprocess_connection_timeout = std::chrono::seconds(30);
constexpr std::size_t subprocess_connection_max_bytes = 4096;
std::atomic<unsigned long long> next_control_dir_id{0};

// CAF actor messages require core and MIAOW meta objects
void initialize_subprocess_caf() {
    caf::init_global_meta_objects<caf::id_block::core_module>();
    caf::init_global_meta_objects<caf::id_block::miaow_module>();
    caf::io::middleman::init_global_meta_objects();
}

// Write subprocess setup failures through the diagnostic stderr file
void write_subprocess_error(const std::string& message) {
    std::cerr << subprocess_error_prefix << ": subprocess setup error: "
              << message << "\n";
}

struct SubprocessConnection {
    // Host address where the subprocess CAF actor is published
    std::string host_;

    // TCP port where the subprocess CAF actor is published
    std::uint16_t port_{0};
};

// Close an owned descriptor and mark it unavailable
void close_file_descriptor(int& fd) {
    if (fd < 0) {
        return;
    }
    ::close(fd);
    fd = -1;
}

// RAII wrapper for pipe descriptors so exceptions close endpoints still held by the parent
struct SubprocessConnectionPipe {
    SubprocessConnectionPipe(int read_fd, int write_fd)
        : read_fd_(read_fd), write_fd_(write_fd) {}

    SubprocessConnectionPipe(const SubprocessConnectionPipe&) = delete;
    SubprocessConnectionPipe& operator=(const SubprocessConnectionPipe&) = delete;

    ~SubprocessConnectionPipe() {
        close_file_descriptor(read_fd_);
        close_file_descriptor(write_fd_);
    }

    // Parent-side read descriptor for the endpoint line, or -1 when unavailable
    int read_fd_{-1};

    // Subprocess-side write descriptor for the endpoint line, or -1 when unavailable
    int write_fd_{-1};
};

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

// Create the control files used by one subprocess process
SubprocessPaths make_subprocess_paths(const Task& task,
                                      const std::string& output_name) {
    const std::filesystem::path control_dir = make_control_dir(task);
    const std::string control_stem =
        std::string(subprocess_file_prefix) + "_" + output_name;

    SubprocessPaths paths;
    paths.stdout_path_ = control_dir / (control_stem + "_stdout.log");
    paths.stderr_path_ = control_dir / (control_stem + "_stderr.log");
    paths.temporary_control_dir_ = task.task_output_dir_.empty();
    return paths;
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

// Parse the subprocess-published CAF connection information
SubprocessConnection parse_connection_text(const std::string& connection_text) {
    boost::json::value root;
    try {
        root = boost::json::parse(connection_text);
    } catch (const std::exception& exception) {
        throw std::runtime_error("failed to parse subprocess connection pipe: " +
                                 std::string(exception.what()));
    }

    const boost::json::object& object = root.as_object();
    const std::string host = std::string(object.at("host").as_string());
    const std::uint64_t port_value = object.at("port").to_number<std::uint64_t>();
    if (host.empty()) {
        throw std::runtime_error("subprocess connection pipe host is empty");
    }
    if (port_value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("subprocess connection pipe port is out of range");
    }
    return SubprocessConnection{host, static_cast<std::uint16_t>(port_value)};
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

// Wait for one endpoint line from the subprocess
SubprocessConnection read_connection_pipe(int read_fd) {
    std::string connection_text;
    const auto deadline = std::chrono::steady_clock::now() +
                          subprocess_connection_timeout;

    while (connection_text.find('\n') == std::string::npos) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw std::runtime_error("subprocess did not write connection pipe");
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
        timeval timeout;
        timeout.tv_sec = static_cast<long>(remaining.count() / 1000000);
        timeout.tv_usec = static_cast<long>(remaining.count() % 1000000);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(read_fd, &read_fds);
        const int ready = ::select(read_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("failed to read subprocess connection pipe: " +
                                     std::string(std::strerror(errno)));
        }
        if (ready == 0) {
            throw std::runtime_error("subprocess did not write connection pipe");
        }

        char buffer[256];
        const ssize_t bytes_read = ::read(read_fd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("failed to read subprocess connection pipe: " +
                                     std::string(std::strerror(errno)));
        }
        if (bytes_read == 0) {
            break;
        }

        connection_text.append(buffer, static_cast<std::size_t>(bytes_read));
        if (connection_text.size() > subprocess_connection_max_bytes) {
            throw std::runtime_error("subprocess connection pipe exceeded size limit");
        }
    }

    const std::size_t newline_position = connection_text.find('\n');
    if (newline_position != std::string::npos) {
        connection_text.resize(newline_position);
    }
    if (connection_text.empty()) {
        throw std::runtime_error("subprocess did not write connection pipe");
    }
    return parse_connection_text(connection_text);
}

// Connect to the subprocess actor published through the connection pipe
caf::actor connect_remote_worker(caf::actor_system& actor_system,
                                 const SubprocessConnection& connection) {
    auto remote_worker =
        actor_system.middleman().remote_actor(connection.host_, connection.port_);
    if (!remote_worker) {
        throw std::runtime_error("unable to connect to subprocess actor at " +
                                 connection.host_ + ":" +
                                 std::to_string(connection.port_) + ": " +
                                 caf::to_string(remote_worker.error()));
    }
    return *remote_worker;
}

// Send one task to the subprocess actor over CAF
TaskResult request_remote_task(caf::actor_system& actor_system,
                               const caf::actor& remote_worker, const Task& task,
                               std::chrono::milliseconds subprocess_task_timeout) {
    caf::scoped_actor self{actor_system};
    std::optional<TaskResult> task_result;
    caf::error request_error;

    // Send the task via CAF and wait for a TaskResult or an error
    self->mail(caf::get_atom_v, task)
        .request(remote_worker, subprocess_task_timeout)
        .receive(
            [&task_result](TaskResult result) { task_result = std::move(result); },
            [&request_error](caf::error& error) { request_error = std::move(error); });

    if (request_error) {
        throw std::runtime_error("subprocess actor request failed: " +
                                 caf::to_string(request_error));
    }
    if (!task_result.has_value()) {
        throw std::runtime_error("subprocess actor request returned no TaskResult");
    }
    return std::move(*task_result);
}

// Append subprocess stderr context to a parent-side subprocess failure
std::string format_subprocess_failure(const std::string& message,
                                      const SubprocessPaths& paths) {
    std::string failure_message = message;
    const std::string stderr_text = read_file_prefix(paths.stderr_path_, 2000);
    if (!stderr_text.empty()) {
        failure_message += ": " + stderr_text;
    }
    return failure_message;
}

// Launch and connect a subprocess worker
void start_subprocess(caf::actor_system& actor_system, SubprocessState& state,
                      const std::string& subprocess_path,
                      const std::string& output_name, const Task& task) {
    if (subprocess_path.empty()) {
        throw std::runtime_error("subprocess executable path is not configured");
    }

    // Pass only the endpoint pipe descriptor; the Task is sent later over CAF
    const std::vector<std::string> args{
        "--connection-fd",
        std::to_string(subprocess_connection_fd),
    };

    int pipe_fds[2]{-1, -1};
    // POSIX pipe stores the read end in index 0 and the write end in index 1
    if (::pipe(pipe_fds) != 0) {
        throw std::runtime_error("failed to create subprocess connection pipe: " +
                                 std::string(std::strerror(errno)));
    }

    SubprocessConnectionPipe connection_pipe{pipe_fds[0], pipe_fds[1]};

    // Prepare state-owned diagnostics before launching the worker process
    state.io_context_.restart();
    state.paths_ = make_subprocess_paths(task, output_name);
    state.keep_logs_ = false;
    const SubprocessPaths& paths = *state.paths_;

    try {
        // Launch the worker with stdout and stderr redirected to diagnostics
        state.subprocess_.emplace(
            state.io_context_, subprocess_path, args,
            boost::process::v2::process_stdio{{}, paths.stdout_path_,
                                              paths.stderr_path_},
            boost::process::v2::posix::bind_fd(subprocess_connection_fd,
                                               connection_pipe.write_fd_));
        close_file_descriptor(connection_pipe.write_fd_);

        // Read the published CAF endpoint from the startup pipe
        const SubprocessConnection connection =
            read_connection_pipe(connection_pipe.read_fd_);
        close_file_descriptor(connection_pipe.read_fd_);

        // Store the remote worker actor for later task requests
        state.remote_worker_ = connect_remote_worker(actor_system, connection);
        state.subprocess_path_ = subprocess_path;
        state.connection_host_ = connection.host_;
        state.connection_port_ = connection.port_;

        const std::string task_label =
            task.task_name_.empty() ? "task" : task.task_name_;
        std::cerr << "[" << task_label
                  << "] MIAOW subprocess: connected to actor at "
                  << connection.host_ << ":" << connection.port_ << "\n";
    } catch (const std::exception& exception) {
        const SubprocessPaths failure_paths = *state.paths_;
        std::string message = exception.what();

        // Preserve launch diagnostics when connection setup fails
        close_file_descriptor(connection_pipe.read_fd_);
        close_file_descriptor(connection_pipe.write_fd_);
        state.keep_logs_ = true;
        stop_subprocess(state);
        throw std::runtime_error(format_subprocess_failure(message, failure_paths));
    }
}

} // namespace

TaskResult execute_subprocess(caf::actor_system& actor_system, SubprocessState& state,
                              const Task& task) {
    try {
        // Prefer the task-local path, then available build and installed helpers
        std::string subprocess_path = task.subprocess_path_;
        if (subprocess_path.empty()) {
            const std::filesystem::path build_subprocess_path{MIAOW_SUBPROCESS_PATH};
            subprocess_path = std::filesystem::exists(build_subprocess_path)
                                  ? build_subprocess_path.string()
                                  : std::string(MIAOW_INSTALLED_SUBPROCESS_PATH);
        }

        // Check if this worker's subprocess is already running the correct executable
        const bool path_changed =
            state.subprocess_.has_value() && state.subprocess_path_ != subprocess_path;
        boost::system::error_code ignored_error;
        const bool process_running =
            state.subprocess_.has_value() && state.subprocess_->running(ignored_error);

        // A new process is needed if any of the following is true:
        // 1. The subprocess executable path has changed since the last task
        // 2. The subprocess executable is not running
        // 3. The execution mode requires a new subprocess for each task
        if (task.execution_mode_ == ExecutionMode::single_task_subprocess ||
            path_changed || !process_running) {
            stop_subprocess(state);
            const std::string task_name =
                task.task_name_.empty() ? "task" : task.task_name_;
            const std::string output_name =
                task.execution_mode_ == ExecutionMode::persistent_subprocess
                    ? "persistent_" + task_name : task_name;
            start_subprocess(actor_system, state, subprocess_path, output_name, task);
        } else if (task.execution_mode_ == ExecutionMode::persistent_subprocess) {
            const std::string task_label =
                task.task_name_.empty() ? "task" : task.task_name_;
            std::cerr << "[" << task_label
                      << "] MIAOW subprocess: reusing actor at "
                      << state.connection_host_ << ":" << state.connection_port_ << "\n";
        }

        // Send absolute output paths so subprocess writes do not depend on cwd
        Task task_payload = task;
        if (!task_payload.task_output_dir_.empty()) {
            task_payload.task_output_dir_ =
                std::filesystem::absolute(task_payload.task_output_dir_)
                    .lexically_normal()
                    .string();
        }

        // Send one task to the active remote worker
        TaskResult task_result = request_remote_task(
            actor_system, state.remote_worker_, task_payload,
            state.subprocess_task_timeout_);

        // Preserve logs and reset the worker subprocess after a failed task
        if (task_result.status_ == TaskStatus::failed) {
            state.keep_logs_ = true;
            stop_subprocess(state);
        } else if (task.execution_mode_ == ExecutionMode::single_task_subprocess) {
            // Successful task-scoped execution stops its one-task subprocess
            stop_subprocess(state);
        }
        return task_result;
    } catch (const std::exception& exception) {
        std::string message = exception.what();

        // Convert parent-side subprocess errors into failed task results
        if (state.paths_) {
            const SubprocessPaths paths = *state.paths_;
            state.keep_logs_ = true;
            stop_subprocess(state);
            message = format_subprocess_failure(message, paths);
        }
        return make_failed_task_result(message, subprocess_error_prefix);
    } catch (...) {
        // Keep diagnostics when an unknown parent-side error escapes
        if (state.paths_) {
            state.keep_logs_ = true;
            stop_subprocess(state);
        }
        return make_failed_task_result("unknown exception", subprocess_error_prefix);
    }
}

void stop_subprocess(SubprocessState& state) {
    // Clear stale actor handles when there is no active process
    if (!state.subprocess_.has_value()) {
        state.remote_worker_ = caf::actor{};
        state.subprocess_path_.clear();
        state.paths_.reset();
        state.keep_logs_ = false;
        return;
    }

    // Terminate the owned process before clearing state
    boost::system::error_code ignored_error;
    if (state.subprocess_->running(ignored_error)) {
        state.subprocess_->terminate(ignored_error);
    }

    // Remove diagnostics only after clean subprocess shutdown
    const bool remove_logs = !state.keep_logs_ && state.paths_.has_value();
    state.subprocess_->wait(ignored_error);

    if (remove_logs) {
        remove_subprocess_files(*state.paths_);
    }

    // Clear all parent-side handles after shutdown
    state.subprocess_.reset();
    state.remote_worker_ = caf::actor{};
    state.subprocess_path_.clear();
    state.connection_host_.clear();
    state.connection_port_ = 0;
    state.paths_.reset();
    state.keep_logs_ = false;
}

SubprocessState::~SubprocessState() {
    // Preserve diagnostics when cleanup happens from state destruction
    keep_logs_ = true;
    stop_subprocess(*this);
}

static int parse_subprocess_connection_fd(int argc, char* argv[]) {
    std::string connection_fd;

    // argv[0] is the executable path; the remaining arguments configure IPC
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--connection-fd") {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + argv[index]);
            }
            ++index;
            connection_fd = argv[index];
        } else {
            throw std::runtime_error("unknown argument '" + arg + "'");
        }
    }

    if (connection_fd.empty()) {
        throw std::runtime_error("missing --connection-fd");
    }

    char* parse_end = nullptr;
    errno = 0;
    const long fd_value = std::strtol(connection_fd.c_str(), &parse_end, 10);
    if (errno != 0 || parse_end == connection_fd.c_str() || *parse_end != '\0' ||
        fd_value < 3 || fd_value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("invalid --connection-fd value '" + connection_fd + "'");
    }
    return static_cast<int>(fd_value);
}

void write_subprocess_connection_pipe(int connection_fd, std::uint16_t port) {
    boost::json::object object;
    object["host"] = subprocess_connection_host;
    object["port"] = port;

    // Write one newline-delimited endpoint message for the parent process
    const std::string connection_text = boost::json::serialize(object) + "\n";
    const char* data = connection_text.data();
    std::size_t remaining = connection_text.size();
    while (remaining > 0) {
        const ssize_t bytes_written = ::write(connection_fd, data, remaining);
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("failed to write connection pipe: " +
                                     std::string(std::strerror(errno)));
        }
        data += bytes_written;
        remaining -= static_cast<std::size_t>(bytes_written);
    }
}

int run_subprocess_worker(int argc, char* argv[], SubprocessWorker worker_factory) {
    try {
        // argv carries IPC settings; task data arrives over CAF
        initialize_subprocess_caf();
        const int connection_fd = parse_subprocess_connection_fd(argc, argv);

        // Start a single-threaded subprocess actor system with CAF middleman communication
        caf::actor_system_config config;
        config.set("caf.scheduler.max-threads", 1u);
        config.load<caf::io::middleman>();
        caf::actor_system actor_system(config);

        // Publish the worker and expose its endpoint to the parent
        const caf::actor worker = actor_system.spawn(worker_factory);
        auto port = actor_system.middleman().publish(
            worker, 0, subprocess_connection_host);
        if (!port) {
            throw std::runtime_error("failed to publish subprocess actor: " +
                                     caf::to_string(port.error()));
        }

        // Return the published endpoint to the parent through the inherited pipe
        write_subprocess_connection_pipe(connection_fd, *port);
        ::close(connection_fd);

        // Keep the subprocess alive until the worker has returned
        caf::scoped_actor self{actor_system};
        self->wait_for(worker);
        return 0;
    } catch (const std::exception& exception) {
        // The connection pipe contract may not be available at this point
        write_subprocess_error(exception.what());
        return 1;
    } catch (...) {
        // The absence of a connection pipe tells the parent process setup failed
        write_subprocess_error("unknown exception");
        return 1;
    }
}

} // namespace miaow::core

namespace miaow {

ExecutionContext::ExecutionContext(caf::actor_system& actor_system,
                                   core::SubprocessState& active_subprocess)
    : actor_system_(&actor_system), active_subprocess_(&active_subprocess) {}

// Execute through the worker-owned subprocess slot
TaskResult ExecutionContext::execute_subprocess(const Task& task) const {
    return core::execute_subprocess(*actor_system_, *active_subprocess_, task);
}

} // namespace miaow
