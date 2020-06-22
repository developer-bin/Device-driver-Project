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

#define _FILE_NAME "\dev\led_driver"	//드라이브 위치
#define MAX_VALUE 100					//8x8 Dot Matrix에서 불을 on, off 하기위해 반복하는 loop의 최대값

int main(int argc, char **argv)
{
	time_t timer;						//time.h 파일에서 운영체제 시간을 구하기 위한 변수
	struct tm *t;						//시간값을 꺼내는 구조체 포인터 변수
	struct pollfd events[2];			//체크할 이벤트 (switch 눌러짐) 정보를 저장할 변수
	int retval;							//event 상태 체크 변수
	int fd;								//디바이스 상태 체크
	int i, j, k;						//Dot Matrix, Motor를 수행하기 위한 loop의 변수들
	int h, m, r;						//시간을 입력하기 위한 변수

	char matrix[5][2] = {				//8x8 Dot LED를 켜기위한 gpio 16진수 배열
		{0xFF, 0x80},					//matrix[0~4][0]은 Dot matrix row 부분을 제어하고 matrix[0~4][1]은 Dot matrix col 부분을 제어
		{0x3F, 0x60},
		{0x00, 0x18},
		{0x6B, 0x07},
		{0x77, 0x01},
	};

	char motor[5] = {					//모터 제어를 위한 gpio 16진수 배열
		0xC0, 0x30,						//후진, 전진
		0x20, 0x10,						//좌회전, 우회전
		0x02							//exit
	};

	fd = open(_FILE_NAME, O_RDWR);		//디바이스 오픈

	if (fd < 0)							//디바이스가 정상적으로 오픈되지 않으면 오류 처리 후 종료
	{
		fprintf(stderr, "Can't open %s\n", LED_FILE_NAME);
		return -1;
	}

	printf("Please input the hour ");
	scanf("%d", &h);					//알람 맞출 시간 입력

	printf("Please input the minute ");
	scanf("%d", &m);					//알람 맞출 분 입력

	while (1)
	{
		timer = time(NULL);				//운영체제 현재시각을 초단위로 얻기위한 함수
		t = localtime(&timer);			//초 단위 시각을 분리하여 변수에 넣는다

		events[0].fd = fd;				//체크할 이벤트 디스크립터
		events[0].events = POLLIN;		//데이터 읽기, 비트값으로 지정

		retval = poll(events, 1, 1000);	//event waiting(switch가 눌러지길 기다림)
										//events: 이벤트정보, 1: 이벤트 수, 1000: 대기시간

		if (retval < 0)					//이벤트 오류 처리
		{
			fprintf(stderr, "Poll error\n");
			exit(0);
		}

		if ((t->tm_hour == h) && (t->tm_min == m))//운영체제 시간과 자신이 설정한 시간이 일치 -> alarm 시작
		{
			r = rand() % 4;				//모터 방향을 랜덤으로 설정하기 위한 인덱스와 rand 함수
			if (events[0].revents & POLLIN)//스위치를 눌렀을 경우 -> 발생한 이벤트가 POLLIN인지 비교
			{
				write(fd, &motor[4], sizeof(char));//모터종료 (motor[4]는 모터관련 gpio를 모두 clear 시키겠다는 의미)
				write(fd, &matrix[2][0], sizeof(char));//Dot matrix를 모두 끈다(matrix[2][0]은 Dot matrix gpio선들을 모두 clear 시키겠다는 의미)
				break;
				//스위치는 알람을 끈다는 의미이므로 프로그램 종료(while(1)에서 나온다)
			}

			for (k = 0; k < MAX_VALUE; k++)		//Dot matrix를 제어하기 위한 반복문
			{
				for (i = 0; i < 5; i++)//matrix 배열 크기는 matrix[5][2] 이다. 이것은 5까지 순회를 해야 단어가 출력이 된다는 의미
				{
					write(fd, &matrix[2][0], sizeof(char));//초기값 또는 다음 matrix를 켜기 위해서는 이전 matrix값을 clear한다
					write(fd, &motor[r], sizeof(char));	//rand함수를 통해 입력받은 값을 이용해 모터방향 제어
					for (j = 0; j < 2; j++)				//col 부분 제어
						write(fd, &matrix[i][j], sizeof(char));//matrix를 반복문을 돌면서 set 시킨다
					usleep(1000);						//1000 마이크로초 시간동안 Matrix[i][j] 값을 출력
				}

				if (events[0].revents & POLLIN)			//스위치를 눌렀을 경우
				{
					write(fd, &motor[4], sizeof(char));	//모터 종료
					write(fd, &matrix[2][0], sizeof(char));//Matrix값 모두 clear
					break;
				}
			}

			printf("Wake UP!!!!\n");
			write(fd, &matrix[2][0], sizeof(char));		//Matrix값 clear -> 이부분을 통해 Matrix는 on, off를 반복한다
			write(fd, &motor[r], sizeof(char));			//모터방향 제어

			if (r == 2 || r == 3)						//좌회전 또는 우회전을 할 경우 1초 동안 전진한다
			{
				write(fd, &motor[1], sizeof(char));
				sleep(1);
			}

			write(fd, &motor[4], sizeof(char));			//모터값 모두 clear -> 모터 최종 종료

			if (events[0].revents & POLLIN)				//스위치를 눌렀을 경우
			{
				write(fd, &motor[4], sizeof(char));		//모터 종료
				write(fd, &matrix[2][0], sizeof(char));	//Matrix값 모두 clear
				break;
			}
		}
		else											//일반상태 (alarm 상태가 아닌 경우)
		{
			write(fd, &matrix[2][0], sizeof(char));		//Matrix gpio값 모두 clear
			write(fd, &motor[4], sizeof(char));			//모터 gpio값 모두 clear
			printf("%d : %d\n",t->tm_hour,t->tm_min)	//현재 운영체제 시간, 분을 출력
		}
	}
	close(fd);											//파일 close
	return 0;
}