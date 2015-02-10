#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "allocator.h"
#include <numaif.h>
#include <errno.h>
#include <pthread.h>
#include "bh-manager.h"

#define AUDIT if(1)

mem_map * bhmaps;
char fictitious[MAX_MSG_SIZE];

pthread_spinlock_t bh_write[MAX_SOBJS];
pthread_spinlock_t bh_read[MAX_SOBJS];

extern int handled_sobjs;

void set_BH_map(mem_map* argA ){
        bhmaps = argA;
}

int init_BH(){
        
        int i;
        char* addr;
        int ret;
        int sobjs;

        if(handled_sobjs < 0) return INVALID_SOBJS_COUNT; 

        sobjs = handled_sobjs;


        for (i=0; i<sobjs; i++){
                bhmaps[i].live_msgs = 0;
                bhmaps[i].live_boundary = 0;
                bhmaps[i].expired_msgs = 0;
                bhmaps[i].expired_offset = 0;
                bhmaps[i].expired_boundary = 0;
        }
       

        for (i=0; i<sobjs; i++){
                addr = allocate_page();
                if (addr == NULL) goto bad_init;
                bhmaps[i].live_bh = addr;
		bhmaps[i].actual_bh_addresses[0] = addr;
                addr = allocate_page();
                if (addr == NULL) goto bad_init;
                bhmaps[i].expired_bh = addr;
		bhmaps[i].actual_bh_addresses[1] = addr;
        }

        for (i=0; i<sobjs; i++){
		pthread_spin_init(&bh_write[i],0);
		pthread_spin_init(&bh_read[i],0);
	}
	
        
        return SUCCESS;

bad_init:
        return INIT_ERROR; 


}


int insert_BH(int sobj, void* msg, int size){// this needs to be atomic per sobj - synch is left to the upper layer

	int tag;
	int needed_store;
	int residual_store;
	int offset;


	if( (sobj<0) || sobj >= handled_sobjs) goto bad_insert;

	if( (size<=0) || size > MAX_MSG_SIZE) goto bad_insert;

	if( msg == NULL ) goto bad_insert;


	pthread_spin_lock(&bh_write[sobj]);

	if(bhmaps[sobj].live_boundary >= BH_SIZE) {
		pthread_spin_unlock(&bh_write[sobj]);
		goto bad_insert;
	}

	tag = size;
	needed_store = tag + sizeof(tag);

	residual_store = BH_SIZE - bhmaps[sobj].live_boundary;
	
	if( residual_store < needed_store ){ 
		pthread_spin_unlock(&bh_write[sobj]);
		goto bad_insert;
	}

	offset = bhmaps[sobj].live_boundary;

	memcpy(bhmaps[sobj].live_bh + offset, &tag, sizeof(tag));

	offset += sizeof(tag);

	memcpy(bhmaps[sobj].live_bh + offset, msg, size);

	bhmaps[sobj].live_boundary += needed_store;

	bhmaps[sobj].live_msgs += 1;

	pthread_spin_unlock(&bh_write[sobj]);
	
	return SUCCESS;

bad_insert: 

	AUDIT
	printf("BH insert failure - sobj %d\n",sobj);

	return FAILURE;

}

void* get_BH(int sobj){// this needs to be atomic per sobj - synch is left to the upper layer

	int msg_tag;
	void* buff;
	void* msg_addr;
	int msg_offset;
	

	if( (sobj<0) || sobj >= handled_sobjs) goto no_msg;
	
	pthread_spin_lock(&bh_read[sobj]);

	if(bhmaps[sobj].expired_msgs <= 0 ) {
	
		pthread_spin_lock(&bh_write[sobj]);
		switch_bh(sobj);
		pthread_spin_unlock(&bh_write[sobj]);

	}

	
	if(bhmaps[sobj].expired_msgs <= 0 ){
		pthread_spin_unlock(&bh_read[sobj]);
		goto no_msg;
	}

	msg_offset = bhmaps[sobj].expired_offset;  

	msg_addr = bhmaps[sobj].expired_bh + msg_offset;

	memcpy(&msg_tag,msg_addr,sizeof(msg_tag)); 

	buff = get_buffer(msg_tag);

	msg_addr += sizeof(msg_tag);

	memcpy(buff,msg_addr,msg_tag);

	msg_addr += msg_tag;

	bhmaps[sobj].expired_offset = (char*)msg_addr - bhmaps[sobj].expired_bh;

	bhmaps[sobj].expired_msgs -= 1;

	pthread_spin_unlock(&bh_read[sobj]);

	return buff;

no_msg:
	
	return NULL;	
}

void switch_bh(int sobj){

	char* addr;

	//atomic needed
	bhmaps[sobj].expired_msgs = bhmaps[sobj].live_msgs;
	bhmaps[sobj].expired_offset = bhmaps[sobj].live_offset;
	bhmaps[sobj].expired_boundary = bhmaps[sobj].live_boundary;

	bhmaps[sobj].live_msgs = 0;
	bhmaps[sobj].live_offset = 0;
	bhmaps[sobj].live_boundary = 0;

	addr = bhmaps[sobj].expired_bh; 
	bhmaps[sobj].expired_bh = bhmaps[sobj].live_bh;
	bhmaps[sobj].live_bh = addr;

	return;

}

void* get_buffer(int size){
	
	return fictitious;
}