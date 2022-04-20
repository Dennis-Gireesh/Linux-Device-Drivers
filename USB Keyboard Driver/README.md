Author- Venkata Sai Gireesh Chamarthi

Problem Statement

To change the Linux USB Keyboard driver (see http://lxr.free-electrons.com/source/drivers/hid/usbhid/usbkbd.c) to change the way the CAPSLOCK led is 
turned on.

Introduce two new modes of operation for CAPSLOCK and NUMLOCK in the Linux USB keyboard driver (usbkbd.c).
Mode 1: Default behavior of the driver
Mode 2: This mode is activated when NUMLOCK is pressed and CAPSLOCK is not on. When transitioning to Mode 2, the CAPSLOCK led will be turned on automatically. In Mode 2, the functionality of CAPSLOCK is reversed. i.e. when CAPSLOCK led is on the charaters typed will be in lower case and when CAPSLOCK led is off the characters typed will be in upper case.
Note: In Mode 2, the driver should leave the CAPSLOCK led status in a way that will be compatible with Mode 1.



Steps to run the usbkbd driver/module

Compile driver module : $ make

Load module : $ sudo insmod usbkbd.ko

Connect the USB Keyboard and transfer the control to Virtual Machine

To link the keyboard to our usbkbd module by unlinking from usbhid(generic module) run the following script(change the keyboard identifier).
    $ sudo sh redirect_to_usbkbd.sh 

Test the modes of operation and check the kerenel log(dmesg -T) to see the mode transitions.

To unlink th keyboard from usbkbd and link to default generic usbhid driver run the following script(change the keyboard identifier).
    $ sudo sh redirect_to_generichid.sh

Unload module : $ sudo rmmod char_driver
