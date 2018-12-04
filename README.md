# dbus-missing-signals-or-fd-issue
This project shall show an issue I faced when working with dbus-signals while listening to the dbus-fd.

An application does not receive DBUS signals via the DBUS file descriptor while
waiting for a dbus-method response.  
The basic setup: Two applications are
communicating via DBUS, using a select() loop waiting for events on the dbus-fd.  

The faulty procedure where the bug is triggered:
- Triggered by a timer event, Application A calls the dbus-method "transmit" of
  application B.  
- During processing of the “transmit”-method, application B broadcasts a
  dbus-signal on which application A is matched on.  
- Afterwards, application B sleeps for 5ms and then finishes its “transmit”-method
  by sending a response message towards application A.  
- The dbus-fd of Application A does not trigger the select() afterwards (same for
  epoll() if implemented so).  

Though  
- when triggering sd_bus_process() manually, the signal is being receved in
  application A, meaning that the signal was in fact received and queued, but
  the event was not signalled.  
- other processes, not being “blocked” by waiting for a response-message, do also
  receive the signal.  

I saw this behavior on multiple platforms, e.g. Linux kubuntu 4.4.0-137-generic
with 229-4ubuntu21.1 and Debian Buster 4.18.0-2-am64 with systemd 239-10.  
Attached you can find a simple example, including application A / B, a makefile
and the correlating *.conf-files.

To reproduce the minimal, faulty example:

*wget https://github.com/mue-jan/dbus-missing-signals-or-fd-issue/raw/master/files.zip*  
*unzip files.zip*  
*sudo cp \*.conf /etc/dbus-1/system.d/*  
*make*  
*./mbsp_signal_receiver*  
*./mbsp_signal_sender*  

**Compare behavior to source-code - expecting the reception of signals**

