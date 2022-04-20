#include <linux/ioctl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#define DEVICE "/dev/mycdrv"

#define CDRV_IOC_MAGIC 'Z'
#define ASP_CLEAR_BUF _IOW(CDRV_IOC_MAGIC, 1, int)


int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Device number not specified\n");
		return 1;
	}
	int dev_no = atoi(argv[1]);
	char dev_path[20];
	int i,fd;
	char ch, write_buf[11], read_buf[11];
	int offset, origin;
	sprintf(dev_path, "%s%d", DEVICE, dev_no);
	printf("Device Path: %s\n", dev_path);
 
	fd = open(dev_path, O_RDWR);
	if(fd == -1) {
		printf("File %s either does not exist or has been locked by another "
				"process\n", DEVICE);
		exit(-1);
	}

    // Test of lseek: SEEK_SET
	strcpy(write_buf, "1234567890");
	write(fd, write_buf, sizeof(write_buf));
    printf("data written to driver: %s\n", write_buf);

	origin = 0; //0: SEEK_SET - 1: SEEK_CUR - 2: SEEK_END
	offset = 5;
	lseek(fd, offset, origin);
	if (read(fd, read_buf, sizeof(read_buf)) > 0) {
		printf("[TEST1] Expected: \"67890\" - Got: \"%s\"\n", read_buf);
	} else {
		fprintf(stderr, "Reading failed\n");
	}

    // Test of lseek: SEEK_CUR
    lseek(fd, 0, 0); // Reset position	
	strcpy(write_buf, "abcdefghij");
	write(fd, write_buf, sizeof(write_buf));
    printf("data written to driver: %s\n", write_buf);
    lseek(fd, 0, 0); // Reset position

	origin = 1;
	offset = 4;
	lseek(fd, offset, origin);
	if (read(fd, read_buf, sizeof(read_buf)) > 0) {
		printf("[TEST2] Expected: \"efghij\" - Got: \"%s\"\n", read_buf);
	} else {
		fprintf(stderr, "Reading failed\n");
	}
    
    // Test of lseek: SEEK_END
    lseek(fd, 0, 0); // Reset position
	origin = 2;
	offset = -5;
	lseek(fd, offset, origin);
	if (read(fd, read_buf, sizeof(read_buf)) > 0) {
		printf("[TEST3] Expected: \"ghij\" - Got: \"%s\"\n", read_buf);
	} else {
		fprintf(stderr, "Reading failed\n");
	}
    
    // Test of lseek: SEEK_SET Negative Value
    lseek(fd, 0, 0); // Reset position
    char read_buf2[11];
	origin = 0;
	offset = -1;
	lseek(fd, offset, origin);
	if (read(fd, read_buf2, sizeof(read_buf2)) > 0) {
		printf("[TEST4] Expected: \"abcdefghij\" - Got: \"%s\"\n", read_buf2);
	} else {
		fprintf(stderr, "Reading failed\n");
	} 

    // Test of ioctl: Clear memory
    lseek(fd, 0, 0); // Reset position
	int rc = ioctl(fd, ASP_CLEAR_BUF, 0);
	if (rc == -1) { 
        printf("IOCTL: ASP_CLEAR_BUF=%ld", ASP_CLEAR_BUF);
		perror("\n***error in ioctl***\n");
		return -1;
	}
    if (read(fd, read_buf2, sizeof(read_buf2)) > 0) {
		printf("[TEST5] Expected: \"\" - Got: \"%s\"\n", read_buf2);
	} else {
		fprintf(stderr, "Reading failed\n");
	}

	close(fd);
	return 0; 
}