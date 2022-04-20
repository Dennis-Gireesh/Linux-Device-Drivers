#!/bin/bash
sudo echo -n "1-2:1.0" > /sys/bus/usb/drivers/usbkbd/unbind
sudo echo -n "1-2:1.0" > /sys/bus/usb/drivers/usbhid/bind