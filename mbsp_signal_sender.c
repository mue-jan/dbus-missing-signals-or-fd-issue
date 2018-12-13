
//#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
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

enum fds_used{
    SDBUS,
    TIMER,
    MAX_FD
};

int main(int argc, char *argv[])
{
    int ret = 0;

    struct pollfd fds[MAX_FD];       // sdbus == 0, timer == 1
    nfds_t nfds = MAX_FD;

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

/** switched to ppoll-approach **/
    while(1)
    {
        int i;

// (RE-)CONFIGURE STRUCT POLLFDs
    // sdbus
        fds[SDBUS].fd = sd_bus_get_fd(sdbus_obj);
        if (fds[SDBUS].fd <= 0)
            printf("issue sd_bus_get_fd\n");
        // make fd unblocking
        ret = fcntl (fds[SDBUS].fd, F_SETFL, fcntl (fds[SDBUS].fd, F_GETFL, 0) | O_NONBLOCK);
        if (ret == -1)
            printf("issue sdbus-fcntl\n");
        fds[SDBUS].events = sd_bus_get_events(sdbus_obj) | POLLIN;   // fixme: printf?!
        printf("sd_bus_get_events(sdbus_obj): %i\n", sd_bus_get_events(sdbus_obj));
        if (!fds[SDBUS].events)
            printf("issue with fds[JBUS].events\n");
        fds[SDBUS].revents = 0;
    // timer
        fds[TIMER].fd = timerfd_create (CLOCK_MONOTONIC, 0);
        if (fds[TIMER].fd < 0)
            printf("issue timerfd_create\n");
        // make fd unblocking
        ret = fcntl (fds[TIMER].fd, F_SETFL, fcntl (fds[TIMER].fd, F_GETFL, 0) | O_NONBLOCK);
        if (ret == -1) {
            return -1;
        }
        fds[TIMER].events = POLLIN;   // fixme: ?!
        fds[TIMER].revents = 0;
        ret = set_timer(fds[TIMER].fd, 1000);
        if (ret < 0)
            printf("issue set_timer\n");

// POLL-CALL
        // - 1 = block indefinitely,
        //   0 = return immediately,
        // > 0 = block for x ms
        if (poll(fds, nfds, -1) < 0)
        {
            printf("poll error!\n");
            exit(3);
        }

        for (i = 0; i < nfds; i++)
        {
            switch (fds[i].revents)
            {
                case 0:                 // no event
                    break;
                case POLLERR:
                case POLLHUP:
                    // An error has occured on this fd
                    fprintf (stderr, "epoll error - fd: %i\n", i);
                    close (fds[i].fd);
                    break;
                case POLLIN:
                    if (i == SDBUS)
                    {
                        printf("Call on sd_bus-filedescriptor!\n");
                        ret = handle_dbus_fd();
                        if (ret < 0) {
                            printf("Issue with sd_bus-filedescriptor!\n\n");
                        }
                    }
                    else if (i == TIMER)
                    {
                        printf("Call on timer-filedescriptor!\n");
                        ret = handle_timer_fd(fds[TIMER].fd, 1000);
// FIXME: calling handle_dbus_fd() manually will cause signal reception
//                        handle_dbus_fd();
                        if (ret < 0) {
                            printf("Issue with timer-filedescriptor!\n\n");
                        }
                    }
                    break;
                default:
                    printf("default case?! - fds[i].revents: %i\n", fds[i].revents);
                    break;
            }
        }
    }

    close_fd(fds[SDBUS].fd);
    close_fd(fds[TIMER].fd);

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
