#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "vmm.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* 页表modified */
PageTableItem pageTable[FIRST_PAGE_SUM][SECOND_PAGE_SUM];
/* 实存空间 */
BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
FILE *ptr_auxMem;
/* 物理块使用标识 */
BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
//Ptr_MemoryAccessRequest ptr_memAccReq;

int fifo;

unsigned long systemTime = 0;


/* 初始化环境 */
void do_init()
{
	int i, j;
	systemTime = 0;
	srandom(time(NULL));
	for(i = 0; i < FIRST_PAGE_SUM; i++)
	{
		for(j = 0; j < SECOND_PAGE_SUM; j++)
		{
			if(i < 2)
				pageTable[i][j].program = 0;
			else
				pageTable[i][j].program = 1;

			pageTable[i][j].pageNum = j;//页号
			pageTable[i][j].filled = FALSE;
			pageTable[i][j].edited = FALSE;
			pageTable[i][j].lastVisitTime= 0;
			

			/* 使用随机数设置该页的保护类型 */
			switch (random() % 7)
			{
				case 0:
				{
					pageTable[i][j].proType = READABLE;
					break;
				}
				case 1:
				{
					pageTable[i][j].proType = WRITABLE;
					break;
				}
				case 2:
				{
					pageTable[i][j].proType = EXECUTABLE;
					break;
				}
				case 3:
				{
					pageTable[i][j].proType = READABLE | WRITABLE;
					break;
				}
				case 4:
				{
					pageTable[i][j].proType = READABLE | EXECUTABLE;
					break;
				}
				case 5:
				{
					pageTable[i][j].proType = WRITABLE | EXECUTABLE;
					break;
				}
				case 6:
				{
					pageTable[i][j].proType = READABLE | WRITABLE | EXECUTABLE;
					break;
				}
				default:
					break;
			}
			/* 设置该页对应的辅存地址 */
			pageTable[i][j].auxAddr = (SECOND_PAGE_SUM*i + j) * PAGE_SIZE;
		}
		
	}
	for (j = 0; j < BLOCK_SUM; j++)
	{
		/* 随机选择一些物理块进行页面装入 */
		if (random() % 2 == 0)
		{
			do_page_in(&pageTable[j/16][j%16], j);
			pageTable[j/16][j%16].blockNum = j;
			pageTable[j/16][j%16].filled = TRUE;
			blockStatus[j] = TRUE;
		}
		else
			blockStatus[j] = FALSE;
	}
}


/* 响应请求 */
void do_response()
{
    int count;
	Ptr_PageTableItem ptr_pageTabIt;
	unsigned int firstPageNum, secondPageNum, offAddr;
	unsigned int actAddr;
    
    MemoryAccessRequest req;
    Ptr_MemoryAccessRequest ptr_memAccReq;
    bzero(&req,sizeof(MemoryAccessRequest));
    ptr_memAccReq = &req;

    systemTime++;
    
    //读取FIFO中的一个访存请求结构体
    if((count = read(fifo, ptr_memAccReq, sizeof(MemoryAccessRequest))) < 0)
        printf("read fifo failed");
    
    else if(count > 0)
    {
        /* 检查地址是否越界 */
        if (ptr_memAccReq->virAddr< 0 || ptr_memAccReq->virAddr >= ONE_PROGRAM_SIZE)
        {
            do_error(ERROR_OVER_BOUNDARY);
            return;
        }
        /* 计算页号和页内偏移值 */
        firstPageNum = ((ptr_memAccReq->virAddr + ptr_memAccReq->program * ONE_PROGRAM_SIZE) / PAGE_SIZE) / SECOND_PAGE_SUM;
        secondPageNum = ((ptr_memAccReq->virAddr + ptr_memAccReq->program * ONE_PROGRAM_SIZE) / PAGE_SIZE) % SECOND_PAGE_SUM;
        offAddr = (ptr_memAccReq->virAddr + ptr_memAccReq->program * ONE_PROGRAM_SIZE) % PAGE_SIZE;
        
        printf("程序:%d\t虚拟地址为%lu\n", ptr_memAccReq->program, ptr_memAccReq->virAddr);
        printf("页目录为：%u\t页号为：%u页内偏移为：%u\n", firstPageNum, secondPageNum, offAddr);
        
        /* 获取对应页表项 */
        ptr_pageTabIt = &pageTable[firstPageNum][secondPageNum];
        
        /* 根据特征位决定是否产生缺页中断 */
        if (!ptr_pageTabIt->filled)
        {
            do_page_fault(ptr_pageTabIt);
        }
        
        actAddr = ptr_pageTabIt->blockNum * PAGE_SIZE + offAddr;
        printf("实地址为：%u\n", actAddr);
        /* 检查页面访问权限并处理访存请求 */
        switch (ptr_memAccReq->reqType)
        {
            case REQUEST_READ: //读请求
            {
                //ptr_pageTabIt->count++;
                ptr_pageTabIt->lastVisitTime = systemTime;
                if (!(ptr_pageTabIt->proType & READABLE)) //页面不可读
                {
                    do_error(ERROR_READ_DENY);
                    return;
                }
                /* 读取实存中的内容 */
                printf("读操作成功：读到的值为%d\n", actMem[actAddr]);
                break;
            }
            case REQUEST_WRITE: //写请求
            {
                //ptr_pageTabIt->count++;
                ptr_pageTabIt->lastVisitTime = systemTime;
                if (!(ptr_pageTabIt->proType & WRITABLE)) //页面不可写
                {
                    do_error(ERROR_WRITE_DENY);
                    return;
                }
                /* 向实存中写入请求的内容 */
                actMem[actAddr] = ptr_memAccReq->value;
                ptr_pageTabIt->edited = TRUE;
                printf("写操作成功\n 写入的值为%d\n", actMem[actAddr]);
                break;
            }
            case REQUEST_EXECUTE: //执行请求
            {
                //ptr_pageTabIt->count++;
                ptr_pageTabIt->lastVisitTime = systemTime;
                if (!(ptr_pageTabIt->proType & EXECUTABLE)) //页面不可执行
                {
                    do_error(ERROR_EXECUTE_DENY);
                    return;
                }			
                printf("执行成功\n");
                break;
            }
            default: //非法请求类型
            {	
                do_error(ERROR_INVALID_REQUEST);
                return;
            }
        }
    }
}

/* 处理缺页中断 */
void do_page_fault(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i;
	printf("产生缺页中断，开始进行调页...\n");
	for (i = 0; i < BLOCK_SUM; i++)
	{
		if (!blockStatus[i])
		{
			/* 读辅存内容，写入到实存 */
			do_page_in(ptr_pageTabIt, i);
			
			/* 更新页表内容 */
			ptr_pageTabIt->blockNum = i;
			ptr_pageTabIt->filled = TRUE;
			ptr_pageTabIt->edited = FALSE;
			//ptr_pageTabIt->count = 0;
			ptr_pageTabIt->lastVisitTime = systemTime;
			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	//do_LFU(ptr_pageTabIt);
    do_LRU(ptr_pageTabIt);
}

/* 根据LFU算法进行页面替换 */
void do_LFU(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i, j, min, firstpage, secondpage;
	printf("没有空闲物理块，开始进行LFU页面替换...\n");
	if(ptr_pageTabIt->program == 0)
	{
		for (i = 0, min = 0xFFFFFFFF, firstpage = 0, secondpage = 0; i < 2; i++)
		{
			for(j = 0; j < SECOND_PAGE_SUM; j++)
			{
				if ((pageTable[i][j].filled == TRUE) && (pageTable[i][j].count < min))//Questiion:if ((pageTable[i].filled == TRUE) && (pageTable[i].count < min))
				{
					min = pageTable[i][j].count;
					firstpage = i;
					secondpage = j;
				}
			}
		}
	}
	else
	{
		for (i = 0, min = 0xFFFFFFFF, firstpage = 0, secondpage = 0; i < FIRST_PAGE_SUM; i++)
		{
			for(j = 0; j < SECOND_PAGE_SUM; j++)
			{
				if ((pageTable[i][j].filled == TRUE) && (pageTable[i][j].count < min))//Questiion:if ((pageTable[i].filled == TRUE) && (pageTable[i].count < min))
				{
					min = pageTable[i][j].count;
					firstpage = i;
					secondpage = j;
				}
			}
		}
	}
	
	printf("选择第%u页目录的第%u页进行替换\n", firstpage, secondpage);
	if (pageTable[firstpage][secondpage].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&pageTable[firstpage][secondpage]);
	}
	pageTable[firstpage][secondpage].filled = FALSE;
	pageTable[firstpage][secondpage].count = 0;


	/* 读辅存内容，写入到实存 */
	do_page_in(ptr_pageTabIt, pageTable[firstpage][secondpage].blockNum);
	
	/* 更新页表内容 */
	ptr_pageTabIt->blockNum = pageTable[firstpage][secondpage].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;
	printf("页面替换成功\n");
}

/* 根据LRU算法进行页面替换 */
void do_LRU(Ptr_PageTableItem ptr_pageTabIt)
{
    unsigned int i, j, min, firstpage, secondpage;
    printf("没有空闲物理块，开始进行LRU页面替换...\n");
    if(ptr_pageTabIt->program == 0)
    {
    	for (i = 0, min = 0xFFFFFFFF, firstpage = 0, secondpage = 0; i < 2; i++)
    	{
       		 for(j = 0; j < SECOND_PAGE_SUM; j++)
        	{
            	if ((pageTable[i][j].filled == TRUE) && (pageTable[i][j].lastVisitTime < min))//Questiion:if ((pageTable[i].filled == TRUE) && (pageTable[i].count < min))
            	{
                	min = pageTable[i][j].lastVisitTime;
                	firstpage = i;
                	secondpage = j;
            	}
       		}
    	}
    }
    else
    {
    	for (i = 2, min = 0xFFFFFFFF, firstpage = 0, secondpage = 0; i < 4; i++)
    	{
       		 for(j = 0; j < SECOND_PAGE_SUM; j++)
        	{
            	if ((pageTable[i][j].filled == TRUE) && (pageTable[i][j].lastVisitTime < min))//Questiion:if ((pageTable[i].filled == TRUE) && (pageTable[i].count < min))
            	{
                	min = pageTable[i][j].lastVisitTime;
                	firstpage = i;
                	secondpage = j;
            	}
       		}
    	}

    }
    
    printf("选择第%u页目录的第%u页进行替换\n", firstpage, secondpage);
    if (pageTable[firstpage][secondpage].edited)
    {
        /* 页面内容有修改，需要写回至辅存 */
        printf("该页内容有修改，写回至辅存\n");
        do_page_out(&pageTable[firstpage][secondpage]);
    }
    pageTable[firstpage][secondpage].filled = FALSE;
    pageTable[firstpage][secondpage].lastVisitTime = 0;
    
    
    /* 读辅存内容，写入到实存 */
    do_page_in(ptr_pageTabIt, pageTable[firstpage][secondpage].blockNum);
    
    /* 更新页表内容 */
    ptr_pageTabIt->blockNum = pageTable[firstpage][secondpage].blockNum;
    ptr_pageTabIt->filled = TRUE;
    ptr_pageTabIt->edited = FALSE;
    ptr_pageTabIt->lastVisitTime = systemTime;
    printf("页面替换成功\n");
}

/* 将辅存内容写入实存 */
void do_page_in(Ptr_PageTableItem ptr_pageTabIt, unsigned int blockNum)
{
	unsigned int readNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((readNum = fread(actMem + blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: blockNum=%u\treadNum=%u\n", blockNum, readNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("调页成功：辅存地址%u-->>物理块%u\n", ptr_pageTabIt->auxAddr, blockNum);
}

/* 将被替换页面的内容写回辅存 */
void do_page_out(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int writeNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((writeNum = fwrite(actMem + ptr_pageTabIt->blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: writeNum=%u\n", writeNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_WRITE_FAILED);
		exit(1);
	}
	printf("写回成功：物理块%u-->>辅存地址%03X\n", ptr_pageTabIt->auxAddr, ptr_pageTabIt->blockNum);
}

/* 错误处理 */
void do_error(ERROR_CODE code)
{
	switch (code)
	{
		case ERROR_READ_DENY:
		{
			printf("访存失败：该地址内容不可读\n");
			break;
		}
		case ERROR_WRITE_DENY:
		{
			printf("访存失败：该地址内容不可写\n");
			break;
		}
		case ERROR_EXECUTE_DENY:
		{
			printf("访存失败：该地址内容不可执行\n");
			break;
		}		
		case ERROR_INVALID_REQUEST:
		{
			printf("访存失败：非法访存请求\n");
			break;
		}
		case ERROR_OVER_BOUNDARY:
		{
			printf("访存失败：地址越界\n");
			break;
		}
		case ERROR_FILE_OPEN_FAILED:
		{
			printf("系统错误：打开文件失败\n");
			break;
		}
		case ERROR_FILE_CLOSE_FAILED:
		{
			printf("系统错误：关闭文件失败\n");
			break;
		}
		case ERROR_FILE_SEEK_FAILED:
		{
			printf("系统错误：文件指针定位失败\n");
			break;
		}
		case ERROR_FILE_READ_FAILED:
		{
			printf("系统错误：读取文件失败\n");
			break;
		}
		case ERROR_FILE_WRITE_FAILED:
		{
			printf("系统错误：写入文件失败\n");
			break;
		}
		default:
		{
			printf("未知错误：没有这个错误代码\n");
		}
	}
}

/* 打印页表 */
void do_print_info()
{
	unsigned int i, j, k;
	char str[4];
	printf("页目录\t页号\t块号\t装入\t修改\t保护\t程序\t最近访问时间\t辅存\n");
	for (i = 0; i < FIRST_PAGE_SUM; i++)
	{
		for(j = 0; j < SECOND_PAGE_SUM; j++)
		{
			printf("%u\t%u\t%u\t%u\t%u\t%s\t%d\t%u\t%u\n", i, j, pageTable[i][j].blockNum, pageTable[i][j].filled, 
			pageTable[i][j].edited, get_proType_str(str, pageTable[i][j].proType), (i < 2 ? 0 : 1) ,
			pageTable[i][j].lastVisitTime, pageTable[i][j].auxAddr);
		}
	}
}

//打印辅存
void do_print_aux()
{
    int ch, i = 0;
    printf("即将开始打印辅存内容，以ASCII形式打印\n");
    FILE* new = fopen(AUXILIARY_MEMORY,"r");
    while(i < 256 && (ch = fgetc(new)) != EOF)
    {
        printf("[辅存号: %d 内容: %d]\t", i, ch);
        if(i % 3 == 2)
        	puts("");
        i++;
    }
    puts("");
}

//打印实存
void do_print_actMem()
{
    int i;
    printf("即将开始打印实存内容，以ASCII形式打印\n");
    for(i = 0; i < ACTUAL_MEMORY_SIZE; i++)
    {
        printf("[实存号: %d 内容: %d]\t", i, actMem[i]);
        if(i % 3 == 2)
        	puts("");
    }
    puts("");
}

/* 获取页面保护类型字符串 */
char *get_proType_str(char *str, BYTE type)
{
	if (type & READABLE)
		str[0] = 'r';
	else
		str[0] = '-';
	if (type & WRITABLE)
		str[1] = 'w';
	else
		str[1] = '-';
	if (type & EXECUTABLE)
		str[2] = 'x';
	else
		str[2] = '-';
	str[3] = '\0';
	return str;
}

int main(int argc, char* argv[])
{
	char c;
	int i;
    struct stat statbuf;
    
	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "r+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}
	
	do_init();
	do_print_info();
    do_print_aux();
    do_print_actMem();
    
    //建立FIFO
    if(stat("/tmp/server",&statbuf) == 0)
    {
        if(remove("/tmp/server") < 0)
            printf("remove failed");
    }
    if(mkfifo("/tmp/server",0666) < 0)
        printf("mkfifo failed");
    //打开FIFO
    if((fifo = open("/tmp/server", O_RDONLY | O_NONBLOCK)) < 0)
        printf("open fifo failed");
    
	/* 在循环中模拟访存请求与处理过程 */
	while (TRUE)
	{
		do_response();
		printf("按Y打印页表，按其他键不打印...\n");
		if ((c = getchar()) == 'y' || c == 'Y')
            do_print_info();
		while (c != '\n')
			c = getchar();
		printf("按X退出程序，按其他键继续...\n");
		if ((c = getchar()) == 'x' || c == 'X')
			break;
		while (c != '\n')
			c = getchar();
        printf("按A打印辅存，按其他键不打印...\n");
        if ((c = getchar()) == 'a' || c == 'A')
            do_print_aux();
        while (c != '\n')
            c = getchar();
        printf("按M打印实存，按其他键不打印...\n");
        if ((c = getchar()) == 'm' || c == 'M')
            do_print_actMem();
        while (c != '\n')
            c = getchar();
		//sleep(5000);
	}

	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}
    close(fifo);
	return (0);
}
