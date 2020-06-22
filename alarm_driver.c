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

#define _MAJOR 221				//디바이스 드라이버 주번호
#define _NAME "LED_DRIVER"		//디바이스 드라이버 이름
#define _SIZE 256				//gpio 물리적 주소

char _usage = 0;				//드라이버 사용 체크
static void *_map;				//물리적 매핑을 받기 위한 포인터
volatile unsigned *led;			//gpio 설정을 위한 포인터
static int event_flag = 0;		//이벤트 발생횟수 체크

DECLARE_WAIT_QUEUE_HEAD(waitqueue);//이벤트 체크할 대기큐 생성

static irqreturn_t ind_interrupt_handler(int irq, void *pdata)//ISR 설정 함수
{
	*(led + 13)&(1 << 7);			//gpio7 (스위치 GPIO IN)을 읽는다
	wake_up_interruptible(&waitqueue)//인터럽트 활성화
	++event_flag;
	return IRQ_HANDLED;				//인터럽트 handler 리턴
}

static unsigned key_poll(struct file *mfile, struct poll_table_struct *pt)//인터럽트 종료 함수
{
	int mask = 0;					//인터럽트 상태 체크 변수
	poll_wait(mfile, &waitqueue, pt);//큐에 대기시킨다 (sleep로 변형)
	
	if (event_flag > 0)
		mask |= (POLLIN | POLLRDNORM);//읽을 일반 데이터가 존재(인터럽트가 발생했었다)
	
	event_flag = 0;					//flag 값을 다시 0
	return mask;					//상태 return
}

static int _open(struct inode *minode, struct file *mfile)//디바이스 open 함수
{
	int i, k;						//gpio select를 위한 반복문 변수
	int result;						//요청한 인터럽트 결과확인 변수
	
	if (_usage != 0)				//드라이버가 0이 아니면 이미 open되었다
		return -EBUSY;				//는 뜻이므로 return 시켜 함수 종료
	
	_usage = 1;						//드라이버가 open 되었음을 체크
	_map = ioremap(GPIO_BASE, _SIZE);//물리적 매핑
	
	if (!_map)						//매핑 오류처리
	{
		printk("error: mapping gpio memory");
		iounmap(_map);						//매핑 되어 있을경우 매핑을 끊는다
		return -EBUSY;
	
	}
	led = (volatile unsigned int *)_map;	//레지스터로 접근하기 위한 4바이트 포인터지정
	
	for(k=0;k<=2;k++)						//gpio 0~27까지 모두 OUT으로 설정
		for (i = 0; i <= 9; i++)
		{
			if (k == 0 && (3 * i == 21))continue;//단, gpio 7은 IN으로 설정을 위해 넘어간다
			*(led + k) &= ~(0x7 << (3 * i));	//gpio select
			*(led + k) != (0x1 << (3 * i));		//set gpio out
		}
	
	*(led) &= ~(0x7 << (7 * 3));				//gpio 7 select
	*(led + 22) != (0x1 << 7);					//set GPFENO to 1 to detect Falling edge
	
	result = request_irq(gpio_to_irq(7), ind_interrupt_handler, IRQF_TRIGGER_FALLING, "gpio_irq_key", NULL);//gpio 7번에 대한 인터럽트 셋팅										

	if (result < 0)								//인터럽트 요청에 실패
	{
		printk("error: request_irq()");
		return result;
	}
	return 0;
}

static int _release(struct inode *minode, struct file *mfile)//디바이스 release(close) 함수
{
	_usage = 0;									//드라이브 상태 0으로 변경

	if (led)
		iounmap(led);							//드라이브 매핑 해제
	free_irq(gpio_to_irq(7), NULL);				//ISR 해제
	return 0;
}

static int _write(struct file *mfile, const char *gdata, size_t length, loff_t *off_what)//디바이스 데이터 write 함수
{
	char tmp_buf;								//gpio set 할 번호를 선택하는 변수
	int result;
	int left, right;							//copy from user 함수 상태 확인 변수
												//left : gpio address 설정, right : gpio shift 크기 설정
	
	/*아래의 16진수 비교문들은 user가 보낸 16진수에 따라 gpio가 동적으로 write 된다*/
	if (0xFF == *gdata)							//Matrix 0FF(gpio clear)
	{
		left = 10;
		right = 0;
	}

	if (0x80 == *gdata || 0x60 == *gdata || 0x18 == *gdata || 0x07 == *gdata || 0x01 == *gdata)
	{											//gpio 10~19번 write
		left = 7;
		right = 10;
	}

	if (0x6B == *gdata || 0x77 == *gdata || 0x3F == *gdata)//gpio 0~9번 write
	{
		left = 7;
		right = 0;
	}

	if (0xC0 == *gdata || 0x30 == *gdata || 0x20 == *gdata || 0x10 == *gdata)//gpio 20~29번 write
	{
		left = 7;
		right = 20;
	}

	result = copy_from_user(&tmp_buf, gdata, length);//user 에서 데이터를 받아온다
	if (result < 0)					//데이터를 정상적으로 못받아왔으면 0
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
		*(led + left) = (tmp_buf << right);			//16진수 조건문에서 설정한 left,right 값에 따라 gpio 설정
		*(led + 10) = (0x01 << 8);					//BUZZER gpio 8 활성화
		*(led + 7) = (0x01 << 9);					//BUZZER gpio 9 활성화
	}
	return length;
}

static struct file_operations _fops =				//파일 연산 구조체
{
	.owner = THIS_MODULE,							//모듈 소유장
	.open = _open,									//open
	.release = _release,							//release
	.write = _write,								//write
	.poll = key_poll,								//poll
};

static int _init(void)								//드라이버 적재 함수
{
	int result;										//드라이버 적재 상태 변수

	result = register_chrdev(_MAJOR, _NAME, &_fops);//드라이버를 적재한다
	
	if (result < 0)									//드라이버 적재에 실패
	{
		printk("KERN_WARNING Can't get any major!\n");
		return result;
	}
	return 0;
}

static void _exit(void)								//드라이버 제거함수
{
	unregister_chrdev(_MAJOR, _NAME);				//드라이버 제거
	printk("module removed.\n");
}

module_init(_init);									//커널모듈 적재
module_exit(_exit);									//커널 적재모듈 제거

MODULE_LICENSE("GPL")								//모듈 라이선스