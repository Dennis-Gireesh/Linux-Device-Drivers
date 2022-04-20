// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  USB HIDBP Keyboard support
 */

/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

/*
 * Version Information
 */
#define DRIVER_VERSION ""
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB HID Boot Protocol keyboard driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

static const unsigned char usb_kbd_keycode[256] = {
    0, 0, 0, 0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
    50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 2, 3,
    4, 5, 6, 7, 8, 9, 10, 11, 28, 1, 14, 15, 57, 12, 13, 26,
    27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 87, 88, 99, 70, 119, 110, 102, 104, 111, 107, 109, 106,
    105, 108, 103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
    72, 73, 82, 83, 86, 127, 116, 117, 183, 184, 185, 186, 187, 188, 189, 190,
    191, 192, 193, 194, 134, 138, 130, 132, 128, 129, 131, 137, 133, 135, 136, 113,
    115, 114, 0, 0, 0, 121, 0, 89, 93, 124, 92, 94, 95, 0, 0, 0,
    122, 123, 90, 91, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    29, 42, 56, 125, 97, 54, 100, 126, 164, 166, 165, 163, 161, 115, 114, 113,
    150, 158, 159, 128, 136, 177, 178, 176, 142, 152, 173, 140};

/**
 * struct usb_kbd - state of each attached keyboard
 * @dev:	input device associated with this keyboard
 * @usbdev:	usb device associated with this keyboard
 * @old:	data received in the past from the @irq URB representing which
 *		keys were pressed. By comparing with the current list of keys
 *		that are pressed, we are able to see key releases.
 * @irq:	URB for receiving a list of keys that are pressed when a
 *		new key is pressed or a key that was pressed is released.
 * @led:	URB for sending LEDs (e.g. numlock, ...)
 * @newleds:	data that will be sent with the @led URB representing which LEDs
 *		should be on
 * @name:	Name of the keyboard. @dev's name field points to this buffer
 * @phys:	Physical path of the keyboard. @dev's phys field points to this
 *		buffer
 * @new:	Buffer for the @irq URB
 * @cr:		Control request for @led URB
 * @leds:	Buffer for the @led URB
 * @new_dma:	DMA address for @irq URB
 * @leds_dma:	DMA address for @led URB
 * @leds_lock:	spinlock that protects @leds, @newleds, and @led_urb_submitted
 * @led_urb_submitted: indicates whether @led is in progress, i.e. it has been
 *		submitted and its completion handler has not returned yet
 *		without	resubmitting @led
 */
struct usb_kbd
{
    struct input_dev *dev;
    struct usb_device *usbdev;
    unsigned char old[8];
    struct urb *irq, *led;
    unsigned char newleds;
    char name[128];
    char phys[64];
    int mode; // To track the mode

    unsigned char *new;
    struct usb_ctrlrequest *cr;
    unsigned char *leds;
    dma_addr_t new_dma;
    dma_addr_t leds_dma;

    spinlock_t leds_lock;
    bool led_urb_submitted;
};

static void usb_kbd_irq(struct urb *urb)
{
    printk(KERN_INFO "usb_kbd_irq");
    struct usb_kbd *kbd = urb->context;
    int i;
    // pr_info("usb_kbd_irq: urb->status = %d \n",urb->status);

    switch (urb->status)
    {
    case 0: /* success */
        break;
    case -ECONNRESET: /* unlink */
    case -ENOENT:
    case -ESHUTDOWN:
        return;
    /* -EPIPE:  should clear the halt */
    default: /* error */
        goto resubmit;
    }

    for (i = 0; i < 8; i++)                                                           //(224~231) Determine whether the newly pressed key is a combination
        input_report_key(kbd->dev, usb_kbd_keycode[i + 224], (kbd->new[0] >> i) & 1); // Key combination

    for (i = 2; i < 8; i++)
    {
        // memscan(kbd->new + 2, kbd->old[i], 6) means to scan 6 units from kbd->new[2] to kbd->new[7],
        // Look for the same character as kbd->old[i], and return the scanned unit address "==kbd->new+8" to indicate that it was not found
        // Keyboard scan code 0-No Event 1-Overrun Error 2-POST Fail 3-ErrorUndefined so kbd->old[i] > 3
        // The key value usb_kbd_keycode[i] and the scan code new[i]/old[i] should be distinguished and don't mess up
        if (kbd->old[i] > 3 && memscan(kbd->new + 2, kbd->old[i], 6) == kbd->new + 8)
        {
            // The same code value as the old data is not found in the new data--indicates that the new button is pressed and the old button is released

            // pr_info("usb_kbd_irq:first if- i = %d, kbd->old[i] = %d, %s, kbd->new = %d, %s\n",i, kbd->old[i],kbd->old[i],kbd->new,kbd->new);
            if (usb_kbd_keycode[kbd->old[i]]) // The released button is a normal button
                input_report_key(kbd->dev, usb_kbd_keycode[kbd->old[i]], 0);
            else
                hid_info(urb->dev,
                         "Unknown key (scancode %#x) released.\n",
                         kbd->old[i]);
        }
        // The same code value as the new data is not found in the old data--indicates that the new button is pressed, and the old button is pressed
        if (kbd->new[i] > 3 && memscan(kbd->old + 2, kbd->new[i], 6) == kbd->old + 8)
        {
            // pr_info("usb_kbd_irq:Second if- i = %d, kbd->old[i] = %d, %s, kbd->new = %d, %s\n",i, kbd->old[i],kbd->old[i],kbd->new,kbd->new);
            if (usb_kbd_keycode[kbd->new[i]])
                input_report_key(kbd->dev, usb_kbd_keycode[kbd->new[i]], 1);
            else
                hid_info(urb->dev,
                         "Unknown key (scancode %#x) pressed.\n",
                         kbd->new[i]);
        }

        // If you keep pressing a certain key on the keyboard, the data received by usb will be the same, that is, kbd->old==kbd->new,
        // then the press event will be reported when it is pressed, and it will be pressed all the time. will not continue to report key presses or releases when

        // If a new key is pressed, all the values ​​of kdb->old can be found in kdb->new, and the value of the key code representing
        // the new key in kdb->new cannot be found in kdb->old to, so the second if condition is triggered, and the button press event is reported

        // If the previous key is released, all the values ​​of kdb->new can be found in kdb->old, and the value of the old key code in kdb->old
        // cannot be found in kdb->new , so the first if condition is triggered, and the release button event is reported
    }

    input_sync(kbd->dev);

    memcpy(kbd->old, kbd->new, 8);

resubmit:
    i = usb_submit_urb(urb, GFP_ATOMIC);
    if (i)
        hid_err(urb->dev, "can't resubmit intr, %s-%s/input0, status %d",
                kbd->usbdev->bus->bus_name,
                kbd->usbdev->devpath, i);
}

static int usb_kbd_event(struct input_dev *dev, unsigned int type,
                         unsigned int code, int value)
{
    printk(KERN_INFO "usb_kbd_event");

    unsigned long flags;
    struct usb_kbd *kbd = input_get_drvdata(dev);

    if (type != EV_LED)
        return -1;

    spin_lock_irqsave(&kbd->leds_lock, flags);
    kbd->newleds = (!!test_bit(LED_KANA, dev->led) << 3) | (!!test_bit(LED_COMPOSE, dev->led) << 3) |
                   (!!test_bit(LED_SCROLLL, dev->led) << 2) | (!!test_bit(LED_CAPSL, dev->led) << 1) |
                   (!!test_bit(LED_NUML, dev->led));
    // pr_info("usb_kbd_event: kbd->newleds = %X, %c\n",kbd->newleds,kbd->newleds);

    if (kbd->newleds == 0x01 && kbd->mode == 1)
    {
        printk(KERN_ALERT "MODE1 ----- to -----  MODE2");
        kbd->newleds = 0x03;
        kbd->mode = 2;
    }
    else if (kbd->newleds == 0x00 && kbd->mode == 2)
    {
        printk(KERN_ALERT "MODE2 ----- to -----  MODE1");
        kbd->newleds = 0x00;
        kbd->mode = 1;
    }
    else if (kbd->newleds == 0x01 && kbd->mode == 2)
    {
        printk(KERN_ALERT "MODE2");
        kbd->newleds = 0x03;
    }
    else if (kbd->newleds == 0x03 && kbd->mode == 2)
    {
        printk(KERN_ALERT "MODE2");
        kbd->newleds = 0x01;
    }
    else if (kbd->newleds == 0x02 && kbd->mode == 2)
    {
        printk(KERN_ALERT "MODE2 ----- to -----  MODE1");
        kbd->newleds = 0x02;
        kbd->mode = 1;
    }
    else
        printk(KERN_ALERT "MODE1");

    if (kbd->led_urb_submitted)
    {
        spin_unlock_irqrestore(&kbd->leds_lock, flags);
        return 0;
    }

    if (*(kbd->leds) == kbd->newleds)
    {
        spin_unlock_irqrestore(&kbd->leds_lock, flags);
        return 0;
    }

    *(kbd->leds) = kbd->newleds;

    kbd->led->dev = kbd->usbdev;
    if (usb_submit_urb(kbd->led, GFP_ATOMIC))
        pr_err("usb_submit_urb(leds) failed\n");
    else
        kbd->led_urb_submitted = true;

    spin_unlock_irqrestore(&kbd->leds_lock, flags);

    return 0;
}

static void usb_kbd_led(struct urb *urb)
{
    printk(KERN_INFO "usb_kbd_led");

    // pr_info("usb_kbd_led: Started\n");
    unsigned long flags;
    struct usb_kbd *kbd = urb->context;

    if (urb->status)
        hid_warn(urb->dev, "led urb status %d received\n",
                 urb->status);

    spin_lock_irqsave(&kbd->leds_lock, flags);

    if (*(kbd->leds) == kbd->newleds)
    {
        kbd->led_urb_submitted = false;
        spin_unlock_irqrestore(&kbd->leds_lock, flags);
        return;
    }

    *(kbd->leds) = kbd->newleds;

    kbd->led->dev = kbd->usbdev;
    if (usb_submit_urb(kbd->led, GFP_ATOMIC))
    {
        hid_err(urb->dev, "usb_submit_urb(leds) failed\n");
        kbd->led_urb_submitted = false;
    }
    spin_unlock_irqrestore(&kbd->leds_lock, flags);
}

static int usb_kbd_open(struct input_dev *dev)
{
    printk(KERN_INFO "usb_kbd_open");

    struct usb_kbd *kbd = input_get_drvdata(dev);
    // pr_info("usb_kbd_open: Opened- \n");

    kbd->irq->dev = kbd->usbdev;
    if (usb_submit_urb(kbd->irq, GFP_KERNEL))
        return -EIO;

    return 0;
}

static void usb_kbd_close(struct input_dev *dev)
{
    printk(KERN_INFO "usb_kbd_close");

    struct usb_kbd *kbd = input_get_drvdata(dev);
    // pr_info("usb_kbd_close: Closed- \n");
    usb_kill_urb(kbd->irq);
}

static int usb_kbd_alloc_mem(struct usb_device *dev, struct usb_kbd *kbd)
{
    printk(KERN_INFO "usb_kbd_alloc_mem");

    if (!(kbd->irq = usb_alloc_urb(0, GFP_KERNEL)))
        return -1;
    if (!(kbd->led = usb_alloc_urb(0, GFP_KERNEL)))
        return -1;
    if (!(kbd->new = usb_alloc_coherent(dev, 8, GFP_KERNEL, &kbd->new_dma)))
        return -1;
    if (!(kbd->cr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL)))
        return -1;
    if (!(kbd->leds = usb_alloc_coherent(dev, 1, GFP_KERNEL, &kbd->leds_dma)))
        return -1;

    return 0;
}

static void usb_kbd_free_mem(struct usb_device *dev, struct usb_kbd *kbd)
{
    printk(KERN_INFO "usb_kbd_free_mem");

    // pr_info("usb_kbd_free_mem: Freemem- \n");

    usb_free_urb(kbd->irq);
    usb_free_urb(kbd->led);
    usb_free_coherent(dev, 8, kbd->new, kbd->new_dma);
    kfree(kbd->cr);
    usb_free_coherent(dev, 1, kbd->leds, kbd->leds_dma);
}

static int usb_kbd_probe(struct usb_interface *iface,
                         const struct usb_device_id *id)
{
    printk(KERN_INFO "usb_kbd_probe");

    // pr_info("usb_kbd_probe: Probing- \n");

    struct usb_device *dev = interface_to_usbdev(iface);
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_kbd *kbd;
    struct input_dev *input_dev;
    int i, pipe, maxp;
    int error = -ENOMEM;

    interface = iface->cur_altsetting;

    if (interface->desc.bNumEndpoints != 1)
        return -ENODEV;

    endpoint = &interface->endpoint[0].desc;
    if (!usb_endpoint_is_int_in(endpoint))
        return -ENODEV;

    pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
    maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

    kbd = kzalloc(sizeof(struct usb_kbd), GFP_KERNEL);
    input_dev = input_allocate_device();
    if (!kbd || !input_dev)
        goto fail1;

    if (usb_kbd_alloc_mem(dev, kbd))
        goto fail2;

    kbd->usbdev = dev;
    kbd->dev = input_dev;

    kbd->mode = 1;
    spin_lock_init(&kbd->leds_lock);

    if (dev->manufacturer)
        strlcpy(kbd->name, dev->manufacturer, sizeof(kbd->name));
    // pr_info("usb_kbd_probe: kbd->name= %s\n",kbd->name);

    if (dev->product)
    {
        if (dev->manufacturer)
            strlcat(kbd->name, " ", sizeof(kbd->name));
        strlcat(kbd->name, dev->product, sizeof(kbd->name));
    }
    // pr_info("usb_kbd_probe: dev->product= %s\n",dev->product);

    if (!strlen(kbd->name))
        snprintf(kbd->name, sizeof(kbd->name),
                 "USB HIDBP Keyboard %04x:%04x",
                 le16_to_cpu(dev->descriptor.idVendor),
                 le16_to_cpu(dev->descriptor.idProduct));

    usb_make_path(dev, kbd->phys, sizeof(kbd->phys));
    strlcat(kbd->phys, "/input0", sizeof(kbd->phys));
    pr_info("usb_kbd_probe: kbd->phys = %s\n", kbd->phys);

    input_dev->name = kbd->name;
    input_dev->phys = kbd->phys;
    usb_to_input_id(dev, &input_dev->id);
    input_dev->dev.parent = &iface->dev;

    input_set_drvdata(input_dev, kbd);

    input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) |
                          BIT_MASK(EV_REP);
    // Input event type key+led+repeat
    //#define BIT_MASK(nr)		(UL(1) << ((nr) % BITS_PER_LONG))
    input_dev->ledbit[0] = BIT_MASK(LED_NUML) | BIT_MASK(LED_CAPSL) |
                           BIT_MASK(LED_SCROLLL) | BIT_MASK(LED_COMPOSE) |
                           BIT_MASK(LED_KANA);
    // Keyboard led events: keypad, case, scroll lock, key combination, KANA
    // pr_info("usb_kbd_probe: input_dev->evbit[0] = %X\n",input_dev->evbit[0]);
    // pr_info("usb_kbd_probe: input_dev->ledbit[0] = %X\n",input_dev->ledbit[0]);

    for (i = 0; i < 255; i++)
        set_bit(usb_kbd_keycode[i], input_dev->keybit);
    clear_bit(0, input_dev->keybit);

    input_dev->event = usb_kbd_event;
    input_dev->open = usb_kbd_open;
    input_dev->close = usb_kbd_close;

    usb_fill_int_urb(kbd->irq, dev, pipe,
                     kbd->new, (maxp > 8 ? 8 : maxp),
                     usb_kbd_irq, kbd, endpoint->bInterval);
    kbd->irq->transfer_dma = kbd->new_dma;
    kbd->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    kbd->cr->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    kbd->cr->bRequest = 0x09;
    kbd->cr->wValue = cpu_to_le16(0x200);
    kbd->cr->wIndex = cpu_to_le16(interface->desc.bInterfaceNumber);
    kbd->cr->wLength = cpu_to_le16(1);

    usb_fill_control_urb(kbd->led, dev, usb_sndctrlpipe(dev, 0),
                         (void *)kbd->cr, kbd->leds, 1,
                         usb_kbd_led, kbd);
    kbd->led->transfer_dma = kbd->leds_dma;
    kbd->led->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    error = input_register_device(kbd->dev);
    if (error)
        goto fail2;

    usb_set_intfdata(iface, kbd);
    device_set_wakeup_enable(&dev->dev, 1);
    return 0;

fail2:
    usb_kbd_free_mem(dev, kbd);
fail1:
    input_free_device(input_dev);
    kfree(kbd);
    return error;
}

static void usb_kbd_disconnect(struct usb_interface *intf)
{
    printk(KERN_INFO "usb_kbd_disconnect");

    // pr_info("usb_kbd_disconnect: Disconnected- \n");

    struct usb_kbd *kbd = usb_get_intfdata(intf);

    usb_set_intfdata(intf, NULL);
    if (kbd)
    {
        usb_kill_urb(kbd->irq);
        input_unregister_device(kbd->dev);
        usb_kill_urb(kbd->led);
        usb_kbd_free_mem(interface_to_usbdev(intf), kbd);
        kfree(kbd);
    }
}

static const struct usb_device_id usb_kbd_id_table[] = {
    {USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
                        USB_INTERFACE_PROTOCOL_KEYBOARD)},
    {} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_kbd_id_table);

static struct usb_driver usb_kbd_driver = {
    .name = "usbkbd",
    .probe = usb_kbd_probe,
    .disconnect = usb_kbd_disconnect,
    .id_table = usb_kbd_id_table,
};

module_usb_driver(usb_kbd_driver);

// Reference notes
//#define LED_NUML		0x00
//#define LED_CAPSL		0x01
//#define LED_SCROLLL		0x02
//#define LED_COMPOSE		0x03
//#define LED_KANA		0x04
//#define LED_SLEEP		0x05
//#define LED_SUSPEND		0x06
//#define LED_MUTE		0x07
//#define LED_MISC		0x08
//#define LED_MAIL		0x09
//#define LED_CHARGING		0x0a
//#define LED_MAX			0x0f
//#define LED_CNT			(LED_MAX+1)

//#define EV_SYN			0x00
//#define EV_KEY			0x01
//#define EV_REL			0x02
//#define EV_ABS			0x03
//#define EV_MSC			0x04
//#define EV_SW			0x05
//#define EV_LED			0x11
//#define EV_SND			0x12
//#define EV_REP			0x14
//#define EV_FF			0x15
//#define EV_PWR			0x16
//#define EV_FF_STATUS		0x17
//#define EV_MAX			0x1f
//#define EV_CNT			(EV_MAX+1)

/* BEGIN 04 - Regular Keys */
//	/* 0-7 */       KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 8-15 */      KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_LEFTMETA, KEY_RIGHTCTRL, KEY_RIGHTSHIFT, KEY_RIGHTALT, KEY_RESERVED,
//	/* 16-23 */     KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_A, KEY_B, KEY_C, KEY_D,
//	/* 24-31 */     KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L,
//	/* 32-39 */     KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
//	/* 40-47 */     KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_1, KEY_2,
//	/* 48-55 */     KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
//	/* 56-63 */     KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB, KEY_SPACE, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE,
/* END 04 */

/* BEGIN 05 - Function Keys */
//	/* 64-71 */     KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 72-79 */     KEY_RIGHTBRACE, KEY_BACKSLASH, AZ_KEY_CONTESTED, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA, KEY_DOT,
//	/* 80-87 */     KEY_SLASH, KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
//	/* 88-95 */     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_SYSRQ, KEY_SCROLLLOCK,
//	/* 96-103 */    KEY_PAUSE, KEY_INSERT, KEY_HOME, KEY_PAGEUP, KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_RIGHT,
//	/* 104-111 */   KEY_LEFT, KEY_DOWN, KEY_UP, KEY_NUMLOCK, KEY_KPSLASH, KEY_KPASTERISK, KEY_KPMINUS, KEY_KPPLUS,
//	/* 112-119 */   KEY_KPENTER, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KP7,
//	/* 120-127 */   KEY_KP8, KEY_KP9, KEY_KP0, KEY_KPDOT, KEY_102ND, KEY_MENU, KEY_RESERVED, KEY_RESERVED,
/* END 05 */

/* BEGIN 01 -  Volume Keys */
//	/* 128-135 */   KEY_VOLUMEDOWN, KEY_VOLUMEUP, KEY_MEDIA, KEY_MUTE, KEY_PAUSE, KEY_PREVIOUSSONG, KEY_PLAYPAUSE, KEY_NEXTSONG,
//	/* 136-143 */   KEY_MAIL, KEY_HOMEPAGE, KEY_CALC, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 144-151 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 152-159 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 160-167 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 168-175 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 176-183 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 184-191 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
/* END 01 */

/* BEGIN 06 - Other (unknown) Keys */
//	/* 192-199 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 200-207 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 208-215 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 216-223 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 224-231 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_SLASH,
//	/* 232-239 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 240-247 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
//	/* 248-255 */   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
/* END 06 */
