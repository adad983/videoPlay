#include <stdio.h>
#include <sys/types.h> /* See NOTES */
#include <sys/socket.h>
#include <strings.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>

#include "kernel_list.h"

// mplayer out.mp4 -quiet -zoom -x 800 -y 480
// ffmpeg -i 123.mp4   -vf "transpose=2" 234.mp4
#define SERVER_ADDR "tucdn.wpon.cn"
#define LEFT 0
#define RIGHT 1
#define PAUSE_CONTINUE 2

int fd;
int name;
int pause_n;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

struct vedio_info
{
	char vedio_name[32];
	struct list_head list;
};


struct vedio_info *list_init(); //对链表进行初始化

struct vedio_info *__new_node(char *name); //新增链表节点，存放视频文件的名称

int touch_event(); //获取对开发板的点击事件的返回值（上滑、下滑、暂停，继续）

int connect_dest_web_server(const char *server_addr); //连接API服务器

int disconnect_dest_server(int srv_fd); //断开服务器连接



void *inital_src(void *arg)
{

	int skt_fd;
	struct vedio_info *head = (struct vedio_info *)arg;

	char buffer[40960] = {0};
	ssize_t rd_size;
	char *pos, *url_pos;
	int status_flag;

	char *request_meth = "GET";
	char URL[200] = "/api-girl/index.php?wpon=302";
	char *prot_ver = "HTTP/1.1";
	char *cmd_head = "Host:";
	char server_addr[40] = SERVER_ADDR;

	char request_cmd[1024]; // = "GET /api-girl/videos/BMjAyMTEwMjYxOTI3NTRfMjU2MjQ0NDg5Ml81OTcyNDE0NjQ1Nl8xXzM=_b_Bc4fc8976489ff1ada92a04acb9572fab.mp4 HTTP/1.1\r\nHost:tucdn.wpon.cn\r\n\r\n";

	while (1)
	{
		skt_fd = connect_dest_web_server(server_addr);

		sprintf(request_cmd, "%s %s %s\r\n%s%s\r\n\r\n", request_meth, URL, prot_ver, cmd_head, server_addr); //组合指令

		send(skt_fd, request_cmd, strlen(request_cmd), 0); //给服务器发送请求

		rd_size = recv(skt_fd, buffer, sizeof(buffer), 0); //接收服务器反馈的包，rd_size是返回包的长度

		// printf("size=%ld\n%s\n", rd_size, buffer);

		pos = strstr(buffer, " ") + 1; //找到第一个空格，+1跳过这个空格便是状态码的内存位置

		sscanf(pos, "%d", &status_flag); //将状态码通过整型数的方式提取出来

		// printf("状态码=%d\n", status_flag);

		switch (status_flag)
		{

		case 100 ... 199:
			printf("接收到一个通知\n");
			break;
		case 200 ... 299:
			printf("接收到服务器反馈成功的包\n");
			break;
		case 300 ... 399:
			printf("接收到服务器跟我们说要跳转地址啦\n");
			disconnect_dest_server(skt_fd);
			//分析出新的服务器地址与URL
			pos = strstr(buffer, "Location: ") + strlen("Location: //"); //找到域名的位置
			url_pos = strstr(pos, "/");
			strncpy(server_addr, pos, url_pos - pos);
			printf("域名为：%s\n", server_addr);
			bzero(URL, sizeof(URL));
			strncpy(URL, url_pos, strstr(url_pos, "\r\n") - url_pos);
			// printf("URL为：%s\n", URL);
			continue;

		default:
			printf("出现错误啦:错误码：%d\n", status_flag);
			goto connect_server_err;
		}

		break;
	}
	int load_size; //下载的文件长度
	int cur_size;

	pos = strstr(buffer, "Content-Length:") + strlen("Content-Length:");

	sscanf(pos, "%d", &load_size); //将状态码通过整型数的方式提取出来

	FILE *fp;

	printf("URL = %s\n", URL);
	printf("即将下载的文件长度:%d\n", load_size);

	pthread_mutex_lock(&m);

	name++;
	char filename[512];
	char dest_filename[512];
	char ffmpeg_command[1024];

	sprintf(filename, "%d.mp4", name);
	sprintf(dest_filename, "%d%d.mp4", name, name);

	if (1 == name)
	{
		strcpy(head->vedio_name, dest_filename);
	}
	else
	{
		struct vedio_info *new = __new_node(dest_filename);
		list_add_tail(&new->list, &head->list);
	}

	pthread_mutex_unlock(&m);

	fp = fopen(filename, "w");

	pos = strstr(buffer, "\r\n\r\n") + 4; //将pos指针指向响应包体

	fwrite(pos, rd_size - (pos - buffer), 1, fp); // rd_size-(pos-buffer)代表包体的\r\n\r\n后面的内容，也就是视频的内容

	// printf("开始接收数据，已经写入了%ld个字节\n", rd_size - (pos - buffer));

	// printf("等待...\n");

	for (cur_size = rd_size - (pos - buffer); cur_size < load_size; cur_size += rd_size)
	{
		rd_size = recv(skt_fd, buffer, sizeof(buffer), 0);

		fwrite(buffer, rd_size, 1, fp);

		// printf("读取到%ld个字节,总计下载了%ld个字节\n", rd_size, cur_size + rd_size);
	}

	// printf("成功\n");

	sprintf(ffmpeg_command, "ffmpeg -i %s -b:v 4000k -vf 'transpose=2' -s 800*480 %s", filename, dest_filename);
	system(ffmpeg_command);

	fclose(fp); //关闭文件

	pthread_exit(NULL);
connect_server_err:
	printf("cuowu");
	// return -1;
}

void *play_vedio(void *arg)
{
	struct vedio_info *head = (struct vedio_info *)arg;
	int ret;
	if (access("/tmp/my_fifo", F_OK))
	{
		ret = mkfifo("/tmp/my_fifo", 0777);
	}

	fd = open("/tmp/my_fifo", O_RDWR);
	if (-1 == fd)
	{
		perror("open fifo failed");
		exit(0);
	}

	char buf[1024];
	struct vedio_info *tmp = list_entry(&head->list, struct vedio_info, list);

	struct list_head *pos;
	list_for_each(pos, &head->list)
	{
		// 从小结构体指针pos，获得大结构体指针p
		struct vedio_info *p = list_entry(pos, struct vedio_info, list);
		printf("%s\t", p->vedio_name);
	}

	sprintf(buf, "mplayer -quiet -slave -loop 0 -input  file=/tmp/my_fifo %s", head->vedio_name);
	popen(buf, "r");

	while (1)
	{
		int get_touch = touch_event();
		if (get_touch == 0)
		{
			system("killall -9 mplayer");
			tmp = list_entry(tmp->list.next, struct vedio_info, list);
			printf("%s\n", tmp->vedio_name);
			sprintf(buf, "mplayer -quiet -slave -loop 0 -input file=/tmp/my_fifo %s", tmp->vedio_name);
			popen(buf, "r");
			printf("上滑\n");
		}
		else if (get_touch == 1)
		{
			system("killall -9 mplayer");
			tmp = list_entry(tmp->list.prev, struct vedio_info, list);
			printf("%s\n", tmp->vedio_name);
			sprintf(buf, "mplayer -quiet -slave -loop 0 -input file=/tmp/my_fifo %s", tmp->vedio_name);
			popen(buf, "r");
			printf("下滑\n");
		}
		else if (get_touch == 2)
		{
			pause_n++;

			if (pause_n % 2 == 0)
			{
				printf("暂停\n");
				system("killall -STOP mplayer");
			}
			if (pause_n % 2 == 1)
			{
				printf("继续\n");
				system("killall -CONT mplayer");
			}
		}
	}
}


int main(int argc, const char *argv[])
{

	system("rm *.mp4");

	struct vedio_info *head = list_init();

	printf("等待初始化\n");

	pthread_t tid[10];

	pthread_t play_tid;

	for (int i = 0; i < 10; i++)
	{

		pthread_create(tid + i, NULL, inital_src, head);
	}

	for (int i = 0; i < 10; i++)
	{
		pthread_join(tid[i], NULL);
		if (i == 9)
		{
			printf("初始化完毕\n");
		}
	}

	getchar();

	pthread_create(&play_tid, NULL, play_vedio, head);

	struct list_head *pos;
	list_for_each(pos, &head->list)
	{
		// 从小结构体指针pos，获得大结构体指针p
		struct vedio_info *p = list_entry(pos, struct vedio_info, list);
		printf("%s\t", p->vedio_name);
	}

	pthread_exit(NULL);
}

struct vedio_info *list_init()
{

	struct vedio_info *new = calloc(1, sizeof(struct vedio_info));
	if (new == NULL)
	{
		printf("fail to initail \n");
		return NULL;
	}

	INIT_LIST_HEAD(&new->list);

	return new;
}

struct vedio_info *__new_node(char *name)
{
	struct vedio_info *new = calloc(1, sizeof(struct vedio_info));

	if (new == NULL)
	{
		printf("fail to initail \n");
		return NULL;
	}

	INIT_LIST_HEAD(&new->list);

	strcpy(new->vedio_name, name);

	return new;
}

int touch_event()
{

	int e_x, e_y, s_x, s_y;
	int tp = open("/dev/input/event0", O_RDWR);

	struct input_event ts;
	int x, y;
	while (1)
	{
		read(tp, &ts, sizeof(ts));

		if (ts.type == EV_ABS)
		{
			if (ts.code == ABS_X)
			{
				x = ts.value * 800 / 1024;
			}
			if (ts.code == ABS_Y)
			{
				y = ts.value * 800 / 1024;
			}
		}

		else if (ts.type == EV_KEY && ts.code == BTN_TOUCH && ts.value == 1)
		{
			e_x = x;
			e_y = y;
		}
		else if (ts.type == EV_KEY && ts.code == BTN_TOUCH && ts.value == 0)
		{
			s_x = x;
			s_y = y;
			break;
		}
	}
	int differentx = e_x - s_x; //横向滑动的差值
	int differenty = e_y - s_y;

	if (abs(differentx) > abs(differenty))
	{
		if (differentx > 0) //从右往左
		{
			return LEFT;
		}
	}

	if (abs(differentx) > abs(differenty))
	{
		if (differentx < 0) //从右往左
		{
			return RIGHT;
		}
	}

	if (abs(differentx) == abs(differenty))
	{
		return PAUSE_CONTINUE;
	}
}

int connect_dest_web_server(const char *server_addr)
{
	struct hostent *hostinfo;
	int i;

	hostinfo = gethostbyname(server_addr);

	int skt_fd;
	int retval;

	skt_fd = socket(AF_INET, SOCK_STREAM, 0); //申请一个IPV4网络接口TCP的网络接口
	if (skt_fd == -1)
	{
		perror("获取TCP接口失败");
		return -1;
	}

	struct sockaddr_in dest_addr; // IPV4的地址结构体

	dest_addr.sin_family = AF_INET;										// IPV4的协议
	dest_addr.sin_port = htons(80);										//声明端口号,将端口号数据转化为网络字节序
	dest_addr.sin_addr = *(struct in_addr *)(hostinfo->h_addr_list[0]); //将字符串的地址转化为数字的网络字节序列

	printf("%d\n", __LINE__);
	retval = connect(skt_fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)); //连接服务器

	if (retval == -1)
	{
		perror("客户端：连接服务器失败\n");
		return -1;
	}

	printf("连接服务器成功\n");

	return skt_fd;
}

int disconnect_dest_server(int srv_fd)
{
	return close(srv_fd);
}
