
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/select.h>
#include <systemd/sd-bus.h>



sd_bus *sdbus_obj;
sd_bus_slot *dbus_slot;
char object_path[] = "/test/route/module_x/application_A";
char interface_name[] = "test.route.module_x.application_A";
char foreign_object_path[] = "/test/route/module_x/application_B";
char foreign_interface_name[] = "test.route.module_x.application_B";


static int handle_dbus_fd(void);
static int handle_timer_fd(int timer_fd, int time_in_ms);
static int set_timer(int timer_fd, int time_in_ms);
static void close_fd(int fd);


/**** sdbus ********************************************************************/
static int transmit(sd_bus_message *dbus_msg, void *userdata,
                      sd_bus_error *ret_error);
static int sdbus_signal_callback(sd_bus_message *m, void *signal_context,
                                    sd_bus_error *ret_error);

/**** playground **************************************************************/
void example_communication(void);



static const sd_bus_vtable object_vtable[] =
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("transmit", "i", "i", transmit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

static int missing_signals = 0;


int main(int argc, char *argv[])
{
    int ret = 0;

    static int epoll_fd = -1;
    static int sdbus_fd = -1;
    static int timer_fd = -1;

    static struct epoll_event event_array[2];
    static struct epoll_event *events;


// initialize DBus
    ret = sd_bus_open_system(&sdbus_obj);
    if (ret < 0) {
        printf("issue 1 - ret: %i\n", ret);
        exit(1);
    }
    ret = sd_bus_add_object_vtable(sdbus_obj, &dbus_slot, object_path,
                                    interface_name, object_vtable, NULL);
    if (ret < 0) {
        printf("issue 2 - ret: %i\n", ret);
        exit(1);
    }
    ret = sd_bus_request_name(sdbus_obj, interface_name, 0);
    if (ret < 0) {
        printf("issue 3 - ret: %i\n", ret);
        exit(1);
    }
    ret = sd_bus_add_match(sdbus_obj, &dbus_slot,
                            "type='signal',member='test_signal'",
                            sdbus_signal_callback, NULL);
    if (ret < 0) {
        printf("issue 4 - ret: %i\n", ret);
        exit(1);
    }


/** switched to epoll-approach **/

// epoll_fd
    epoll_fd = epoll_create1 (0);
    if (epoll_fd < 0) {
        return -1;
    }
    // don't pass epoll's fd to child processes
    ret = fcntl(epoll_fd, F_SETFD, (fcntl(epoll_fd, F_GETFD) | FD_CLOEXEC));
    if (ret < 0) {
        return -1;
    }
// dbus_fd
    sdbus_fd = sd_bus_get_fd(sdbus_obj);
    if (sdbus_obj < 0)
    {
        return -1;
    }
    // make fd unblocking
    ret = fcntl (sdbus_fd, F_SETFL, fcntl (sdbus_fd, F_GETFL, 0) | O_NONBLOCK);
    if (ret == -1) {
        return -1;
    }
    // don't pass fds to child processes
    ret = fcntl(sdbus_fd, F_SETFD, (fcntl(sdbus_fd, F_GETFD) | FD_CLOEXEC));
    if (ret < 0) {
        return -1;
    }
    // add to event-struct
    event_array[0].data.fd = sdbus_fd;
    event_array[0].events = sd_bus_get_events(sdbus_obj) | EPOLLIN | EPOLLET ;  // EDGE-TRIGGERED!
    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sdbus_fd, &event_array[0]);
    if (ret < 0) {
       return -1;
    }
// timer_fd
    timer_fd = timerfd_create (CLOCK_MONOTONIC, 0);
    if (timer_fd < 0)
    {
        return -1;
    }
    // make fd unblocking
    ret = fcntl (timer_fd, F_SETFL, fcntl (timer_fd, F_GETFL, 0) | O_NONBLOCK);
    if (ret == -1) {
        return -1;
    }
    // don't pass fds to child processes
    ret = fcntl(timer_fd, F_SETFD, (fcntl(timer_fd, F_GETFD) | FD_CLOEXEC));
    if (ret < 0) {
        return -1;
    }
    // add to event-struct
    event_array[1].data.fd = timer_fd;
    event_array[1].events = EPOLLIN | EPOLLET;           // EDGE-TRIGGERED!
    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &event_array[1]);
    if (ret < 0) {
       return -1;
    }
    ret = set_timer(timer_fd, 1000);
    if (ret < 0)
    {
        printf("issue 7\n");
        exit(1);
    }

// buffer where events are returned to
    events = calloc(2, sizeof(struct epoll_event));

    // event loop
    while (1)
    {
        int fd_ready = 0, i;

        // - 1 = block indefinitely,
        //   0 = return immediately,
        // > 0 = block for x ms
        fd_ready = epoll_wait(epoll_fd, events, 2, -1);

        for (i = 0; i < fd_ready; i++)
        {
            // ERROR
            if (    (events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                  (!(events[i].events & EPOLLIN))   ) {
                // An error has occured on this fd
                fprintf (stderr, "epoll error - fd: %i\n", events[i].data.fd);
                close (events[i].data.fd);
                continue;

            } else if (sdbus_fd == events[i].data.fd) {                 // JBUS
                printf("Call on sd_bus-filedescriptor!\n");
                ret = handle_dbus_fd();
                if (ret < 0) {
                    // any reaction?!
                    printf("Issue with sd_bus-filedescriptor!\n\n");
                }
            } else if (timer_fd == events[i].data.fd) {                // TIMER
                printf("Call on timer-filedescriptor!\n");
                ret = handle_timer_fd(timer_fd, 1000);
//                handle_dbus_fd();     // enabled --> receive the "missing signals"
                if (ret < 0) {
                    // any reaction?!
                    printf("Issue with timer-filedescriptor!\n\n");
                }
            } else {
                // nothing yet ... normally probably to handle
                // fd-reads via "read(events[i].data.fd ... )
            }
        }
    }

    close_fd(sdbus_fd);
    close_fd(timer_fd);

    return EXIT_SUCCESS;
}


/**** Poking around with dbus *************************************************/

void example_communication(void)
{
    int ret = -1;
    static int value = 0;

    sd_bus_message *dbus_msg = NULL;
    sd_bus_message *dbus_reply = NULL;
    sd_bus_error dbus_error = SD_BUS_ERROR_NULL;


    ret = sd_bus_message_new_method_call(sdbus_obj, &dbus_msg,
                                        foreign_interface_name,
                                        foreign_object_path,
                                        foreign_interface_name,
                                        "transmit");
    if (ret < 0) {
        printf("sd_bus_message_new_method_call failed\n");
        exit(1);
    }

    sd_bus_message_append_basic(dbus_msg, SD_BUS_TYPE_INT32, &value);

    ret = sd_bus_call(sdbus_obj,
                        dbus_msg,
                        100000,             // 100ms
                        &dbus_error,
                        &dbus_reply);
    if (ret < 0) {
        printf("sd_bus_call failed\n");
    } else {
        printf("\n  SENDER - sent dbus-message \"%i\"\n", value);
    }

    printf("  SENDER - missing signals: '%i'\n", missing_signals);

    if(missing_signals == 10) {
        printf("\nMISSED 10 SIGNALS - EXITING!\n\n");
        sd_bus_message_unref(dbus_msg);
        sd_bus_flush_close_unref(sdbus_obj);
        free(sdbus_obj);

        exit(EXIT_FAILURE);
    }


    sd_bus_message_unref(dbus_msg);
    printf("  SENDER - finished periodic send-event.\n");

    missing_signals++;
    value++;
}

static int sdbus_signal_callback(sd_bus_message *m, void *signal_context,
                                    sd_bus_error *ret_error)
{
    int ret = -1;
    int value = 0;

    ret = sd_bus_message_read_basic(m, SD_BUS_TYPE_INT32, &value);
    if (ret < 0) {
        printf("sd_bus_message_read_basic failed\n");
        exit(1);
    }
    printf("  SENDER - received signal \"%i\"\n", value);

    missing_signals--;

    return 0;
}

static int transmit(sd_bus_message *dbus_msg, void *userdata,
                      sd_bus_error *ret_error)
{
    int ret = -1;
    int value = 0;
    sd_bus_message *dbus_ret_msg = NULL;

    ret = sd_bus_message_read_basic(dbus_msg, SD_BUS_TYPE_INT32, &value);
    if (ret < 0) {
        printf("sd_bus_message_read_basic failed\n");
        exit(1);
    }

    value++;

    ret = sd_bus_message_append_basic(dbus_ret_msg, SD_BUS_TYPE_INT32, &value);
    if (ret < 0) {
        printf("sd_bus_message_append_basic failed\n");
        exit(1);
    }

    ret = sd_bus_send(NULL, dbus_ret_msg, NULL);
    if (ret < 0) {
        printf("sd_bus_send failed\n");
        exit(1);
    }

    sd_bus_message_unref(dbus_ret_msg);

    return 0;
}

static int handle_dbus_fd(void)
{
    int ret = 1;

    while (ret > 0) {

        ret = sd_bus_process(sdbus_obj, NULL);
        if (ret < 0)
            return -1;
    }

    return 0;
}

static int handle_timer_fd(int timer_fd, int time_in_ms)
{
    int ret = -1;

    // reset timer to given time
    ret = set_timer(timer_fd, time_in_ms);
    if (ret < 0)
    {
        printf("Had issues with handle_sd_bus()\n");
        return -1;
    }

    example_communication();

    return 0;
}

static int set_timer(int timer_fd, int time_in_ms)
{
    int ret = -1;
    struct itimerspec ts;

    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = time_in_ms / 1000;
    ts.it_value.tv_nsec = (time_in_ms % 10) * 1000000;

    ret = timerfd_settime(timer_fd, 0, &ts, NULL);

    return ret;
}

static void close_fd(int fd)
{
    close(fd);
}
