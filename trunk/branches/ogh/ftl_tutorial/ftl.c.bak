// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.


#include "jasmine.h"

static void sanity_check(void);
static BOOL32 is_bad_block(UINT32 const bank, UINT32 const vblk_offset);
//Removed by RED
/*static UINT32 get_physical_address(UINT32 const lpage_addr);
static void update_physical_address(UINT32 const lpage_addr, UINT32 const new_bank, UINT32 const new_row);*/
static UINT32 get_free_page(UINT32 const bank);
static BOOL32 check_format_mark(void);
static void write_format_mark(void);
static void format(void);
	//Added by GYUHWA

UINT32 g_ftl_read_buf_id;
UINT32 g_ftl_write_buf_id;


static volatile UINT32 g_read_fail_count;
static volatile UINT32 g_program_fail_count;
static volatile UINT32 g_erase_fail_count;

//by ogh

typedef struct _misc_metadata											//modified by GYUHWA
{
	UINT32 cur_miscblk_vpn;
	UINT32 g_scan_list_entries;
	UINT32 g_target_row;
}misc_metadata; // per bank
#define MISC_BLK_NUM 1
#define MISCBLK_VBN	1
#define NUM_MISC_META_SECT  ((sizeof(misc_metadata) + BYTES_PER_SECTOR - 1)/ BYTES_PER_SECTOR)
//----------------------------------
// FTL metadata (maintain in SRAM)
//----------------------------------
static misc_metadata  g_misc_meta[NUM_BANKS];

//----------------------------------

//*Red//
static void get_psn(UINT32 const lba, UINT32 const totals);
static void set_psn(UINT32 const lba, UINT32 const totals);

//no striping version.
UINT32 g_target_bank;
UINT32 g_target_sect;
UINT32 g_merge_buffer_lsn[SECTORS_PER_PAGE];

//mem_copy version.
UINT32 g_psn_read[SECTORS_PER_PAGE + 1];
UINT32 g_psn_write[SECTORS_PER_PAGE + 1];
UINT32 g_psn_sect;
UINT32 g_psn_write_sect;
UINT32 g_psn_write_temp;
UINT32 g_psn_start;
volatile UINT32 smt_temp = SMT_ADDR;
void ftl_open(void)
{
	sanity_check();

	// STEP 1 - read scan lists from NAND flash

	scan_list_t* scan_list = (scan_list_t*) SCAN_LIST_ADDR;
	UINT32 bank;

	// Since we are going to check the flash interrupt flags within this function, ftl_isr() should not be called.
	disable_irq();

	flash_clear_irq();	// clear any flash interrupt flags that might have been set
	
	//ogh
	g_psn_sect = 0;
	g_target_bank = 0;
	g_target_sect = 0;
	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		//g_misc_meta[bank].g_merge_buff_sect = 0;
		SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);			// FC_COL_ROW_READ_OUT = sensing and data output
		SETREG(FCP_OPTION, FO_E);						// scan list was written in 1-plane mode by install.exe, so there is no FO_P
		SETREG(FCP_DMA_ADDR, scan_list + bank);			// target address should be DRAM or SRAM (see flash.h for rules)
		SETREG(FCP_DMA_CNT, SCAN_LIST_SIZE);			// number of bytes for data output
		SETREG(FCP_COL, 0);
		SETREG(FCP_ROW_L(bank), SCAN_LIST_PAGE_OFFSET);	// scan list was written to this position by install.exe
		SETREG(FCP_ROW_H(bank), SCAN_LIST_PAGE_OFFSET);	// Tutorial FTL always uses the same row addresses for high chip and low chip

		flash_issue_cmd(bank, RETURN_ON_ISSUE);			// Take a look at the source code of flash_issue_cmd() now.
	}

	// This while() statement waits the last issued command to be accepted.
	// If bit #0 of WR_STAT is one, a flash command is in the Waiting Room, because the target bank has not accepted it yet.
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// Now, FC_COL_ROW_READ_OUT commands are accepted by all the banks.
	// Before checking whether scan lists are corrupted or not, we have to wait the completion of read operations.
	// This code shows how to wait for ALL the banks to become idle.
	while (GETREG(MON_CHABANKIDLE) != 0);

	// Now we can check the flash interrupt flags.

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		UINT32 num_entries = NULL;
		UINT32 result = OK;

		if (BSP_INTR(bank) & FIRQ_DATA_CORRUPT)
		{
			// Too many bits are corrupted so that they cannot be corrected by ECC.
			result = FAIL;
		}
		else
		{
			// Even though the scan list is not corrupt, we have to check whether its contents make sense.

			UINT32 i;

			num_entries = read_dram_16(&(scan_list[bank].num_entries));

			if (num_entries > SCAN_LIST_ITEMS)
			{
				result = FAIL;	// We cannot trust this scan list. Perhaps a software bug.
			}
			else
			{
				for (i = 0; i < num_entries; i++)
				{
					UINT16 entry = read_dram_16(&(scan_list[bank].list[i]));
					UINT16 pblk_offset = entry & 0x7FFF;

					if (pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK)
					{
						#if OPTION_REDUCED_CAPACITY == FALSE
						result = FAIL;	// We cannot trust this scan list. Perhaps a software bug.
						#endif
					}
					else
					{
						// Bit position 15 of scan list entry is high-chip/low-chip flag.
						// Remove the flag in order to make is_bad_block() simple.

						write_dram_16(&(scan_list[bank].list[i]), pblk_offset);
					}
				}
			}
		}

		if (result == FAIL)
		{
			mem_set_dram(scan_list + bank, 0, SCAN_LIST_SIZE);
			g_misc_meta[bank].g_scan_list_entries = 0;
		}
		else
		{
			write_dram_16(&(scan_list[bank].num_entries), 0);
			g_misc_meta[bank].g_scan_list_entries = num_entries;
		}
	}

	// STEP 2 - If necessary, do low-level format
	// format() should be called after loading scan lists, because format() calls is_bad_block().

//	if (check_format_mark() == FALSE)
	if( TRUE)
	{
		// When ftl_open() is called for the first time (i.e. the SSD is powered up the first time)
		// format() is called.

		format();
	}


	//*Red//
	// STEP 3 - initialize sector mapping table pieces
	// The page mapping table is too large to fit in SRAM and DRAM.
	// gyuhwa
//	init_metadata();
	// STEP 4 - initialize global variables that belong to FTL

	g_ftl_read_buf_id = 0;
	g_ftl_write_buf_id = 0;

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		g_misc_meta[bank].g_target_row = PAGES_PER_VBLK;
	}

	flash_clear_irq();

	// This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts

	SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	ftl_flush();													//Added by GYUHWA

	enable_irq();
}

void ftl_read(UINT32 const lba, UINT32 const total_sectors)				//modified by GYUHWA
{
	UINT32 sect_offset, count,  next_read_buf_id;
	
	sect_offset = lba % SECTORS_PER_PAGE;					//sect_offset : offset of RD_BUFFER
	get_psn(lba,SECTORS_PER_PAGE - sect_offset);
	g_psn_sect = 0;
	for(count = 0; count < total_sectors; count++)
	{
		ftl_read_sector(lba+count, sect_offset++);			//read one sector
		g_psn_sect++;
		if(sect_offset == SECTORS_PER_PAGE )				//One page is read complete
		{
			next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

			#if OPTION_FTL_TEST == 0
			while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
			#endif

			g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
			while (GETREG(MON_CHABANKIDLE) != 0);	// This while() loop ensures that Waiting Room is empty and all the banks are idle.
				
		
			SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
			SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit
			sect_offset = 0;
			get_psn(lba+count+1, SECTORS_PER_PAGE);
			g_psn_sect = 0;
		}
	}
	if(sect_offset != 0)								//no complete page -> make read command, example : only 4 sector read command
	{
		next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

		#if OPTION_FTL_TEST == 0
		while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
		#endif
		g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
			while (GETREG(MON_CHABANKIDLE) != 0);	// This while() loop ensures that Waiting Room is empty and all the banks are idle.
		
SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
			SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit			// change bm_read_limit
	}
}
void ftl_read_sector(UINT32 const lba, UINT32 const sect_offset)							//added by GYUHWA
{
	UINT32 psn, bank, row, buf_offset, nand_offset;
	UINT32 t1;
	psn = g_psn_read[g_psn_sect];									//physical sector nomber
	//bank = lba % NUM_BANKS;	
	bank = psn / SECTORS_PER_BANK;
	t1 = psn % SECTORS_PER_BANK;
	row = t1 / SECTORS_PER_PAGE;		
	nand_offset = t1 % SECTORS_PER_PAGE;				//physical nand offset

	if((psn & (UINT32)BIT31) != 0 )					//data is in merge buffer
	{
		buf_offset = (psn ^ (UINT32)BIT31);
		bank = g_target_bank;
		mem_copy(RD_BUF_PTR(g_ftl_read_buf_id) + sect_offset * BYTES_PER_SECTOR, 
				MERGE_BUFFER_ADDR + bank * BYTES_PER_PAGE + BYTES_PER_SECTOR * buf_offset, 
				BYTES_PER_SECTOR);					
		//find collect data -> mem_copy to RD_BUFFER
	}
	else if (psn != NULL)							//data is in nand flash
		{
		SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);		//FCP command for read one sector
		SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
		SETREG(FCP_COL, nand_offset);						
		SETREG(FCP_DMA_ADDR, RD_BUF_PTR(g_ftl_read_buf_id) + (BYTES_PER_SECTOR * (sect_offset - nand_offset)));
		// nand_offset is COL, and RD_BUFFER_offset is COL. So we change DMA_ADDR to read data different sector offset.
		SETREG(FCP_OPTION, FO_P | FO_E );		
		SETREG(FCP_ROW_L(bank), row);				
		SETREG(FCP_ROW_H(bank), row);// change bm_read_limit

		flash_issue_cmd(bank, RETURN_ON_ISSUE);
		//Because we don't increase BM_STACK_RD_LIMIT to collect sectors 
	}
	else								//data that never written
	{
		mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) + row * BYTES_PER_SECTOR, 0xFFFFFFFF, BYTES_PER_SECTOR); //add 0xfffff to data that never written
	}
}

// modified by ogh

void ftl_write(UINT32 const lba, UINT32 const total_sectors)
{
	UINT32 i, num_sectors_to_write;
	UINT32 remain_sectors = total_sectors;
	UINT32 next_lba = lba;
	UINT32 sect_offset = lba % SECTORS_PER_PAGE;
	UINT32 pre,cnt;
	g_psn_sect = 0;
	g_psn_write_sect = 0;
	g_psn_start = g_target_sect;
	/* until write operations end */
	while(remain_sectors != 0)
	{
#if OPTION_FTL_TEST == 0
		// wait data which is from sata host
		while(g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR) );
#endif
		/* requested sector sizes are more than sector sizes per one virtual page */
		if( sect_offset + remain_sectors >= SECTORS_PER_PAGE)
		{
			num_sectors_to_write = SECTORS_PER_PAGE - sect_offset;
		}
		else
		{
			num_sectors_to_write = remain_sectors;
		}
		get_psn(lba,num_sectors_to_write);
		for(i = 0 ;i < num_sectors_to_write;i++)
		{
			/* call sector level write function */
			ftl_write_sector( next_lba + i);
			g_psn_sect++;
		}
		remain_sectors -= num_sectors_to_write;
		if( remain_sectors == 0) {
			g_ftl_write_buf_id = (g_ftl_write_buf_id + 1 ) % NUM_WR_BUFFERS;
			SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
			SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit
			break;
		}
		sect_offset = 0;
		g_psn_sect = 0;
		next_lba += num_sectors_to_write;
		/* incread bm_write_limit and g_ftl_write_buf_id for next sata host's data sending */
		g_ftl_write_buf_id = (g_ftl_write_buf_id + 1 ) % NUM_WR_BUFFERS;
		SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
		SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit
	}
	pre = g_merge_buffer_lsn[g_psn_start];
	cnt = 1;
	g_psn_write_temp = 0;
	for(i = 1 ;i < g_psn_write_sect; i++ )
	{
		if( g_merge_buffer_lsn[g_psn_start + i] != g_merge_buffer_lsn[g_psn_start + i -1] + 1 ){
			// set psn
			set_psn( pre, cnt );
			g_psn_write_temp += cnt;
			cnt = 1;
			pre = g_merge_buffer_lsn[g_psn_start + i ];
		}
		else
		{
			//increase cnt
			cnt++;
		}
	}
	if( g_psn_write_sect != 0 ) 
	{
		set_psn(pre , cnt);
	}
}
void ftl_write_sector(UINT32 const lba)
{
	UINT32 new_bank,vsect_num, new_row;
	UINT32 new_psn;
	UINT32 index = lba % SECTORS_PER_PAGE;
	UINT32 temp;
	UINT32 pre, cnt;
	int i;
	//new_bank = lba % NUM_BANKS; // get bank number of sector
	new_bank = g_target_bank;
	
	temp = g_psn_read[g_psn_sect];

	if( (temp & (UINT32)BIT31) != 0 ){
		// If data, which located in same lba, is already in dram
		// copy sata host data to same merge buffer sector
		vsect_num = (temp ^ (UINT32)BIT31); 
		mem_copy((UINT32)MERGE_BUFFER_ADDR + new_bank * (UINT32)BYTES_PER_PAGE + 
				vsect_num * (UINT32)BYTES_PER_SECTOR,
				WR_BUF_PTR(g_ftl_write_buf_id) + index * BYTES_PER_SECTOR,
				BYTES_PER_SECTOR);
		g_psn_write[g_psn_write_sect++] = temp;
	}
	else{
		// copy sata host data to dram memory merge buffer page 
		//g_target_sect = g_misc_meta[new_bank].g_merge_buff_sect;

		mem_copy(MERGE_BUFFER_ADDR + new_bank * BYTES_PER_PAGE + g_target_sect * BYTES_PER_SECTOR,
				WR_BUF_PTR(g_ftl_write_buf_id) + index * BYTES_PER_SECTOR,
				BYTES_PER_SECTOR);
		// set psn to -1 , it means that data is in dram 
		g_psn_write[g_psn_write_sect++] = ((UINT32)BIT31 ^ g_target_sect );
		//set_psn(lba, ((UINT32)BIT31 ^ g_target_sect ));

		// for change psn 
		g_merge_buffer_lsn[g_target_sect] = lba;

		g_target_sect++;

		// If merge_buffer of bank is full ,
		// than flush the merge buffer page to nand flash
		// and set a psn number of all sectors.
		if( g_target_sect >= SECTORS_PER_PAGE ){
			/* get free page */
			new_row = get_free_page(new_bank);
			SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
			SETREG(FCP_OPTION, FO_P | FO_E | FO_B_W_DRDY);
			SETREG(FCP_DMA_ADDR, MERGE_BUFFER_ADDR + new_bank * BYTES_PER_PAGE);
			SETREG(FCP_DMA_CNT, BYTES_PER_PAGE);
			SETREG(FCP_COL,0);
			SETREG(FCP_ROW_L(new_bank),new_row);
			SETREG(FCP_ROW_H(new_bank),new_row);

			flash_issue_cmd(new_bank,RETURN_ON_ISSUE);

			/* initialize merge buffer page's sector point */
		//	g_misc_meta[new_bank].g_merge_buff_sect = 0;
			g_target_sect = 0;
			g_psn_write_sect = 0;
			g_psn_start = 0;
			// allocate new psn 
			//new_psn = new_row * SECTORS_PER_PAGE;

			new_psn = new_bank * SECTORS_PER_BANK + new_row * SECTORS_PER_PAGE;
			// vsn - > psn mapping  
			
			for(i = 0;i < SECTORS_PER_PAGE;i++){
				g_psn_write[i] = new_psn + i;
			}
			pre = g_merge_buffer_lsn[0];
			cnt = 1;
			g_psn_write_temp = 0;
			for(i = 1 ;i < SECTORS_PER_PAGE; i++ )
			{
				if( g_merge_buffer_lsn[i] != g_merge_buffer_lsn[i-1] + 1 ){
					// set psn
					set_psn( pre, cnt );
					g_psn_write_temp += cnt;
					cnt = 1;
					pre = g_merge_buffer_lsn[i];
				}
				else
				{
					//increase cnt
					cnt++;
				}
			}
			set_psn(pre , cnt);
			g_target_bank = (g_target_bank + 1 ) % NUM_BANKS;
		}
	}
}

void flush_merge_buffer()
{
	UINT32 new_row, new_psn;
	UINT32 new_bank = g_target_bank;

	int i;
	if( g_target_sect != 0 ){
		// get free page from target bank
		new_row = get_free_page(new_bank);

		// set registers to write a data to nand flash memory
		SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
		SETREG(FCP_OPTION, FO_P | FO_E | FO_B_W_DRDY);
		// Address is merge buffer address which contains actual data
		SETREG(FCP_DMA_ADDR, MERGE_BUFFER_ADDR + new_bank * BYTES_PER_PAGE);
		SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR * g_target_sect);
		SETREG(FCP_COL,0);
		SETREG(FCP_ROW_L(new_bank),new_row);
		SETREG(FCP_ROW_H(new_bank),new_row);

		flash_issue_cmd(new_bank,RETURN_ON_ISSUE);
		
		// for lba -> psn mapping information 
		new_psn = new_bank * SECTORS_PER_BANK + new_row * SECTORS_PER_PAGE;
		// Update mapping information
		for(i = 0 ;i < g_target_sect; i++ )
		{
			set_psn( g_merge_buffer_lsn[i],
					new_psn + i );
		}
	}
}
void ftl_flush(void)
{
}

static BOOL32 is_bad_block(UINT32 const bank, UINT32 const vblk_offset)
{
	// The scan list, which is installed by installer.c:install_block_zero(), contains physical block offsets of initial bad blocks.
	// Since the parameter to is_bad_block() is not a pblk_offset but a vblk_offset, we have to do some conversion.
	//
	// When 1-plane mode is used, vblk_offset is equivalent to pblk_offset.
	// When 2-plane mode is used, vblk_offset = pblk_offset / 2.
	// Two physical blocks 0 and 1 are grouped into virtual block 0.
	// Two physical blocks 2 and 3 are grouped into virtual block 1.
	// Two physical blocks 4 and 5 are grouped into virtual block 2.

#if OPTION_2_PLANE

	UINT32 pblk_offset;
	scan_list_t* scan_list = (scan_list_t*) SCAN_LIST_ADDR;

	pblk_offset = vblk_offset * NUM_PLANES;

	if (mem_search_equ_dram(scan_list + bank, sizeof(UINT16), g_misc_meta[bank].g_scan_list_entries, pblk_offset) < g_misc_meta[bank].g_scan_list_entries)
	{
		return TRUE;
	}

	pblk_offset = vblk_offset * NUM_PLANES + 1;

	if (mem_search_equ_dram(scan_list + bank, sizeof(UINT16), g_misc_meta[bank].g_scan_list_entries, pblk_offset) < g_misc_meta[bank].g_scan_list_entries)
	{
		return TRUE;
	}

	return FALSE;

#else

	scan_list_t* scan_list = (scan_list_t*) SCAN_LIST_ADDR;

	if (mem_search_equ_dram(scan_list + bank, sizeof(UINT16), g_misc_meta[bank].g_scan_list_entries, vblk_offset) < g_misc_meta[bank].g_scan_list_entries)
	{
		return TRUE;
	}

	return FALSE;

#endif
}
//modified by ogh
//
/*
   static UINT32 get_physical_address(UINT32 const lpage_addr)
   {
// Page mapping table entry size is 4 byte.
return read_dram_32(PAGE_MAP_ADDR + lpage_addr * sizeof(UINT32));
}

static void update_physical_address(UINT32 const lpage_addr, UINT32 const new_bank, UINT32 const new_row)
{
write_dram_32(PAGE_MAP_ADDR + lpage_addr * sizeof(UINT32), new_bank * PAGES_PER_BANK + new_row);
}
*/
static void get_psn(UINT32 const lba, UINT32 const totals)		//added by RED
{   
	UINT32 src = SMT_ADDR + (lba * sizeof(UINT32));
	UINT32 dst = (UINT32)g_psn_read;
	UINT32 size = sizeof(UINT32) * totals;
	//int i = 0;
	//for(i = 0 ;i < totals;i++){
		//g_psn_read[i] = read_dram_32(src + (sizeof(UINT32)*i))
	//}
	mem_copy(dst,src,size);
}
static void set_psn(UINT32 const lba, UINT32 const totals)			//added by RED
{
	UINT32 src = (UINT32)g_psn_write + (sizeof(UINT32) * g_psn_write_temp);
	UINT32 dst = SMT_ADDR + (lba * sizeof(UINT32));
	UINT32 size = sizeof(UINT32) * totals;
	//int i;
	mem_copy(dst,src,size);
	/*for(i = 0 ;i < totals;i++){
		write_dram_32(dst + (sizeof(UINT32)* i),g_psn_write[g_psn_write_temp+i]);
	}*/
}
static UINT32 get_free_page(UINT32 const bank)
{
	// This function returns the row address for write operation.

	UINT32 row;
	UINT32 vblk_offset, page_offset;

	row = g_misc_meta[bank].g_target_row;
	vblk_offset = row / PAGES_PER_VBLK;
	page_offset = row % PAGES_PER_VBLK;

	if (page_offset == 0)	// We are going to write to a new vblock.
	{
		while (is_bad_block(bank, vblk_offset) && vblk_offset < VBLKS_PER_BANK)
		{
			vblk_offset++;	// We have to skip bad vblocks.
		}
	}

	if (vblk_offset >= VBLKS_PER_BANK)
	{
		// Free vblocks are exhausted. Since this example FTL does not do garbage collection,
		// no more data can be written to this SSD. The SSD stops working now.

		led (1);
		while (1);
	}

	row = vblk_offset * PAGES_PER_VBLK + page_offset;

	g_misc_meta[bank].g_target_row = row + 1;

	return row;
}

static BOOL32 check_format_mark(void)
{
	// This function reads a flash page from (bank #0, block #0) in order to check whether the SSD is formatted or not.

#ifdef __GNUC__
	extern UINT32 size_of_firmware_image;
	UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
#else
	extern UINT32 Image$$ER_CODE$$RO$$Length;
	extern UINT32 Image$$ER_RW$$RW$$Length;
	UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) + ((UINT32) &Image$$ER_RW$$RW$$Length);
	UINT32 firmware_image_pages = (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
#endif

	UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;
	UINT32 temp;

	flash_clear_irq();	// clear any flash interrupt flags that might have been set

	SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
	SETREG(FCP_BANK, REAL_BANK(0));
	SETREG(FCP_OPTION, FO_E);
	SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR); 	// flash -> DRAM
	SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), format_mark_page_offset);
	SETREG(FCP_ROW_H(0), format_mark_page_offset);

	// At this point, we do not have to check Waiting Room status before issuing a command,
	// because scan list loading has been completed just before this function is called.
	SETREG(FCP_ISSUE, NULL);

	// wait for the FC_COL_ROW_READ_OUT command to be accepted by bank #0
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// wait until bank #0 finishes the read operation
	while (BSP_FSM(0) != BANK_IDLE);

	// Now that the read operation is complete, we can check interrupt flags.
	temp = BSP_INTR(0) & FIRQ_ALL_FF;

	// clear interrupt flags
	CLR_BSP_INTR(0, 0xFF);

	if (temp != 0)
	{
		return FALSE;	// the page contains all-0xFF (the format mark does not exist.)
	}
	else
	{
		return TRUE;	// the page contains something other than 0xFF (it must be the format mark)
	}
}

static void write_format_mark(void)
{
	// This function writes a format mark to a page at (bank #0, block #0).

#ifdef __GNUC__
	extern UINT32 size_of_firmware_image;
	UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
#else
	extern UINT32 Image$$ER_CODE$$RO$$Length;
	extern UINT32 Image$$ER_RW$$RW$$Length;
	UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) + ((UINT32) &Image$$ER_RW$$RW$$Length);
	UINT32 firmware_image_pages = (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
#endif

	UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;

	mem_set_dram(FTL_BUF_ADDR, 0, BYTES_PER_SECTOR);

	SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
	SETREG(FCP_BANK, REAL_BANK(0));
	SETREG(FCP_OPTION, FO_E | FO_B_W_DRDY);
	SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR); 	// DRAM -> flash
	SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), format_mark_page_offset);
	SETREG(FCP_ROW_H(0), format_mark_page_offset);

	// At this point, we do not have to check Waiting Room status before issuing a command,
	// because we have waited for all the banks to become idle before returning from format().
	SETREG(FCP_ISSUE, NULL);

	// wait for the FC_COL_ROW_IN_PROG command to be accepted by bank #0
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// wait until bank #0 finishes the write operation
	while (BSP_FSM(0) != BANK_IDLE);
}

static void format(void)
{
	// This function is called upon the very first power-up of the SSD.
	// This function does the low-level format (i.e. FTL level format) of SSD.
	// A typical FTL would create its mapping table and the list of free blocks.
	// However, this example does nothing more than erasing all the free blocks.
	//
	// This function may take a long time to complete. For example, erasing all the flash blocks can
	// take more than ten seconds depending on the total density.
	// In that case, the host will declare time-out error. (no response from SSD for a long time)
	// A suggested solution to this problem is:
	// When you power-up the SSD for the first time, connect the power cable but not the SATA cable.
	// At the end of this function, you can put a call to led(1) to indicate that the low level format
	// has been completed. When the LED is on, turn off the power, connect the SATA cable, and turn on
	// the power again.

	UINT32 vblk_offset, bank;

	for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
	{
		for (bank = 0; bank < NUM_BANKS; bank++)
		{
			if (is_bad_block(bank, vblk_offset))
				continue;

			// You do not need to set the values of FCP_DMA_ADDR, FCP_DMA_CNT and FCP_COL for FC_ERASE.

			SETREG(FCP_CMD, FC_ERASE);
			SETREG(FCP_BANK, REAL_BANK(bank));
			SETREG(FCP_OPTION, FO_P);
			SETREG(FCP_ROW_L(bank), vblk_offset * PAGES_PER_VBLK);
			SETREG(FCP_ROW_H(bank), vblk_offset * PAGES_PER_VBLK);

			// You should not issue a new command when Waiting Room is not empty.

			while ((GETREG(WR_STAT) & 0x00000001) != 0);

			// By writing any value to FCP_ISSUE, you put FC_ERASE into Waiting Room.
			// The value written to FCP_ISSUE does not have any meaning.

			SETREG(FCP_ISSUE, NULL);
		}
	}

	// In general, write_format_mark() should be called upon completion of low level format in order to prevent
	// format() from being called again.
	// However, since the tutorial FTL does not support power off recovery,
	// format() should be called every time.
	//init_metadata();								//Added by GYUHWA
	mem_set_dram(SMT_ADDR, NULL, SMT_BYTES);
	ftl_flush();									//Added by GYUHWA
#if 0
	write_format_mark();
#endif

	led(1);
}
void ftl_isr(void)
{
	// interrupt service routine for flash interrupts

	UINT32 bank;
	UINT32 bsp_intr_flag;

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		while (BSP_FSM(bank) != BANK_IDLE);

		bsp_intr_flag = BSP_INTR(bank);

		if (bsp_intr_flag == 0)
		{
			continue;
		}

		UINT32 fc = GETREG(BSP_CMD(bank));

		CLR_BSP_INTR(bank, bsp_intr_flag);

		if (bsp_intr_flag & FIRQ_DATA_CORRUPT)
		{
			g_read_fail_count++;
		}

		if (bsp_intr_flag & (FIRQ_BADBLK_H | FIRQ_BADBLK_L))
		{
			if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG)
			{
				g_program_fail_count++;
			}
			else
			{
				ASSERT(fc == FC_ERASE);
				g_erase_fail_count++;
			}
		}
	}

	// clear the flash interrupt flag at the interrupt controller
	SETREG(APB_INT_STS, INTR_FLASH);
}

static void sanity_check(void)
{
	UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + COPY_BUF_BYTES + FTL_BUF_BYTES
		+ HIL_BUF_BYTES + TEMP_BUF_BYTES + SCAN_LIST_BYTES + MERGE_BUFFER_BYTES + SMT_BYTES;

	if (dram_requirement > DRAM_SIZE)
	{
		while (1);
	}
}
