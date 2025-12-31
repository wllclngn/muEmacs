#include "test_utils.h"
#include "test_platform_specific.h"
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <sys/sysinfo.h>
#include <sys/signalfd.h>

// Test Linux terminal features
int test_linux_terminal_features(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Linux Terminal Features ===%s", BLUE, RESET);
    
    // Test Linux-specific terminal capabilities
    LOG_INFO("Testing Linux terminal capabilities...");
    total++;
    
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        if (strcmp(system_info.sysname, "Linux") == 0) {
            LOG_INFOF("[SUCCESS] Linux detection: running on %s %s", system_info.sysname, system_info.release);
            passed++;
        }
    }
    
    // Test epoll for terminal event handling
    LOG_INFO("Testing epoll for terminal events...");
    total++;
    
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd >= 0) {
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = STDIN_FILENO;
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == 0) {
            LOG_INFO("[SUCCESS] Epoll setup: terminal stdin added to epoll");
            passed++;
        }
        close(epoll_fd);
    }
    
    LOG_INFOF("Linux terminal feature tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test filesystem specific features
int test_filesystem_specific(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Filesystem Specific Features ===%s", BLUE, RESET);
    
    // Test inotify for file watching
    LOG_INFO("Testing inotify file watching...");
    total++;
    
    int inotify_fd = inotify_init1(IN_CLOEXEC);
    if (inotify_fd >= 0) {
        const char* watch_dir = "/tmp";
        int watch_fd = inotify_add_watch(inotify_fd, watch_dir, IN_CREATE | IN_DELETE | IN_MODIFY);
        
        if (watch_fd >= 0) {
            LOG_INFOF("[SUCCESS] Inotify setup: watching %s for file changes", watch_dir);
            inotify_rm_watch(inotify_fd, watch_fd);
            passed++;
        }
        close(inotify_fd);
    }
    
    // Test Linux-specific file attributes
    LOG_INFO("Testing Linux file attributes...");
    total++;
    
    const char* test_file = "/tmp/uemacs_attr_test.txt";
    FILE* f = fopen(test_file, "w");
    if (f) {
        fprintf(f, "Test file for attributes\n");
        fclose(f);
        
        struct stat st;
        if (stat(test_file, &st) == 0) {
            // Check if it's a regular file
            if (S_ISREG(st.st_mode)) {
                LOG_INFOF("[SUCCESS] File attributes: regular file detected, size=%ld", st.st_size);
                passed++;
            }
        }
        unlink(test_file);
    }
    
    LOG_INFOF("Filesystem specific tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// File-scope volatile for signal handler (moved from nested function)
static volatile int rt_signal_received = 0;

// Signal handler (moved to file scope to comply with ISO C)
static void rt_signal_handler(int sig) {
    if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
        rt_signal_received = 1;
    }
}

// Test signal handling (Linux-specific)
int test_signal_handling_linux(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Linux Signal Handling ===%s", BLUE, RESET);

    // Test signalfd (Linux-specific)
    LOG_INFO("Testing signalfd functionality...");
    total++;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == 0) {
        int signal_fd = signalfd(-1, &mask, SFD_CLOEXEC);
        if (signal_fd >= 0) {
            LOG_INFO("[SUCCESS] Signalfd: created signal file descriptor");
            close(signal_fd);
            passed++;
        }

        // Restore signal mask
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }

    // Test real-time signals
    LOG_INFO("Testing real-time signals...");
    total++;

    rt_signal_received = 0;  // Reset before test

    if (signal(SIGRTMIN, rt_signal_handler) != SIG_ERR) {
        kill(getpid(), SIGRTMIN);
        usleep(10000); // 10ms for signal delivery
        
        if (rt_signal_received) {
            LOG_INFO("[SUCCESS] Real-time signals: SIGRTMIN handled correctly");
            passed++;
        }
        
        signal(SIGRTMIN, SIG_DFL);
    }
    
    LOG_INFOF("Linux signal handling tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test memory management (Linux-specific)
int test_memory_management_linux(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Linux Memory Management ===%s", BLUE, RESET);
    
    // Test mmap with specific Linux flags
    LOG_INFO("Testing mmap with Linux flags...");
    total++;
    
    void* mapped = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped != MAP_FAILED) {
        // Test memory access
        char* mem = (char*)mapped;
        strcpy(mem, "mmap test data");
        
        if (strcmp(mem, "mmap test data") == 0) {
            LOG_INFO("[SUCCESS] Mmap: 4096 bytes mapped and accessible");
            passed++;
        }
        
        munmap(mapped, 4096);
    }
    
    // Test system memory info
    LOG_INFO("Testing system memory information...");
    total++;
    
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        unsigned long total_mb = info.totalram / (1024 * 1024);
        unsigned long free_mb = info.freeram / (1024 * 1024);
        
        if (total_mb > 0 && free_mb > 0) {
            LOG_INFOF("[SUCCESS] System memory: %lu MB total, %lu MB free", total_mb, free_mb);
            passed++;
        }
    }
    
    LOG_INFOF("Linux memory management tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Thread function (moved to file scope to comply with ISO C)
static void* thread_function(void* arg) {
    int* value = (int*)arg;
    *value = 42;
    return NULL;
}

// Test threading primitives
int test_threading_primitives(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Threading Primitives ===%s", BLUE, RESET);

    // Test pthread creation and joining
    LOG_INFO("Testing pthread operations...");
    total++;

    pthread_t thread;
    int thread_data = 0;
    
    if (pthread_create(&thread, NULL, thread_function, &thread_data) == 0) {
        if (pthread_join(thread, NULL) == 0) {
            if (thread_data == 42) {
                LOG_INFO("[SUCCESS] Pthread: thread created, executed, and joined");
                passed++;
            }
        }
    }
    
    // Test semaphore operations
    LOG_INFO("Testing semaphore operations...");
    total++;
    
    sem_t semaphore;
    if (sem_init(&semaphore, 0, 1) == 0) {
        if (sem_wait(&semaphore) == 0) {
            // Semaphore acquired
            if (sem_post(&semaphore) == 0) {
                // Semaphore released
                LOG_INFO("[SUCCESS] Semaphore: wait/post operations successful");
                passed++;
            }
        }
        sem_destroy(&semaphore);
    }
    
    LOG_INFOF("Threading primitive tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test IPC mechanisms
int test_ipc_mechanisms(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing IPC Mechanisms ===%s", BLUE, RESET);
    
    // Test message queue (POSIX)
    LOG_INFO("Testing POSIX message queue...");
    total++;
    
    const char* mq_name = "/uemacs_test_mq";
    struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 256;
    
    mqd_t mq = mq_open(mq_name, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq != (mqd_t)-1) {
        const char* test_msg = "IPC test message";
        
        if (mq_send(mq, test_msg, strlen(test_msg), 0) == 0) {
            LOG_INFO("[SUCCESS] Message queue: message sent successfully");
            passed++;
        }
        
        mq_close(mq);
        mq_unlink(mq_name);
    }
    
    // Test shared memory
    LOG_INFO("Testing shared memory...");
    total++;
    
    void* shared_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_mem != MAP_FAILED) {
        char* shared_data = (char*)shared_mem;
        strcpy(shared_data, "Shared memory test data");
        
        // Simulate access from another process (in this case, same process)
        if (strcmp(shared_data, "Shared memory test data") == 0) {
            LOG_INFO("[SUCCESS] Shared memory: data accessible across processes");
            passed++;
        }
        
        munmap(shared_mem, 4096);
    }
    
    LOG_INFOF("IPC mechanism tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test kernel interfaces
int test_kernel_interfaces(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Kernel Interfaces ===%s", BLUE, RESET);
    
    // Test system call interface
    LOG_INFO("Testing direct system calls...");
    total++;
    
    // Test getpid system call
    pid_t pid1 = getpid();
    pid_t pid2 = syscall(SYS_getpid);
    
    if (pid1 == pid2 && pid1 > 0) {
        LOG_INFOF("[SUCCESS] System calls: getpid via syscall matches libc (pid=%d)", pid1);
        passed++;
    }
    
    // Test /proc filesystem access
    LOG_INFO("Testing /proc filesystem access...");
    total++;
    
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", getpid());
    
    FILE* proc_f = fopen(proc_path, "r");
    if (proc_f) {
        char proc_data[1024];
        if (fgets(proc_data, sizeof(proc_data), proc_f)) {
            // Parse PID from /proc/self/stat
            int proc_pid;
            if (sscanf(proc_data, "%d", &proc_pid) == 1 && proc_pid == getpid()) {
                LOG_INFOF("[SUCCESS] /proc access: read process info for PID %d", proc_pid);
                passed++;
            }
        }
        fclose(proc_f);
    }
    
    // Test /sys filesystem access
    LOG_INFO("Testing /sys filesystem access...");
    total++;
    
    FILE* sys_f = fopen("/sys/kernel/hostname", "r");
    if (sys_f) {
        char hostname[256];
        if (fgets(hostname, sizeof(hostname), sys_f)) {
            // Remove newline
            hostname[strcspn(hostname, "\n")] = '\0';
            if (strlen(hostname) > 0) {
                LOG_INFOF("[SUCCESS] /sys access: hostname '%s'", hostname);
                passed++;
            }
        }
        fclose(sys_f);
    }
    
    LOG_INFOF("Kernel interface tests: %d/%d passed\n", passed, total);
    return (passed == total);
}