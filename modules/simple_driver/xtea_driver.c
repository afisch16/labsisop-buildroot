#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/errno.h>

#define DEVICE_NAME "xtea_driver"
#define CLASS_NAME "xtea_class"
#define MAX_DATA_SIZE 128
#define MAX_COMMAND_SIZE 400

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XTEA character driver");

/* Chaves configuráveis ao carregar o módulo */
static char *key0 = "f0e1d2c3";
static char *key1 = "b4a59687";
static char *key2 = "78695a4b";
static char *key3 = "3c2d1e0f";

module_param(key0, charp, 0444);
module_param(key1, charp, 0444);
module_param(key2, charp, 0444);
module_param(key3, charp, 0444);

static u32 xtea_key[4];

static int majorNumber;
static struct class *charClass;
static struct device *charDevice;

static char response[MAX_DATA_SIZE * 2 + 1];
static size_t response_size;

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops = {
	.open = dev_open,
	.read = dev_read,
	.write = dev_write,
	.release = dev_release,
};

static void encipher(u32 v[2], const u32 key[4])
{
	u32 i;
	u32 v0 = v[0], v1 = v[1], sum = 0;
	u32 delta = 0x9E3779B9;

	for (i = 0; i < 32; i++) {
		v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^
		      (sum + key[sum & 3]);
		sum += delta;
		v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^
		      (sum + key[(sum >> 11) & 3]);
	}

	v[0] = v0;
	v[1] = v1;
}

static void decipher(u32 v[2], const u32 key[4])
{
	u32 i;
	u32 v0 = v[0], v1 = v[1];
	u32 delta = 0x9E3779B9;
	u32 sum = delta * 32;

	for (i = 0; i < 32; i++) {
		v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^
		      (sum + key[(sum >> 11) & 3]);
		sum -= delta;
		v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^
		      (sum + key[sum & 3]);
	}

	v[0] = v0;
	v[1] = v1;
}

static int hex_byte(const char *text, u8 *value)
{
	char pair[3];

	pair[0] = text[0];
	pair[1] = text[1];
	pair[2] = '\0';

	return kstrtou8(pair, 16, value);
}

static int __init xtea_init(void)
{
	if (kstrtou32(key0, 16, &xtea_key[0]) ||
	    kstrtou32(key1, 16, &xtea_key[1]) ||
	    kstrtou32(key2, 16, &xtea_key[2]) ||
	    kstrtou32(key3, 16, &xtea_key[3])) {
		printk(KERN_ERR "XTEA Driver: invalid key parameter\n");
		return -EINVAL;
	}

	printk(KERN_INFO "XTEA Driver: initializing\n");

	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber < 0)
		return majorNumber;

	charClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(charClass)) {
		unregister_chrdev(majorNumber, DEVICE_NAME);
		return PTR_ERR(charClass);
	}

	charDevice = device_create(charClass, NULL,
				   MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(charDevice)) {
		class_destroy(charClass);
		unregister_chrdev(majorNumber, DEVICE_NAME);
		return PTR_ERR(charDevice);
	}

	printk(KERN_INFO "XTEA Driver: available at /dev/%s\n", DEVICE_NAME);
	return 0;
}

static void __exit xtea_exit(void)
{
	device_destroy(charClass, MKDEV(majorNumber, 0));
	class_unregister(charClass);
	class_destroy(charClass);
	unregister_chrdev(majorNumber, DEVICE_NAME);
	printk(KERN_INFO "XTEA Driver: unloaded\n");
}

static int dev_open(struct inode *inodep, struct file *filep)
{
	return 0;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
	return 0;
}

static ssize_t dev_write(struct file *filep, const char *buffer,
			 size_t len, loff_t *offset)
{
	char input[MAX_COMMAND_SIZE];
	char operation[4];
	char hex_data[MAX_DATA_SIZE * 2 + 1];
	u32 data_size;
	u8 data[MAX_DATA_SIZE];
	u32 block, i, v[2];

	if (len == 0 || len >= sizeof(input))
		return -EINVAL;

	if (copy_from_user(input, buffer, len))
		return -EFAULT;
	input[len] = '\0';

	/* Formato: comando tamanho dados_em_hex */
	if (sscanf(input, "%3s %u %256s",
		   operation, &data_size, hex_data) != 3)
		return -EINVAL;

	if (strcmp(operation, "enc") != 0 &&
	    strcmp(operation, "dec") != 0)
		return -EINVAL;

	if (data_size == 0 || data_size > MAX_DATA_SIZE ||
	    data_size % 8 != 0)
		return -EINVAL;

	if (strlen(hex_data) < data_size * 2)
		return -EINVAL;

	for (i = 0; i < data_size; i++) {
		if (hex_byte(&hex_data[i * 2], &data[i]))
			return -EINVAL;
	}

	for (block = 0; block < data_size; block += 8) {
		v[0] = ((u32)data[block] << 24) |
		       ((u32)data[block + 1] << 16) |
		       ((u32)data[block + 2] << 8) |
		       (u32)data[block + 3];

		v[1] = ((u32)data[block + 4] << 24) |
		       ((u32)data[block + 5] << 16) |
		       ((u32)data[block + 6] << 8) |
		       (u32)data[block + 7];

		if (strcmp(operation, "enc") == 0)
			encipher(v, xtea_key);
		else
			decipher(v, xtea_key);

		data[block] = (v[0] >> 24) & 0xff;
		data[block + 1] = (v[0] >> 16) & 0xff;
		data[block + 2] = (v[0] >> 8) & 0xff;
		data[block + 3] = v[0] & 0xff;
		data[block + 4] = (v[1] >> 24) & 0xff;
		data[block + 5] = (v[1] >> 16) & 0xff;
		data[block + 6] = (v[1] >> 8) & 0xff;
		data[block + 7] = v[1] & 0xff;
	}

	for (i = 0; i < data_size; i++)
		snprintf(&response[i * 2], 3, "%02x", data[i]);

	response_size = data_size * 2;
	response[response_size] = '\0';

	return len;
}

static ssize_t dev_read(struct file *filep, char *buffer,
			size_t len, loff_t *offset)
{
	size_t size;

	if (response_size == 0)
		return 0;

	if (len < response_size)
		return -EINVAL;

	size = response_size;
	if (copy_to_user(buffer, response, size))
		return -EFAULT;

	response_size = 0;
	return size;
}

module_init(xtea_init);
module_exit(xtea_exit);
