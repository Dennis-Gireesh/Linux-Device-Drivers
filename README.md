# Linux-Device-Drivers
Advanced Systems Programming

## Problem Statement of Character Device Driver

Write a simple character device driver which supports read, write, open, close, lseek and ioctl system calls by the user space program

### Steps to run the driver/module

- Copy the files char_driver.c, Makefile and userapp2.c to a virtual Linux machine and follow the below steps:

- Compile driver module : $ make

   - Load module : $ sudo insmod char_driver.ko NUM_DEVICES=<num_devices>

    - Test driver :
        Compile userapp : $ make app
        Run userapp : $ sudo ./userapp <device_number> where device_number identifies the id number of the device to be tested.
        Note : userapp has to be executed with sudo privilege as the device files in /dev/ are created in the driver with root privileges.

   -  Unload module : $ sudo rmmod char_driver

The binaries provided are the result of executing the above instructions. They can be used to run the module.
