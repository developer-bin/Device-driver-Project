#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <linux/kdev_t.h>
#include <time.h>
#include <linux/poll.h>
#include <signal.h>

#define _FILE_NAME "\dev\led_driver"	//����̺� ��ġ
#define MAX_VALUE 100					//8x8 Dot Matrix���� ���� on, off �ϱ����� �ݺ��ϴ� loop�� �ִ밪

int main(int argc, char **argv)
{
	time_t timer;						//time.h ���Ͽ��� �ü�� �ð��� ���ϱ� ���� ����
	struct tm *t;						//�ð����� ������ ����ü ������ ����
	struct pollfd events[2];			//üũ�� �̺�Ʈ (switch ������) ������ ������ ����
	int retval;							//event ���� üũ ����
	int fd;								//����̽� ���� üũ
	int i, j, k;						//Dot Matrix, Motor�� �����ϱ� ���� loop�� ������
	int h, m, r;						//�ð��� �Է��ϱ� ���� ����

	char matrix[5][2] = {				//8x8 Dot LED�� �ѱ����� gpio 16���� �迭
		{0xFF, 0x80},					//matrix[0~4][0]�� Dot matrix row �κ��� �����ϰ� matrix[0~4][1]�� Dot matrix col �κ��� ����
		{0x3F, 0x60},
		{0x00, 0x18},
		{0x6B, 0x07},
		{0x77, 0x01},
	};

	char motor[5] = {					//���� ��� ���� gpio 16���� �迭
		0xC0, 0x30,						//����, ����
		0x20, 0x10,						//��ȸ��, ��ȸ��
		0x02							//exit
	};

	fd = open(_FILE_NAME, O_RDWR);		//����̽� ����

	if (fd < 0)							//����̽��� ���������� ���µ��� ������ ���� ó�� �� ����
	{
		fprintf(stderr, "Can't open %s\n", LED_FILE_NAME);
		return -1;
	}

	printf("Please input the hour ");
	scanf("%d", &h);					//�˶� ���� �ð� �Է�

	printf("Please input the minute ");
	scanf("%d", &m);					//�˶� ���� �� �Է�

	while (1)
	{
		timer = time(NULL);				//�ü�� ����ð��� �ʴ����� ������� �Լ�
		t = localtime(&timer);			//�� ���� �ð��� �и��Ͽ� ������ �ִ´�

		events[0].fd = fd;				//üũ�� �̺�Ʈ ��ũ����
		events[0].events = POLLIN;		//������ �б�, ��Ʈ������ ����

		retval = poll(events, 1, 1000);	//event waiting(switch�� �������� ��ٸ�)
										//events: �̺�Ʈ����, 1: �̺�Ʈ ��, 1000: ���ð�

		if (retval < 0)					//�̺�Ʈ ���� ó��
		{
			fprintf(stderr, "Poll error\n");
			exit(0);
		}

		if ((t->tm_hour == h) && (t->tm_min == m))//�ü�� �ð��� �ڽ��� ������ �ð��� ��ġ -> alarm ����
		{
			r = rand() % 4;				//���� ������ �������� �����ϱ� ���� �ε����� rand �Լ�
			if (events[0].revents & POLLIN)//����ġ�� ������ ��� -> �߻��� �̺�Ʈ�� POLLIN���� ��
			{
				write(fd, &motor[4], sizeof(char));//�������� (motor[4]�� ���Ͱ��� gpio�� ��� clear ��Ű�ڴٴ� �ǹ�)
				write(fd, &matrix[2][0], sizeof(char));//Dot matrix�� ��� ����(matrix[2][0]�� Dot matrix gpio������ ��� clear ��Ű�ڴٴ� �ǹ�)
				break;
				//����ġ�� �˶��� ���ٴ� �ǹ��̹Ƿ� ���α׷� ����(while(1)���� ���´�)
			}

			for (k = 0; k < MAX_VALUE; k++)		//Dot matrix�� �����ϱ� ���� �ݺ���
			{
				for (i = 0; i < 5; i++)//matrix �迭 ũ��� matrix[5][2] �̴�. �̰��� 5���� ��ȸ�� �ؾ� �ܾ ����� �ȴٴ� �ǹ�
				{
					write(fd, &matrix[2][0], sizeof(char));//�ʱⰪ �Ǵ� ���� matrix�� �ѱ� ���ؼ��� ���� matrix���� clear�Ѵ�
					write(fd, &motor[r], sizeof(char));	//rand�Լ��� ���� �Է¹��� ���� �̿��� ���͹��� ����
					for (j = 0; j < 2; j++)				//col �κ� ����
						write(fd, &matrix[i][j], sizeof(char));//matrix�� �ݺ����� ���鼭 set ��Ų��
					usleep(1000);						//1000 ����ũ���� �ð����� Matrix[i][j] ���� ���
				}

				if (events[0].revents & POLLIN)			//����ġ�� ������ ���
				{
					write(fd, &motor[4], sizeof(char));	//���� ����
					write(fd, &matrix[2][0], sizeof(char));//Matrix�� ��� clear
					break;
				}
			}

			printf("Wake UP!!!!\n");
			write(fd, &matrix[2][0], sizeof(char));		//Matrix�� clear -> �̺κ��� ���� Matrix�� on, off�� �ݺ��Ѵ�
			write(fd, &motor[r], sizeof(char));			//���͹��� ����

			if (r == 2 || r == 3)						//��ȸ�� �Ǵ� ��ȸ���� �� ��� 1�� ���� �����Ѵ�
			{
				write(fd, &motor[1], sizeof(char));
				sleep(1);
			}

			write(fd, &motor[4], sizeof(char));			//���Ͱ� ��� clear -> ���� ���� ����

			if (events[0].revents & POLLIN)				//����ġ�� ������ ���
			{
				write(fd, &motor[4], sizeof(char));		//���� ����
				write(fd, &matrix[2][0], sizeof(char));	//Matrix�� ��� clear
				break;
			}
		}
		else											//�Ϲݻ��� (alarm ���°� �ƴ� ���)
		{
			write(fd, &matrix[2][0], sizeof(char));		//Matrix gpio�� ��� clear
			write(fd, &motor[4], sizeof(char));			//���� gpio�� ��� clear
			printf("%d : %d\n",t->tm_hour,t->tm_min)	//���� �ü�� �ð�, ���� ���
		}
	}
	close(fd);											//���� close
	return 0;
}