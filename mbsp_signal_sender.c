
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
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

    fd_set readfds;
    int nfds, sdbus_fd, timer_fd, ready;


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

    sdbus_fd = sd_bus_get_fd(sdbus_obj);
    if (sdbus_fd < 0)
    {
        printf("issue 5\n");
        exit(1);
    }
    nfds = sdbus_fd +1;

    timer_fd = timerfd_create (CLOCK_MONOTONIC, 0);
    if (timer_fd < 0)
    {
        printf("issue 6\n");
        exit(1);
    }
    ret = set_timer(timer_fd, 1000);
    if (ret < 0)
    {
        printf("issue 7\n");
        exit(1);
    }

    if (timer_fd > sdbus_fd) {
        nfds = timer_fd +1;
    }


    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sdbus_fd, &readfds);
        FD_SET(timer_fd, &readfds);

        ready = select(nfds, &readfds, NULL, NULL, NULL);
        if (ready == -1)            // error
        {
            printf("issue 8\n");
            exit(1);
        } else if (ready) {         // data available
            if (FD_ISSET(sdbus_fd, &readfds)) {      // dbus-fd ready
                ret = handle_dbus_fd();
                if (ret < 0)
                {
                    // any reaction?!
                    printf("Issue with sdbus-routine!\n");
                }
            }
            if (FD_ISSET(timer_fd, &readfds)) {      // timer-fd ready
                ret = handle_timer_fd(timer_fd, 1000);
                if (ret < 0)
                {
                    // any reaction?!
                    printf("Issue with timer-routine\n\n");
                }
            }
        } else {                    // timeout
            printf("select timed out (5s).\nBye\n");
            break;
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
    sd_bus_message *dbus_signal = NULL;

    ret = sd_bus_message_read_basic(dbus_msg, SD_BUS_TYPE_INT32, &value);
    if (ret < 0) {
        printf("sd_bus_message_read_basic failed\n");
        exit(1);
    }

    value++;

    printf("    RECEIVER - dbus messages arrived, sending signal ...\n");

    // SEND SIGNAL
    {
        ret = sd_bus_message_new_signal(sdbus_obj, &dbus_signal, object_path,
                                        interface_name, "test_signal");
        if (ret < 0) {
            printf("sd_bus_message_new_signal failed\n");
            exit(1);
        }

        ret = sd_bus_message_append_basic(dbus_signal, SD_BUS_TYPE_INT32,
                                            &value);
        if (ret < 0) {
            printf("sd_bus_message_append_basic failed\n");
            exit(1);
        }

        ret = sd_bus_send(NULL, dbus_signal, NULL);
        if (ret < 0) {
            printf("sd_bus_send failed\n");
            exit(1);
        }

        sd_bus_message_unref(dbus_signal);
    }

    printf("    RECEIVER - signal sent, going to sleep ...\n");
    usleep(5000);
    printf("    RECEIVER - slept for 5ms, sending dbus-msg response ...\n");

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
