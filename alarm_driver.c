#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <mach/platform.h>
#include <linux/io.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/wait.h>

#define _MAJOR 221				//����̽� ����̹� �ֹ�ȣ
#define _NAME "LED_DRIVER"		//����̽� ����̹� �̸�
#define _SIZE 256				//gpio ������ �ּ�

char _usage = 0;				//����̹� ��� üũ
static void *_map;				//������ ������ �ޱ� ���� ������
volatile unsigned *led;			//gpio ������ ���� ������
static int event_flag = 0;		//�̺�Ʈ �߻�Ƚ�� üũ

DECLARE_WAIT_QUEUE_HEAD(waitqueue);//�̺�Ʈ üũ�� ���ť ����

static irqreturn_t ind_interrupt_handler(int irq, void *pdata)//ISR ���� �Լ�
{
	*(led + 13)&(1 << 7);			//gpio7 (����ġ GPIO IN)�� �д´�
	wake_up_interruptible(&waitqueue)//���ͷ�Ʈ Ȱ��ȭ
	++event_flag;
	return IRQ_HANDLED;				//���ͷ�Ʈ handler ����
}

static unsigned key_poll(struct file *mfile, struct poll_table_struct *pt)//���ͷ�Ʈ ���� �Լ�
{
	int mask = 0;					//���ͷ�Ʈ ���� üũ ����
	poll_wait(mfile, &waitqueue, pt);//ť�� ����Ų�� (sleep�� ����)
	
	if (event_flag > 0)
		mask |= (POLLIN | POLLRDNORM);//���� �Ϲ� �����Ͱ� ����(���ͷ�Ʈ�� �߻��߾���)
	
	event_flag = 0;					//flag ���� �ٽ� 0
	return mask;					//���� return
}

static int _open(struct inode *minode, struct file *mfile)//����̽� open �Լ�
{
	int i, k;						//gpio select�� ���� �ݺ��� ����
	int result;						//��û�� ���ͷ�Ʈ ���Ȯ�� ����
	
	if (_usage != 0)				//����̹��� 0�� �ƴϸ� �̹� open�Ǿ���
		return -EBUSY;				//�� ���̹Ƿ� return ���� �Լ� ����
	
	_usage = 1;						//����̹��� open �Ǿ����� üũ
	_map = ioremap(GPIO_BASE, _SIZE);//������ ����
	
	if (!_map)						//���� ����ó��
	{
		printk("error: mapping gpio memory");
		iounmap(_map);						//���� �Ǿ� ������� ������ ���´�
		return -EBUSY;
	
	}
	led = (volatile unsigned int *)_map;	//�������ͷ� �����ϱ� ���� 4����Ʈ ����������
	
	for(k=0;k<=2;k++)						//gpio 0~27���� ��� OUT���� ����
		for (i = 0; i <= 9; i++)
		{
			if (k == 0 && (3 * i == 21))continue;//��, gpio 7�� IN���� ������ ���� �Ѿ��
			*(led + k) &= ~(0x7 << (3 * i));	//gpio select
			*(led + k) != (0x1 << (3 * i));		//set gpio out
		}
	
	*(led) &= ~(0x7 << (7 * 3));				//gpio 7 select
	*(led + 22) != (0x1 << 7);					//set GPFENO to 1 to detect Falling edge
	
	result = request_irq(gpio_to_irq(7), ind_interrupt_handler, IRQF_TRIGGER_FALLING, "gpio_irq_key", NULL);//gpio 7���� ���� ���ͷ�Ʈ ����										

	if (result < 0)								//���ͷ�Ʈ ��û�� ����
	{
		printk("error: request_irq()");
		return result;
	}
	return 0;
}

static int _release(struct inode *minode, struct file *mfile)//����̽� release(close) �Լ�
{
	_usage = 0;									//����̺� ���� 0���� ����

	if (led)
		iounmap(led);							//����̺� ���� ����
	free_irq(gpio_to_irq(7), NULL);				//ISR ����
	return 0;
}

static int _write(struct file *mfile, const char *gdata, size_t length, loff_t *off_what)//����̽� ������ write �Լ�
{
	char tmp_buf;								//gpio set �� ��ȣ�� �����ϴ� ����
	int result;
	int left, right;							//copy from user �Լ� ���� Ȯ�� ����
												//left : gpio address ����, right : gpio shift ũ�� ����
	
	/*�Ʒ��� 16���� �񱳹����� user�� ���� 16������ ���� gpio�� �������� write �ȴ�*/
	if (0xFF == *gdata)							//Matrix 0FF(gpio clear)
	{
		left = 10;
		right = 0;
	}

	if (0x80 == *gdata || 0x60 == *gdata || 0x18 == *gdata || 0x07 == *gdata || 0x01 == *gdata)
	{											//gpio 10~19�� write
		left = 7;
		right = 10;
	}

	if (0x6B == *gdata || 0x77 == *gdata || 0x3F == *gdata)//gpio 0~9�� write
	{
		left = 7;
		right = 0;
	}

	if (0xC0 == *gdata || 0x30 == *gdata || 0x20 == *gdata || 0x10 == *gdata)//gpio 20~29�� write
	{
		left = 7;
		right = 20;
	}

	result = copy_from_user(&tmp_buf, gdata, length);//user ���� �����͸� �޾ƿ´�
	if (result < 0)					//�����͸� ���������� ���޾ƿ����� 0
	{
		printk("Error: copy from user");
		return result;
	}

	printk("data from app : %d\n", tmp_buf);

	if (0x00 == *gdata)				//Dot exit		//Dot Matrix, BUZZER gpio clear
	{
		*(led + 10) = (0xFFF << 0);				//0~9 gpio clear
		*(led + 10) = (0xFF << 10);				//10~19 gpio clear
		*(led + 10) = (0x01 << 8);				//gpio 8 clear
		*(led + 10) = (0x01 << 9);				//gpio 9 clear
	}

	else if (0x02 == *gdata)					//Motor gpio clear
	{
		*(led + 10) = (0xFF << 20);				//20~39 gpio clear
	}

	else
	{
		*(led + left) = (tmp_buf << right);			//16���� ���ǹ����� ������ left,right ���� ���� gpio ����
		*(led + 10) = (0x01 << 8);					//BUZZER gpio 8 Ȱ��ȭ
		*(led + 7) = (0x01 << 9);					//BUZZER gpio 9 Ȱ��ȭ
	}
	return length;
}

static struct file_operations _fops =				//���� ���� ����ü
{
	.owner = THIS_MODULE,							//��� ������
	.open = _open,									//open
	.release = _release,							//release
	.write = _write,								//write
	.poll = key_poll,								//poll
};

static int _init(void)								//����̹� ���� �Լ�
{
	int result;										//����̹� ���� ���� ����

	result = register_chrdev(_MAJOR, _NAME, &_fops);//����̹��� �����Ѵ�
	
	if (result < 0)									//����̹� ���翡 ����
	{
		printk("KERN_WARNING Can't get any major!\n");
		return result;
	}
	return 0;
}

static void _exit(void)								//����̹� �����Լ�
{
	unregister_chrdev(_MAJOR, _NAME);				//����̹� ����
	printk("module removed.\n");
}

module_init(_init);									//Ŀ�θ�� ����
module_exit(_exit);									//Ŀ�� ������ ����

MODULE_LICENSE("GPL")								//��� ���̼���