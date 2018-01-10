#include "types.h"
#include "defs.h"

// Simple system call
int
my_syscall(char *str)
{
	cprintf("%s\n", str);
	return 0xABCDABCD;
}

int
sys_my_syscall(void)
{
	char *str;
	//decode argument using argstr
	if (argstr(0, &str) <0)//argstr은 str이 커널모드가아니라 유저모드처럼 위험한 곳에 있는지 확인해주는 함수 0보다 작으면 이상이 있는것이기 때문에 return -1을 한다.
		return -1;
	return my_syscall(str);
}

