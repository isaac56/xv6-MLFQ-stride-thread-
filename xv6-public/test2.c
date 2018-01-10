#include "types.h"
#include "user.h"
#include "stat.h"

struct as{
	int a;
};

int
main(int argc, char *argv[])
{
    	printf(1,"2번 %d queue에 있습니다!!!!!\n",getlev());
    	while(getlev()==0) {}
    
    	printf(1,"2번 %d queue에 있습니다!!!!!\n",getlev());
    	while(getlev()==1) {}
	
	    printf(1,"2번 %d queue에 있습니다!!!!!\n",getlev());
        while(getlev()==2) {}
        printf(1,"2번 %d queue에 있습니다!!!!!\n",getlev());
        exit();
}

/*
int
main(int argc, char *argv[])
{
	yield();
	exit();
}
*/
