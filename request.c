
#include <stdio.h>
#include <fcntl.h>
#include "vmm.h"


int main()
{
	MemoryAccessRequest req;
	int fd, temp, flag = 0, item;

	srandom(time(NULL));

	while(1)
	{

		printf("请输入访存请求的类型。P代表自定义请求，R代表随机请求\n");

		if(getchar() == 'P')
		{

			req.program = random() % 2;


			printf("请输入访存类型P。0代表读出， 1代表写入，2代表执行\n");

			scanf("%d", &temp);
			switch(temp)
			{
				case 0:
					req.reqType = REQUEST_READ;
					break;
				case 1:
					req.reqType = REQUEST_WRITE;
					break;
				case 2:
					req.reqType = REQUEST_EXECUTE;
					break;
				default:
					printf("非法访存类型。正确格式为：0代表读出， 1代表写入，2代表执行\n");
					flag = 1;
					break;
			}
			if(! flag)
			{
				printf("请输入要访问的虚存地址，虚存地址要小于32*4=128\n");
				scanf("%d", &item);
                req.virAddr = item;
				if(temp == 1)
				{
					printf("请输入要写入的数值\n");
					scanf("%d", &item);
					req.value = item % 0xFFu;
					printf("产生请求：\n程序:%d\t地址：%u\t类型：写入\t值：%d\n", req.program, req.virAddr, req.value);
				}
				else if(temp == 0)
					printf("产生请求：\n程序:%d\t地址：%u\t类型：读取\n", req.program, req.virAddr);
				else
					printf("产生请求：\n程序:%d\t地址：%u\t类型：执行\n", req.program, req.virAddr);

			}
			getchar();
		}
		else
		{
			getchar();
			req.program = random() % 2;
			req.virAddr = random() % ONE_PROGRAM_SIZE;
			switch (random() % 3)
			{
				case 0:
				{
					req.reqType = REQUEST_READ;
					printf("产生请求：\n程序:%d\t地址：%u\t类型：读取\n", req.program, req.virAddr);
					break;
				}
				case 1:
				{
					req.reqType = REQUEST_WRITE;
					req.value = random() % 0xFFu;
					printf("产生请求：\n程序:%d\t地址：%u\t类型：写入\t值：%d\n", req.program, req.virAddr, req.value);
					break;
				}
				case 2:
				{
					req.reqType = REQUEST_EXECUTE;
					printf("产生请求：\n程序:%d\t地址：%u\t类型：执行\n", req.program, req.virAddr);
					break;
				}
				default:
				break;
			}
		}

		if(! flag)
		{
			if((fd = open("/tmp/server",O_WRONLY)) < 0)
				printf("request open fifo failed");
			else if(write(fd,&req,sizeof(MemoryAccessRequest)) < 0)
				printf("request write failed");
			close(fd);
		}
		flag = 0;
	}
}
