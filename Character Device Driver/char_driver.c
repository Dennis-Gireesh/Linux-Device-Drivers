//Author - Venkata Sai Gireesh Chamarthi
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/moduleparam.h>

#define DEVICE_NAME "mycdrv"

#define CDRV_IOC_MAGIC 'Z'
#define ASP_CLEAR_BUF _IOW(CDRV_IOC_MAGIC, 1, int)

static size_t ramdisk_size = (16 * PAGE_SIZE);
static unsigned int NUM_DEVICES = 3;
static dev_t maj_min;              // device number = majornumber : minornumber
static struct class *mycdrv_class; // class clueprint to define our device class later in init.
int major_num;
int minor_num;


typedef struct asp_mycdev
{
    struct cdev cdev;
    char *ramdisk;
    size_t ram_size;
    int counter;
    struct semaphore sem;
    int buf_size;
    int dev_num;
} asp_mycdev;
asp_mycdev *device_nodes; // struct pointer to the devices.

static int mycdrv_open(struct inode *inode, struct file *file);
static int mycdrv_release(struct inode *inode, struct file *file);
static ssize_t mycdrv_read(struct file *file, char __user *buf, size_t lbuf, loff_t *ppos);
static ssize_t mycdrv_write(struct file *file, const char __user *buf, size_t lbuf, loff_t *ppos);
static loff_t mycdrv_llseek(struct file *filp, loff_t offset, int parameter);
static long mycdrv_ioctl(struct file *filp, unsigned int cmd, unsigned long direction);
static int __init my_init(void);
static void __exit my_exit(void);

static const struct file_operations mycdrv_fops =
    {
        .owner = THIS_MODULE,
        .read = mycdrv_read,
        .write = mycdrv_write,
        .open = mycdrv_open,
        .release = mycdrv_release,
        .llseek = mycdrv_llseek,
        .unlocked_ioctl = mycdrv_ioctl,
};

// module parameters -
module_param(NUM_DEVICES, int, S_IRUGO);
static int __init my_init(void)
{
    unsigned int first_minor_num = 0; // first minor number starts with 0
    int err, i;

    // get range of minor numbers and dynamic major number.
    if (alloc_chrdev_region(&maj_min, first_minor_num, NUM_DEVICES, DEVICE_NAME) != 0)
    {
        pr_info("INIT ERROR (mycdrv): Could not create major number dynamically.\n");
        return -1;
    }
    major_num = MAJOR(maj_min);
    // create class device nodes
    if ((mycdrv_class = class_create(THIS_MODULE, DEVICE_NAME)) == NULL)
    {
        pr_info("INIT ERROR (mycdrv): Class creation failed.\n");
        return -1;
    }

    // allocate space for the device nodes.
    device_nodes = (asp_mycdev *)kmalloc(NUM_DEVICES * sizeof(asp_mycdev), GFP_KERNEL);
    memset(device_nodes, 0, NUM_DEVICES * sizeof(struct asp_mycdev));
    // allocate ramdisk space and create device nodes for total number of devices (init all cdevs)

    for (i = 0; i < NUM_DEVICES; i++)
    {

        // create a device  number with major and minor(i) numbers
        dev_t device_Id = MKDEV(major_num, i);
        asp_mycdev *dev = &device_nodes[i];

        // initialize the custom cdev struct members
        cdev_init(&dev->cdev, &mycdrv_fops);
        dev->cdev.owner = THIS_MODULE;
        // add character device to the system VFS
        err = cdev_add(&dev->cdev, device_Id, 1);
        if (err)
        {
            pr_err("INIT Error %d adding device node %d", err, i);
        }
        dev->ram_size = ramdisk_size;
        dev->counter = 0;
        dev->dev_num = i;
        dev->buf_size =0;
        dev->ramdisk = (char *)kzalloc(ramdisk_size, GFP_KERNEL);
        sema_init(&dev->sem, 1); // binary semaphore
        device_create(mycdrv_class, NULL, device_Id, NULL, DEVICE_NAME "%d", i);

        pr_info("INIT: Succesfully registered character device %s%d\n", DEVICE_NAME, i);
    }
    return 0;
}

static int mycdrv_open(struct inode *inode, struct file *file)
{
    // static int counter = 0;
    asp_mycdev *d_struct_ptr;
    pr_info("OPEN: Opening Device: %s:\n", DEVICE_NAME);

    // get pointer to the struct data
    d_struct_ptr = container_of(inode->i_cdev, struct asp_mycdev, cdev);
    // store the pointer in files private data.
    file->private_data = d_struct_ptr;

    if(down_interruptible(&d_struct_ptr->sem)){
                return ERESTARTSYS;
            }
    // keeping track of the count
    d_struct_ptr->counter++;
    up(&d_struct_ptr->sem);

    pr_info("OPEN: Successfully opened  %s : opened  %d times since being loaded\n", DEVICE_NAME, d_struct_ptr->counter);
    return 0;
}

static int mycdrv_release(struct inode *inode, struct file *file)
{
    asp_mycdev *d_struct_ptr;
    pr_info("CLOSE: Closing Device: %s:\n", DEVICE_NAME);

    // get pointer to the struct data
    d_struct_ptr = container_of(inode->i_cdev, struct asp_mycdev, cdev);
    // store the pointer in files private data.
    file->private_data = d_struct_ptr;
    if(down_interruptible(&d_struct_ptr->sem)){
                return ERESTARTSYS;
            }
    // decrement count when device closes
    d_struct_ptr->counter--;
    up(&d_struct_ptr->sem);

    pr_info("CLOSE: Successfully Closed  %s .\n", DEVICE_NAME);
    return 0;
}
// module init
module_init(my_init);

static ssize_t
mycdrv_read(struct file *file, char __user *buf, size_t lbuf, loff_t *ppos)
{
    int nbytes = 0;
    asp_mycdev *d_struct_ptr;

    d_struct_ptr = (asp_mycdev *)file->private_data;

    if(down_interruptible(&d_struct_ptr->sem)){
                return ERESTARTSYS;
            }
    // maxbytes = d_struct_ptr->ram_size - *ppos;
    // bytes_to_do = maxbytes > lbuf ? lbuf : maxbytes;
    if ((lbuf + *ppos) > d_struct_ptr->ram_size)
    {
        pr_info("READ: !!!! ALERT !!!!!\n");
        pr_info("READ: End of the device\n");
        pr_info("READ: lbuf + *ppos , d_struct_ptr->ram_size= %d + %d , %d\n", (int)lbuf, (int)*ppos, (int)d_struct_ptr->ram_size);
        up(&d_struct_ptr->sem);
        return 0;
    }
    nbytes = lbuf - copy_to_user(buf, d_struct_ptr->ramdisk + *ppos, lbuf);
    *ppos += nbytes;
    up(&d_struct_ptr->sem);

    pr_info("\n READ: Read succesfull, nbytes=%d, pos=%d\n", nbytes, (int)*ppos);
    return nbytes;
}

static ssize_t mycdrv_write(struct file *file, const char __user *buf, size_t lbuf, loff_t *ppos)
{
    int nbytes = 0;
    asp_mycdev *d_struct_ptr;
    d_struct_ptr = (asp_mycdev *)file->private_data;
    pr_info("WRITE: at starting *ppos = %d, file->f_pos = %d \n", (int)*ppos, (int)file->f_pos);
    if(down_interruptible(&d_struct_ptr->sem)){
                return ERESTARTSYS;
            }
    // check if the user is trying to write past end of the device
    if ((lbuf + *ppos) > d_struct_ptr->ram_size)
    {
        pr_info("WRITE: !!!! ALERT !!!!! \n");
        pr_info("WRITE: End of the device\n");
        pr_info("WRITE: lbuf + *ppos , d_struct_ptr->ram_size= %d + %d , %d\n", (int)lbuf, (int)*ppos, (int)d_struct_ptr->ram_size);

        return 0;
    }

    // copy data from user space into kernel space
    nbytes = lbuf - copy_from_user(d_struct_ptr->ramdisk + *ppos, buf, lbuf);
    if(nbytes + *ppos > d_struct_ptr->buf_size){
        d_struct_ptr->buf_size = nbytes- ( d_struct_ptr->buf_size - *ppos);
    }
    //d_struct_ptr->buf_size = nbytes;
    *ppos += nbytes;
    file->f_pos = *ppos;
    up(&d_struct_ptr->sem);

    pr_info("WRITE: Write succesfull, nbytes=%d, pos=%d\n", nbytes, (int)*ppos);
    return nbytes;
}

static loff_t mycdrv_llseek(struct file *file, loff_t offset, int parameter)
{
    loff_t temppos;
    int flag =0;
    size_t current_size, new_size;
    int i;
    asp_mycdev *d_struct_ptr;
    char *temp_ramdisk;
    d_struct_ptr = (asp_mycdev *)file->private_data;
    if(down_interruptible(&d_struct_ptr->sem)){
                return ERESTARTSYS;
            }
    switch (parameter)
    {
    case SEEK_SET:
        temppos = offset;
        break;
    case SEEK_CUR:
        temppos = file->f_pos + offset;
        break;
    case SEEK_END://setting the SEEK_END to the end of the data.
        flag=1;
        if(!(d_struct_ptr->buf_size>= 0)){
            d_struct_ptr->buf_size=0;
        }
        temppos = (d_struct_ptr->buf_size) + (offset);
        break;
    default:
        return -EINVAL;
    }
    if (temppos > d_struct_ptr->ram_size)
    {
        // set the cursor to the end of file + offset
        pr_info("LLSEEK: Reallocating device size.\n");
        //file->f_pos = ramdisk_size + offset - 1;
        current_size = d_struct_ptr->ram_size;
        new_size = d_struct_ptr->ram_size + (1 * PAGE_SIZE);
        pr_info("LLSEEK: Current size = %d, New_size = %d \n", (int)current_size, (int)new_size);

        // extending the size and allocating 0's
        if ((temp_ramdisk = (char *)kzalloc(new_size, GFP_KERNEL)) != NULL)
        {
            // retriving the contents of current_ramdisk to temp_ramdisk
            for (i = 0; i < current_size; i++)
            {
                temp_ramdisk[i] = d_struct_ptr->ramdisk[i];
            }

            // deallocation the original ramdisk
            kfree(d_struct_ptr->ramdisk);

            // assiging new ramdisk and ramsize to the device data structure
            d_struct_ptr->ramdisk = temp_ramdisk;
            d_struct_ptr->ram_size = new_size;
        }
        else
        {
            pr_info("LLSEEK: ERROR: Reallocattion of the memory failed.\n");
        }
    }
    //temppos = temppos < d_struct_ptr->ram_size ? temppos : d_struct_ptr->ram_size;
    temppos = temppos >= 0 ? temppos : 0;
    // if(flag){
    //     file->f_pos = temppos_dispose;
    //     pr_info("Seeking end to pos=%ld\n", (long)temppos_dispose);

    // }else{
    file->f_pos = temppos;
    
    pr_info("LLSEEK: Seeking to position=%ld\n", (long)temppos);
    //}
    up(&d_struct_ptr->sem);
    return temppos;
}

static long mycdrv_ioctl(struct file *file, unsigned int cmd, unsigned long direction)
{
	asp_mycdev* d_struct_ptr;
	d_struct_ptr = (asp_mycdev*)file->private_data;

	switch(cmd)
	{
		case ASP_CLEAR_BUF:
			pr_info("IOCTL: Clearing device memory.\n");
			if(down_interruptible(&d_struct_ptr->sem)){
                return ERESTARTSYS;
            }
            // Reset the device memory 
			memset((void *) d_struct_ptr->ramdisk, 0, d_struct_ptr->ram_size);
            //set position to 0.
			file->f_pos = 0;
            d_struct_ptr->buf_size = 0;
			up(&d_struct_ptr->sem);
			break;
		
		default:
			pr_info("IOCTL: ERROR Invalid cmd flag.\n");
			return -1;
	}

	return 0;
}

static void __exit my_exit(void)
{
    int i;
    pr_info("EXIT: Unregistering Character Device\n");

    // deallocate each device's ramdisk, cdev, and device
    for (i = 0; i < NUM_DEVICES; i++)
    {
        asp_mycdev *devv = &device_nodes[i];

        kfree(devv->ramdisk);
        pr_info("EXIT: Free ramdisk for device %d\n", i);

        device_destroy(mycdrv_class, MKDEV(major_num, i));
        pr_info("EXIT:  Destroyed device node %d\n", i);

        cdev_del(&devv->cdev);
        pr_info("EXIT:  Deleted cdev for device %d\n", i);

        //sema_destroy(&devv->sem);
        //pr_info("EXIT:  Deleted Semaphore for device %d\n", i);
    }

    kfree(device_nodes);
    pr_info("EXIT: Deallocated devices\n");

    class_destroy(mycdrv_class);
    pr_info("EXIT: Destroyed class blueprint\n");

    unregister_chrdev_region(maj_min, NUM_DEVICES);
    pr_info("EXIT: Unregistered device regions\n");
}

// module exit
module_exit(my_exit);



// other module details
MODULE_AUTHOR("Venkata Sai Gireesh Chamarthi");
MODULE_DESCRIPTION("This is a dynamic divices creation with synchronization and Lseek syscall registration assignment");
MODULE_LICENSE("GPL v2");