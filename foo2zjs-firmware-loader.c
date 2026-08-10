/*
 * Native macOS firmware loader for host-based HP LaserJet printers.
 *
 * Provides standalone status/one-shot modes and an IOKit event-driven daemon
 * suitable for supervision by launchd.
 *
 * License: GPL v2 or later (same as foo2zjs)
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/usb/USB.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LOG_PREFIX "foo2zjs-fw: "
#define DEFAULT_FIRMWARE_DIR "/usr/local/share/foo2zjs/firmware"
#define CUPS_USB_BACKEND "/usr/libexec/cups/backend/usb"
#define LOCK_FILE "/var/run/foo2zjs-firmware-loader.lock"
#define DAEMON_LABEL "org.foo2zjs.firmware-loader"
#define DAEMON_PLIST "/Library/LaunchDaemons/org.foo2zjs.firmware-loader.plist"
#define INSTALLED_HELPER "/usr/local/libexec/foo2zjs/foo2zjs-firmware-loader"
#define DISCOVERY_BUFFER_SIZE (64 * 1024)
#define URI_SIZE 1024
#define PROPERTY_SIZE 256
#define VERIFY_ATTEMPTS 6
#define VERIFY_DELAY_SECONDS 2
#define DEVICE_SETTLE_SECONDS 3

typedef struct firmware_device {
    uint16_t vendor_id;
    uint16_t product_id;
    const char *model;
    const char *firmware_name;
    const char *device_id_model;
    bool enabled;
} firmware_device_t;

static const firmware_device_t FirmwareDevices[] = {
    {
        0x03f0,
        0x4117,
        "HP LaserJet 1018",
        "sihp1018.dl",
        "LaserJet 1018",
        true
    }
};

typedef enum firmware_state {
    FIRMWARE_STATE_LOADED,
    FIRMWARE_STATE_NOT_LOADED,
    FIRMWARE_STATE_UNKNOWN
} firmware_state_t;

typedef struct device_status {
    bool connected;
    char product[PROPERTY_SIZE];
    char serial[PROPERTY_SIZE];
    char uri[URI_SIZE];
    firmware_state_t firmware_state;
} device_status_t;

typedef struct monitored_device {
    io_object_t notification;
    uint64_t registry_id;
} monitored_device_t;

static IONotificationPortRef NotificationPort;
static const firmware_device_t *DaemonDevice;
static const char *DaemonFirmwarePath;
static uint64_t LastRegistryID;
static time_t LastArrivalTime;

static void
log_message(const char *format, ...)
{
    va_list ap;

    fprintf(stderr, LOG_PREFIX);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static const char *
firmware_state_name(firmware_state_t state)
{
    switch (state)
    {
    case FIRMWARE_STATE_LOADED:
        return "loaded";
    case FIRMWARE_STATE_NOT_LOADED:
        return "not loaded";
    case FIRMWARE_STATE_UNKNOWN:
    default:
        return "unknown";
    }
}

static bool
contains_case_insensitive(const char *haystack, const char *needle)
{
    size_t needle_len;
    const unsigned char *p;

    if (!haystack || !needle)
        return false;
    needle_len = strlen(needle);
    if (needle_len == 0)
        return true;

    for (p = (const unsigned char *)haystack; *p; p++)
    {
        size_t i;
        for (i = 0; i < needle_len; i++)
        {
            unsigned char hc = p[i];
            unsigned char nc = (unsigned char)needle[i];
            if (!hc || tolower(hc) != tolower(nc))
                break;
        }
        if (i == needle_len)
            return true;
    }
    return false;
}

static bool
copy_cf_string_property(io_service_t service, CFStringRef key,
                        char *buffer, size_t buffer_size)
{
    CFTypeRef value;
    bool copied = false;

    value = IORegistryEntryCreateCFProperty(service, key,
                                             kCFAllocatorDefault, 0);
    if (value && CFGetTypeID(value) == CFStringGetTypeID())
    {
        copied = CFStringGetCString((CFStringRef)value, buffer,
                                    (CFIndex)buffer_size,
                                    kCFStringEncodingUTF8);
    }
    if (value)
        CFRelease(value);
    return copied;
}

static bool
populate_iokit_status(io_service_t service, const firmware_device_t *device,
                      device_status_t *status)
{
    status->connected = true;
    if (!copy_cf_string_property(service, CFSTR("USB Product Name"),
                                 status->product, sizeof(status->product)))
    {
        copy_cf_string_property(service, CFSTR(kUSBProductString),
                                status->product, sizeof(status->product));
    }
    copy_cf_string_property(service, CFSTR("USB Serial Number"),
                            status->serial, sizeof(status->serial));
    if (!status->product[0] ||
        !contains_case_insensitive(status->product, device->model))
    {
        log_message("USB product validation failed: expected '%s', got '%s'",
                    device->model,
                    status->product[0] ? status->product : "unknown");
        status->connected = false;
    }
    return status->connected;
}

static bool
find_iokit_device(const firmware_device_t *device, device_status_t *status)
{
    CFMutableDictionaryRef matching;
    CFNumberRef vendor_number;
    CFNumberRef product_number;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service;
    int vendor = device->vendor_id;
    int product = device->product_id;
    kern_return_t kr;

    matching = IOServiceMatching("IOUSBHostDevice");
    if (!matching)
    {
        log_message("unable to create IOKit USB matching dictionary");
        return false;
    }

    vendor_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType,
                                   &vendor);
    product_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType,
                                    &product);
    if (!vendor_number || !product_number)
    {
        if (vendor_number)
            CFRelease(vendor_number);
        if (product_number)
            CFRelease(product_number);
        CFRelease(matching);
        log_message("unable to create IOKit USB match values");
        return false;
    }

    CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor_number);
    CFDictionarySetValue(matching, CFSTR(kUSBProductID), product_number);
    CFRelease(vendor_number);
    CFRelease(product_number);

    kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kr != KERN_SUCCESS)
    {
        log_message("IOKit USB enumeration failed: 0x%08x", kr);
        return false;
    }

    service = IOIteratorNext(iterator);
    if (service)
    {
        populate_iokit_status(service, device, status);
        IOObjectRelease(service);
    }

    IOObjectRelease(iterator);
    return status->connected;
}

static CFMutableDictionaryRef
create_matching_dictionary(const firmware_device_t *device)
{
    CFMutableDictionaryRef matching;
    CFNumberRef vendor_number;
    CFNumberRef product_number;
    int vendor = device->vendor_id;
    int product = device->product_id;

    matching = IOServiceMatching("IOUSBHostDevice");
    if (!matching)
        return NULL;
    vendor_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType,
                                   &vendor);
    product_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType,
                                    &product);
    if (!vendor_number || !product_number)
    {
        if (vendor_number)
            CFRelease(vendor_number);
        if (product_number)
            CFRelease(product_number);
        CFRelease(matching);
        return NULL;
    }
    CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor_number);
    CFDictionarySetValue(matching, CFSTR(kUSBProductID), product_number);
    CFRelease(vendor_number);
    CFRelease(product_number);
    return matching;
}

static int
capture_usb_backend(char *buffer, size_t buffer_size)
{
    int pipes[2];
    pid_t pid;
    size_t used = 0;
    int status;

    if (pipe(pipes) != 0)
    {
        log_message("pipe failed: %s", strerror(errno));
        return -1;
    }

    pid = fork();
    if (pid < 0)
    {
        log_message("fork failed: %s", strerror(errno));
        close(pipes[0]);
        close(pipes[1]);
        return -1;
    }
    if (pid == 0)
    {
        int null_fd;

        close(pipes[0]);
        if (dup2(pipes[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(pipes[1]);
        null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0)
        {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execl(CUPS_USB_BACKEND, CUPS_USB_BACKEND, (char *)NULL);
        _exit(127);
    }

    close(pipes[1]);
    while (used + 1 < buffer_size)
    {
        ssize_t count = read(pipes[0], buffer + used,
                             buffer_size - used - 1);
        if (count > 0)
        {
            used += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }
    close(pipes[0]);
    buffer[used] = '\0';

    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
            return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return 0;
}

static bool
parse_device_uri(const char *line, char *uri, size_t uri_size)
{
    const char *start = strstr(line, "usb://");
    const char *end;
    size_t length;

    if (!start)
        return false;
    end = start;
    while (*end && !isspace((unsigned char)*end))
        end++;
    length = (size_t)(end - start);
    if (length == 0 || length >= uri_size)
        return false;
    memcpy(uri, start, length);
    uri[length] = '\0';
    return true;
}

static int
read_backend_status(const firmware_device_t *device, device_status_t *status)
{
    char output[DISCOVERY_BUFFER_SIZE];
    char *line;
    char *saveptr = NULL;

    if (access(CUPS_USB_BACKEND, X_OK) != 0)
    {
        log_message("CUPS USB backend is unavailable: %s", CUPS_USB_BACKEND);
        return -1;
    }
    if (capture_usb_backend(output, sizeof(output)) != 0)
    {
        log_message("CUPS USB discovery failed");
        return -1;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line)
    {
        if (contains_case_insensitive(line, device->device_id_model) &&
            parse_device_uri(line, status->uri, sizeof(status->uri)))
        {
            status->firmware_state =
                contains_case_insensitive(line, "FWVER:")
                    ? FIRMWARE_STATE_LOADED
                    : FIRMWARE_STATE_NOT_LOADED;
            return 0;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    status->firmware_state = FIRMWARE_STATE_UNKNOWN;
    return 0;
}

static int
collect_status(const firmware_device_t *device, device_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->firmware_state = FIRMWARE_STATE_UNKNOWN;
    find_iokit_device(device, status);
    return read_backend_status(device, status);
}

static int
validate_firmware(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
    {
        log_message("firmware file is missing: %s (%s)", path,
                    strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode) || st.st_size <= 0)
    {
        log_message("firmware path is not a non-empty regular file: %s", path);
        return -1;
    }
    return 0;
}

static int
upload_firmware(const char *uri, const char *firmware_path)
{
    int pipes[2];
    pid_t pid;
    int status;

    if (pipe(pipes) != 0)
    {
        log_message("cannot create upload log pipe: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0)
    {
        log_message("fork failed before upload: %s", strerror(errno));
        close(pipes[0]);
        close(pipes[1]);
        return -1;
    }
    if (pid == 0)
    {
        close(pipes[0]);
        if (dup2(pipes[1], STDOUT_FILENO) < 0 ||
            dup2(pipes[1], STDERR_FILENO) < 0)
            _exit(126);
        close(pipes[1]);
        if (setenv("DEVICE_URI", uri, 1) != 0)
            _exit(126);
        if (setenv("LANG", "C.UTF-8", 0) != 0)
            _exit(126);
        execl(CUPS_USB_BACKEND, CUPS_USB_BACKEND,
              "1", "root", "foo2zjs firmware", "1", "", firmware_path,
              (char *)NULL);
        _exit(127);
    }

    close(pipes[1]);
    {
        FILE *log_stream = fdopen(pipes[0], "r");
        if (log_stream)
        {
            char line[2048];
            while (fgets(line, sizeof(line), log_stream))
            {
                size_t length = strlen(line);
                while (length > 0 &&
                       (line[length - 1] == '\n' || line[length - 1] == '\r'))
                    line[--length] = '\0';
                if (length > 0)
                    log_message("USB backend: %s", line);
            }
            fclose(log_stream);
        }
        else
        {
            close(pipes[0]);
        }
    }

    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            log_message("waitpid failed during upload: %s", strerror(errno));
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        if (WIFEXITED(status))
            log_message("upload failed: USB backend exit status %d",
                        WEXITSTATUS(status));
        else
            log_message("upload failed: USB backend terminated abnormally");
        return -1;
    }
    return 0;
}

static const char *
daemon_state(void)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0)
        return access(DAEMON_PLIST, F_OK) == 0
                   ? "installed (state unknown)"
                   : "not installed";
    if (pid == 0)
    {
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0)
        {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execl("/bin/launchctl", "launchctl", "print",
              "system/" DAEMON_LABEL, (char *)NULL);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
            return "installed (state unknown)";
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return "running";
    if (access(DAEMON_PLIST, F_OK) != 0)
        return "not installed";
    if (access(INSTALLED_HELPER, X_OK) != 0)
        return "installed (helper unavailable)";
    return "installed (not running)";
}

static void
print_status(const firmware_device_t *device, const device_status_t *status,
             const char *firmware_path)
{
    printf("device: %s\n", device->model);
    printf("vid: %04x\n", device->vendor_id);
    printf("pid: %04x\n", device->product_id);
    printf("connected: %s\n", status->connected ? "yes" : "no");
    printf("product: %s\n", status->product[0] ? status->product : "unknown");
    printf("serial: %s\n", status->serial[0] ? status->serial : "unknown");
    printf("firmware: %s\n", firmware_state_name(status->firmware_state));
    printf("firmware file: %s\n", firmware_path);
    printf("device URI: %s\n", status->uri[0] ? status->uri : "unknown");
    printf("daemon: %s\n", daemon_state());
}

static int
run_once(const firmware_device_t *device, const char *firmware_path)
{
    device_status_t status;
    int lock_fd;
    int attempt;

    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (lock_fd < 0)
    {
        log_message("cannot open lock file %s: %s", LOCK_FILE,
                    strerror(errno));
        log_message("run --once with sudo");
        return 1;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
    {
        log_message("another firmware loader instance is active");
        close(lock_fd);
        return 1;
    }

    log_message("checking %s (VID=%04x PID=%04x)", device->model,
                device->vendor_id, device->product_id);
    if (collect_status(device, &status) != 0)
    {
        close(lock_fd);
        return 1;
    }
    if (!status.connected)
    {
        log_message("device is not connected");
        close(lock_fd);
        return 2;
    }
    log_message("USB device matched: product=%s serial=%s",
                status.product[0] ? status.product : "unknown",
                status.serial[0] ? status.serial : "unknown");
    log_message("current firmware state: %s",
                firmware_state_name(status.firmware_state));

    if (status.firmware_state == FIRMWARE_STATE_LOADED)
    {
        log_message("firmware already loaded; nothing to do");
        close(lock_fd);
        return 0;
    }
    if (status.firmware_state == FIRMWARE_STATE_UNKNOWN || !status.uri[0])
    {
        log_message("refusing upload because firmware state or device URI is unknown");
        close(lock_fd);
        return 3;
    }
    if (validate_firmware(firmware_path) != 0)
    {
        close(lock_fd);
        return 4;
    }

    log_message("firmware path: %s", firmware_path);
    log_message("upload started via %s", CUPS_USB_BACKEND);
    if (upload_firmware(status.uri, firmware_path) != 0)
    {
        close(lock_fd);
        return 5;
    }
    log_message("upload transport completed; verifying device state");

    for (attempt = 1; attempt <= VERIFY_ATTEMPTS; attempt++)
    {
        sleep(VERIFY_DELAY_SECONDS);
        memset(&status, 0, sizeof(status));
        status.firmware_state = FIRMWARE_STATE_UNKNOWN;
        read_backend_status(device, &status);
        log_message("verification attempt %d/%d: firmware=%s", attempt,
                    VERIFY_ATTEMPTS,
                    firmware_state_name(status.firmware_state));
        if (status.firmware_state == FIRMWARE_STATE_LOADED)
        {
            log_message("verification succeeded; firmware upload complete");
            close(lock_fd);
            return 0;
        }
    }

    log_message("verification failed after %d attempts", VERIFY_ATTEMPTS);
    close(lock_fd);
    return 6;
}

static void
device_removed(void *context, io_service_t service, natural_t message_type,
               void *message_argument)
{
    monitored_device_t *monitored = context;

    (void)service;
    (void)message_argument;
    if (message_type != kIOMessageServiceIsTerminated)
        return;

    log_message("device removed: registry-id=%llu",
                (unsigned long long)monitored->registry_id);
    if (monitored->notification)
        IOObjectRelease(monitored->notification);
    free(monitored);
}

static void
device_arrived(void *context, io_iterator_t iterator)
{
    const firmware_device_t *device = context;
    io_service_t service;

    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL)
    {
        device_status_t status;
        monitored_device_t *monitored;
        uint64_t registry_id = 0;
        time_t now = time(NULL);
        kern_return_t kr;

        memset(&status, 0, sizeof(status));
        status.firmware_state = FIRMWARE_STATE_UNKNOWN;
        if (!populate_iokit_status(service, device, &status))
        {
            IOObjectRelease(service);
            continue;
        }

        IORegistryEntryGetRegistryEntryID(service, &registry_id);
        log_message("USB device matched: model=%s VID=%04x PID=%04x serial=%s registry-id=%llu",
                    device->model, device->vendor_id, device->product_id,
                    status.serial[0] ? status.serial : "unknown",
                    (unsigned long long)registry_id);

        monitored = calloc(1, sizeof(*monitored));
        if (monitored)
        {
            monitored->registry_id = registry_id;
            kr = IOServiceAddInterestNotification(
                NotificationPort, service, kIOGeneralInterest,
                device_removed, monitored, &monitored->notification);
            if (kr != KERN_SUCCESS)
            {
                log_message("unable to register device removal notification: 0x%08x",
                            kr);
                free(monitored);
            }
        }

        if (registry_id == LastRegistryID &&
            now - LastArrivalTime < DEVICE_SETTLE_SECONDS)
        {
            log_message("duplicate USB arrival ignored: registry-id=%llu",
                        (unsigned long long)registry_id);
            IOObjectRelease(service);
            continue;
        }
        LastRegistryID = registry_id;
        LastArrivalTime = now;

        log_message("waiting %d seconds for USB printer services to settle",
                    DEVICE_SETTLE_SECONDS);
        sleep(DEVICE_SETTLE_SECONDS);
        if (run_once(device, DaemonFirmwarePath) != 0)
            log_message("automatic firmware check/upload failed");

        IOObjectRelease(service);
    }
}

static int
run_daemon(const firmware_device_t *device, const char *firmware_path)
{
    CFMutableDictionaryRef matching;
    CFRunLoopSourceRef source;
    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t kr;

    if (geteuid() != 0)
    {
        log_message("daemon mode must run as root");
        return 77;
    }

    matching = create_matching_dictionary(device);
    if (!matching)
    {
        log_message("unable to create daemon USB matching dictionary");
        return 1;
    }
    NotificationPort = IONotificationPortCreate(kIOMainPortDefault);
    if (!NotificationPort)
    {
        log_message("unable to create IOKit notification port");
        CFRelease(matching);
        return 1;
    }
    source = IONotificationPortGetRunLoopSource(NotificationPort);
    if (!source)
    {
        log_message("unable to create IOKit run-loop source");
        IONotificationPortDestroy(NotificationPort);
        NotificationPort = NULL;
        CFRelease(matching);
        return 1;
    }

    DaemonDevice = device;
    DaemonFirmwarePath = firmware_path;
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    kr = IOServiceAddMatchingNotification(
        NotificationPort, kIOFirstMatchNotification, matching,
        device_arrived, (void *)DaemonDevice, &iterator);
    if (kr != KERN_SUCCESS)
    {
        log_message("unable to register USB arrival notification: 0x%08x", kr);
        IONotificationPortDestroy(NotificationPort);
        NotificationPort = NULL;
        return 1;
    }

    log_message("daemon start: monitoring %s VID=%04x PID=%04x firmware=%s",
                device->model, device->vendor_id, device->product_id,
                firmware_path);

    /* Drain once to arm the notification and handle an already-online device. */
    device_arrived((void *)device, iterator);
    CFRunLoopRun();

    log_message("daemon stopped");
    IOObjectRelease(iterator);
    IONotificationPortDestroy(NotificationPort);
    NotificationPort = NULL;
    return 0;
}

static void
usage(FILE *fp, const char *program)
{
    fprintf(fp,
            "Usage: %s --status [--firmware path]\n"
            "       sudo %s --once [--firmware path]\n"
            "       sudo %s --daemon [--firmware path]\n"
            "\n"
            "Options:\n"
            "  --status          Show USB and firmware state without changes\n"
            "  --once            Upload firmware once if it is not loaded\n"
            "  --daemon          Monitor USB arrival events in the foreground\n"
            "  --firmware path   Override the default firmware path\n"
            "  --help            Show this help\n",
            program, program, program);
}

int
main(int argc, char *argv[])
{
    enum { MODE_NONE, MODE_STATUS, MODE_ONCE, MODE_DAEMON } mode = MODE_NONE;
    const firmware_device_t *device = &FirmwareDevices[0];
    char default_path[1024];
    const char *firmware_path;
    device_status_t status;
    int option;
    static const struct option options[] = {
        { "status", no_argument, NULL, 's' },
        { "once", no_argument, NULL, 'o' },
        { "daemon", no_argument, NULL, 'd' },
        { "firmware", required_argument, NULL, 'f' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    snprintf(default_path, sizeof(default_path), "%s/%s",
             DEFAULT_FIRMWARE_DIR, device->firmware_name);
    firmware_path = default_path;

    while ((option = getopt_long(argc, argv, "sodf:h", options, NULL)) != -1)
    {
        switch (option)
        {
        case 's':
            if (mode != MODE_NONE)
            {
                usage(stderr, argv[0]);
                return 64;
            }
            mode = MODE_STATUS;
            break;
        case 'o':
            if (mode != MODE_NONE)
            {
                usage(stderr, argv[0]);
                return 64;
            }
            mode = MODE_ONCE;
            break;
        case 'd':
            if (mode != MODE_NONE)
            {
                usage(stderr, argv[0]);
                return 64;
            }
            mode = MODE_DAEMON;
            break;
        case 'f':
            firmware_path = optarg;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 64;
        }
    }

    if (mode == MODE_NONE || optind != argc || !device->enabled)
    {
        usage(stderr, argv[0]);
        return 64;
    }

    if (mode == MODE_STATUS)
    {
        if (collect_status(device, &status) != 0)
            return 1;
        print_status(device, &status, firmware_path);
        return 0;
    }

    if (mode == MODE_ONCE)
        return run_once(device, firmware_path);
    return run_daemon(device, firmware_path);
}
