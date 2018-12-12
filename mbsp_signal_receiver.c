
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <systemd/sd-bus.h>



sd_bus *sdbus_obj;
sd_bus_slot *dbus_slot;
char object_path[] = "/test/route/module_x/application_B";
char interface_name[] = "test.route.module_x.application_B";


static int handle_dbus_fd(void);
static void close_fd(int fd);


/**** sdbus ********************************************************************/
static int transmit(sd_bus_message *dbus_msg, void *userdata,
                      sd_bus_error *ret_error);



static const sd_bus_vtable object_vtable[] =
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("transmit", "i", "i", transmit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};



int main(int argc, char *argv[])
{
    int ret = 0;

    fd_set readfds;
    int nfds, sdbus_fd, ready;
    FD_ZERO(&readfds);


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

    sdbus_fd = sd_bus_get_fd(sdbus_obj);
    if (sdbus_fd < 0)
    {
        printf("issue 5\n");
        exit(1);
    }
    nfds = sdbus_fd +1;

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sdbus_fd, &readfds);

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
        } else {                    // timeout
            printf("select timed out (5s).\nBye\n");
            break;
        }

//        // SEND SIGNAL WHICH WILL   N O T   BE LOST
//        {
//            sd_bus_message *dbus_signal = NULL;
//            int value = 4;
//            ret = sd_bus_message_new_signal(sdbus_obj, &dbus_signal, object_path,
//                                            interface_name, "test_signal");
//            if (ret < 0) {
//                printf("sd_bus_message_new_signal failed\n");
//                exit(1);
//            }
//
//            ret = sd_bus_message_append_basic(dbus_signal, SD_BUS_TYPE_INT32,
//                                                &value);
//            if (ret < 0) {
//                printf("sd_bus_message_append_basic failed\n");
//                exit(1);
//            }
//
//            ret = sd_bus_send(NULL, dbus_signal, NULL);
//            if (ret < 0) {
//                printf("sd_bus_send failed\n");
//                exit(1);
//            }
//
//            sd_bus_message_unref(dbus_signal);
//
//            printf("\n\nSignal sent!\n\n");
//        }
    }

    close_fd(sdbus_fd);

    return EXIT_SUCCESS;
}


static int transmit(sd_bus_message *dbus_msg, void *userdata,
                      sd_bus_error *ret_error)
{
    int ret = -1;
    int value = 0;
    sd_bus_message *dbus_signal = NULL;
    sd_bus_message *dbus_ret_msg = NULL;

    ret = sd_bus_message_new_method_return(dbus_msg, &dbus_ret_msg);
    if (ret < 0) {
        printf("sd_bus_message_new_method_return failed\n");
        exit(1);
    }
    ret = sd_bus_message_read_basic(dbus_msg, SD_BUS_TYPE_INT32, &value);
    if (ret < 0) {
        printf("sd_bus_message_read_basic failed\n");
        exit(1);
    }

    printf("\n    RECEIVER - received dbus-message \"%i\"\n", value);
    printf("               Responding with signal within SD_BUS_METHOD\n");

    // SEND SIGNAL WHICH WILL BE LOST
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

    printf("    RECEIVER - sent signal \"%i\" to caller, going to sleep ...\n",
                                                                        value);
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

static void close_fd(int fd)
{
    close(fd);
}
