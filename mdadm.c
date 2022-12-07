#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "mdadm.h"
#include "jbod.h"

int is_mounted = 0;
int is_written = 0;

//helper function used to make a op block, will shift bits to the left to activate one of the jbod cmds
uint32_t constructOp(jbod_cmd_t cmd, int diskNum, int blockNum){
	uint32_t op = cmd << 12 | diskNum << 8 | blockNum;
	return op;
}

int mdadm_mount(void) {
  uint32_t op = constructOp(JBOD_MOUNT, 0, 0);
	if (jbod_client_operation(op, NULL) == 0){
		is_mounted = 1; //activate disks
		return 1; //success
	}
	else{
		return -1; //fail
	}
}

int mdadm_unmount(void) {
  uint32_t op = constructOp(JBOD_UNMOUNT, 0, 0);
	if (jbod_client_operation(op, NULL) == 0){
		is_mounted = 0; //deactivate disks
		return 1; //success
	}
	else{
		return -1; //fail
	}
}

//helper function that will allow access to a disk or block
 int seek(int diskNum, int blockNum){
	uint32_t seekDisk = constructOp(JBOD_SEEK_TO_DISK, diskNum, 0);
	uint32_t seekBlock = constructOp(JBOD_SEEK_TO_BLOCK, 0, blockNum);

	jbod_client_operation(seekDisk, NULL);
	jbod_client_operation(seekBlock, NULL);
	return 0;
 }

//find the disk number and block number by dividing the address we are at by the disk size and block size
 void giveAddress(uint32_t addr, int *diskNum, int *blockNum, int *offset){
	*diskNum = addr/JBOD_DISK_SIZE;
	*blockNum = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
	*offset = (addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE; //current block position
 }

int mdadm_write_permission(void){
	uint32_t op = constructOp(JBOD_WRITE_PERMISSION, 0, 0);
	if (jbod_client_operation(op, NULL) == 0){
		is_written = 1; //grants write permission
		return 1; //success
	}
	else{
		return -1; //fail
	}
}


int mdadm_revoke_write_permission(void){
	uint32_t op = constructOp(JBOD_REVOKE_WRITE_PERMISSION, 0, 0);
	if (jbod_client_operation(op, NULL) == 0){
		is_written = 0; //takes away write permissions
		return 1; //success
	}
	else{
		return -1; //fail
	}
	return 0;
}


int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)  {
	int diskNum;
	int blockNum;
	uint8_t temp[JBOD_BLOCK_SIZE];
	int offset;
	int tracker = 0; //will keep track of the remaining bytes to read
	int currAddr = start_addr;

	if (is_mounted == 0){ //check if it's unmounted
		return -1;
	}

	if (read_len > 1024 || start_addr + read_len > 256*256*16){ //base cases
		return -1;
	}

	if (read_buf == NULL && read_len > 0){ //base cases
		return -1;
	}

	giveAddress(currAddr, &diskNum, &blockNum, &offset); //finds the disk number and block number we're at
	seek(diskNum, blockNum);
	
	//if cache is enabled, insert into cache
	if (cache_enabled() == true){
		if(cache_lookup(diskNum, blockNum, temp) == -1){
			uint32_t op = constructOp(JBOD_READ_BLOCK,diskNum,blockNum); //constructs operation to read block
			jbod_client_operation(op, temp);
			cache_insert(diskNum, blockNum, temp);
		}
	}

	//if cache is not enabled, do the regular read operation
	else{
		uint32_t op = constructOp(JBOD_READ_BLOCK,diskNum,blockNum); //constructs operation to read block
		jbod_client_operation(op, temp);
	}

	//reads to the first block
	if (read_len < JBOD_BLOCK_SIZE - offset && offset != 0){
		memcpy(&read_buf[tracker], &temp[offset], read_len);

		currAddr += read_len - tracker;
		tracker += read_len - tracker;
	}

	//reads over the disk
	else if(read_len > JBOD_BLOCK_SIZE - offset && offset != 0){
		memcpy(&read_buf[tracker], &temp[offset], JBOD_BLOCK_SIZE - offset);
				
		currAddr += (JBOD_BLOCK_SIZE - offset);
		tracker += (JBOD_BLOCK_SIZE - offset);
	}

	//now read the remaining blocks down below after writing to the first block

	 while(read_len > 0){
		giveAddress(currAddr, &diskNum, &blockNum, &offset); //finds the disk number and block number we're at
		seek(diskNum, blockNum);
		
		if (cache_enabled() == true){
			if(cache_lookup(diskNum, blockNum, temp) == -1){
				uint32_t op = constructOp(JBOD_READ_BLOCK,diskNum,blockNum); //constructs operation to read block
				jbod_client_operation(op, temp);
				cache_insert(diskNum, blockNum, temp);
			}
		}

		else{
			uint32_t op = constructOp(JBOD_READ_BLOCK,diskNum,blockNum); //constructs operation to read block
			jbod_client_operation(op, temp);
		}
		
		//read to the middle blocks
		if (read_len - tracker >= JBOD_BLOCK_SIZE){
			memcpy(&read_buf[tracker], &temp[offset], JBOD_BLOCK_SIZE);

			currAddr += JBOD_BLOCK_SIZE; //address goes to next block
			tracker += JBOD_BLOCK_SIZE;
			}
		
		//read with the remaining data to the blocks
		else{
			memcpy(&read_buf[tracker], &temp[offset], read_len - tracker);

			currAddr += read_len - tracker;
			tracker += read_len - tracker;
		}

		if (currAddr == read_len + start_addr){ //program ends when the the current address reaches the total length
			break;
		}
	}

	return read_len;
}



int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {
	int diskNum;
	int blockNum;
	uint8_t *temp = malloc(JBOD_BLOCK_SIZE);
	int offset;
	int tracker = 0; //will keep track of the remaining bytes to write
	int currAddr = start_addr;

	if (is_written == 0){ //check if there's no write permission
		return -1;
	}

	if (write_len > 1024 || start_addr + write_len > 256*256*16){ //base cases
		return -1;
	}

	if (write_buf == NULL && write_len > 0){ //base cases
		return -1;
	}

	giveAddress(currAddr, &diskNum, &blockNum, &offset); //finds the disk number and block number we're at
	seek(diskNum, blockNum);

	//see if block is in cache, otherwise do a regular read without cache
	if (cache_enabled() == true){
		if(cache_lookup(diskNum, blockNum, temp) == -1){
			uint32_t op = constructOp(JBOD_READ_BLOCK,diskNum,blockNum); //constructs operation to read block
			jbod_client_operation(op, temp);

			cache_insert(diskNum, blockNum, temp);
		}	
	}

	else{
		uint32_t op = constructOp(JBOD_READ_BLOCK,0,0); //constructs operation to read block
		jbod_client_operation(op, temp);
	}

	//writes to the first block
	if (write_len < JBOD_BLOCK_SIZE - offset && offset != 0){
		memcpy(&temp[offset], &write_buf[tracker], write_len);

		if (cache_enabled() == true){
			cache_update(diskNum, blockNum, temp);
		}

		currAddr += write_len - tracker;
		tracker += write_len - tracker;
	}

	//writes over the disks
	else if(write_len > JBOD_BLOCK_SIZE - offset && offset != 0){
		memcpy(&temp[offset], &write_buf[tracker], JBOD_BLOCK_SIZE - offset);

		if (cache_enabled() == true){
			cache_update(diskNum, blockNum, temp);
		}

		currAddr += (JBOD_BLOCK_SIZE - offset);
		tracker += (JBOD_BLOCK_SIZE - offset);
	}

	seek(diskNum, blockNum);
	uint32_t op2 = constructOp(JBOD_WRITE_BLOCK, 0,0); //constructs write operation
	jbod_client_operation(op2, temp); //performs write operation

	//now read the remaining blocks down below after writing to the first block

	 while(write_len > 0){
		giveAddress(currAddr, &diskNum, &blockNum, &offset); //finds the disk number and block number we're at
		seek(diskNum, blockNum);
		
		if (cache_enabled() == true){
			if(cache_lookup(diskNum, blockNum, temp) == -1){
				uint32_t op = constructOp(JBOD_READ_BLOCK,0,0); //constructs operation to read block
				jbod_client_operation(op, temp);

				cache_insert(diskNum, blockNum, temp);
			}
		}

		else{
			uint32_t op = constructOp(JBOD_READ_BLOCK,0,0); //constructs operation to read block
			jbod_client_operation(op, temp);
		}
		
		if (write_len - tracker >= JBOD_BLOCK_SIZE){ //write to the middle blocks
			memcpy(temp, &write_buf[tracker], JBOD_BLOCK_SIZE);

			if (cache_enabled() == true){
				cache_update(diskNum, blockNum, temp);
			}

			currAddr += JBOD_BLOCK_SIZE; //address goes to next block
			tracker += JBOD_BLOCK_SIZE;
			}
		
		else{ //write with the remaining data to the blocks
			memcpy(&temp[offset], &write_buf[tracker], write_len - tracker);
			
			if (cache_enabled() == true){
				cache_update(diskNum, blockNum, temp);
			}
			currAddr += write_len - tracker;
			tracker += write_len - tracker;
		}

		seek(diskNum, blockNum);
		uint32_t op2 = constructOp(JBOD_WRITE_BLOCK, diskNum,blockNum); 
		jbod_client_operation(op2, temp);

		if (currAddr == write_len + start_addr){ //program ends when the the current address reaches the total length
			break;
		}
	}

	return write_len;
}
