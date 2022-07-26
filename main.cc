#include <iostream>
#include <sched.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <fstream>

#define CGROUP_FOLDER "/sys/fs/cgroup/pids/container/"
#define concat(a,b) (a"" b)

int go(int status, const char *msg) {
    if(status == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return status;
}

char *stack_memory() {
    const int stackSize = 65536; // 64KB
    auto *stack = new (std::nothrow) char[stackSize];

    if(stack == nullptr) {
        printf("Cannot allocate memory \n");
        exit(EXIT_FAILURE);
    }

    return stack+stackSize;
}

template <typename...T>
int run(T... params) {
    char *args[] = {(char*)params..., (char*)0}; // array of commands
    return execvp(args[0], args);
}

template <typename Function>
void clone_process(Function&& function, int flags) {
    auto pid = go( clone(function, stack_memory(), flags, 0), "clone" );

    wait(nullptr);
}

void setup_root(const char* folder) {
    chroot(folder); // set root directory - decrease visibility
    chdir("/");
}

void setHostName(std::string hostname) {
  sethostname(hostname.c_str(), hostname.size());
}

void setup_variables() {
    clearenv();
    setenv("TERM", "xterm-256color", 0);
    setenv("PATH", "/bin/:/sbin/:usr/bin:/usr/sbin", 0);
}

// update file with your value
void write_rule(const char* path, const char* value) {
    int fp = open(path, O_WRONLY | O_APPEND );
    write(fp, value, strlen(value));
    close(fp);
}

// limit number of processes that can be created inside container
void limitProcessCreation() {
    mkdir( CGROUP_FOLDER, S_IRUSR | S_IWUSR);

    const char* pid = std::to_string(getpid()).c_str();

    write_rule(concat(CGROUP_FOLDER, "cgroup.procs"), pid);
    write_rule(concat(CGROUP_FOLDER, "notify_on_release"), "1");
    write_rule(concat(CGROUP_FOLDER, "pids.max"), "5");
    // max of 5 processes can be created by our child process
}

int child(void *args) {
    limitProcessCreation();
    printf("child process: %d\n", getpid());
    setHostName("container1"); // change hostname
    setup_variables();

    setup_root("./root");

    mount("proc", "/proc", "proc", 0, 0); // mount proc file system

    auto runThis = [](void *args) -> int { run("/bin/sh"); };

    clone_process(runThis, SIGCHLD);

    umount("/proc");
    return EXIT_SUCCESS;
}

int main() {
    printf("Hello from parent process! \n");
    printf("parent %d\n", getpid());

    clone_process(child, CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD );
    // CLONE_NEWPID - isolate shell
    // CLONE_NEWUTS - clone global namespace (UTS)
    // CLONE_NEWNS - get copy of parent's mounted filesystem, changes only reflect in child

    return EXIT_SUCCESS;
}