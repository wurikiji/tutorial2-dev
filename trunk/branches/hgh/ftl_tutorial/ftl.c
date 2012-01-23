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
static void logging_misc_metadata(void);								//Added by GYUHWA
static void loading_misc_metadata(void);								//Added by GYUHWA
static void init_metadata(void);											//Added by GYUHWA
//added by RED
static void init_smt(void);
static void loadding_smt_buf_pieces(UINT32 buf_index, UINT32 piece_index);
static void logging_smt_buf_piece(UINT32 buf_index);
static void logging_smt_buf_pieces(void);
static UINT32 get_buf_smt_index(UINT32 const piece_index);
static UINT32 get_psn(UINT32 const lba)	;
static void set_psn(UINT32 const lba, UINT32 const psn)	;
static UINT32 get_victim_smt(void);
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
	UINT32 smt_piece_index[NUM_PIECES_PER_BANK];
	UINT32 position_smt[NUM_PIECES_PER_BANK];
}misc_metadata; // per bank
#define MISCBLK_VBN	1
#define NUM_MISC_META_SECT  ((sizeof(misc_metadata) + BYTES_PER_SECTOR - 1)/ BYTES_PER_SECTOR)
#define MAPPAGES_PER_BANK ((SMT_BYTES_PER_BANK + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE) / PAGES_PER_VBLK
#define SMT_START_BLK 2
//----------------------------------
// FTL metadata (maintain in SRAM)
//----------------------------------
static misc_metadata  g_misc_meta[NUM_BANKS];

//----------------------------------
//Dynamic version.
UINT32 g_target_bank;
UINT32 g_target_sect;
UINT32 g_merge_buffer_lsn[SECTORS_PER_PAGE];

UINT32 smt_buf_pindex[NUM_BUF_PIECES];
UINT32 smt_buf_dirty[NUM_BUF_PIECES];
UINT32 smt_buf_prio[NUM_BUF_PIECES];
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

	if (check_format_mark() == FALSE)
//	if( TRUE)
	{
		// When ftl_open() is called for the first time (i.e. the SSD is powered up the first time)
		// format() is called.

		format();
	}


	//*Red//
	// STEP 3 - initialize sector mapping table pieces
	// The page mapping table is too large to fit in SRAM and DRAM.
	// gyuhwa
	//init_metadata();
    
	// STEP 4 - initialize global variables that belong to FTL

	g_ftl_read_buf_id = 0;
	g_ftl_write_buf_id = 0;

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		g_misc_meta[bank].g_target_row = (g_misc_meta[bank].position_smt[NUM_PIECES_PER_BANK - 1] + 1) * PAGES_PER_VBLK;
	}

	flash_clear_irq();

	// This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts

	SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	//ftl_flush();													//Added by GYUHWA

	enable_irq();
}

void ftl_read(UINT32 const lba, UINT32 const total_sectors)				//modified by GYUHWA
{
	UINT32 sect_offset, count,  next_read_buf_id;
	sect_offset = lba % SECTORS_PER_PAGE;					//sect_offset : offset of RD_BUFFER
	for(count = 0; count < total_sectors; count++)
	{
		ftl_read_sector(lba+count, sect_offset++);			//read one sector
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
	UINT32 src,dst;
	psn = get_psn(lba);									//physical sector nomber
	//bank = lba % NUM_BANKS;	
	bank = psn / SECTORS_PER_BANK;
	t1 = psn % SECTORS_PER_BANK;
	row = t1 / SECTORS_PER_PAGE;		
	nand_offset = t1 % SECTORS_PER_PAGE;				//physical nand offset

	if((psn & (UINT32)BIT31) != 0 )					//data is in merge buffer
	{
		buf_offset = (psn ^ (UINT32)BIT31);
		bank = g_target_bank;
		dst = RD_BUF_PTR(g_ftl_read_buf_id) + sect_offset * BYTES_PER_SECTOR;
		src = MERGE_BUFFER_ADDR + bank * BYTES_PER_PAGE + BYTES_PER_SECTOR * buf_offset;

		mem_copy(dst, src, BYTES_PER_SECTOR);					
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
		for(i = 0 ;i < num_sectors_to_write;i++)
		{
			/* call sector level write function */
			ftl_write_sector( next_lba + i);
		}
		sect_offset = 0;
		remain_sectors -= num_sectors_to_write;
		next_lba += num_sectors_to_write;
		/* incread bm_write_limit and g_ftl_write_buf_id for next sata host's data sending */
		g_ftl_write_buf_id = (g_ftl_write_buf_id + 1 ) % NUM_WR_BUFFERS;
		SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
		SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit
	}
}
void ftl_write_sector(UINT32 const lba)
{
	UINT32 new_bank, vsect_num, new_row;
	UINT32 new_psn;
	UINT32 temp;
	UINT32 dst,src;
	UINT32 index = lba % SECTORS_PER_PAGE;
	int i;
	//new_bank = lba % NUM_BANKS; // get bank number of sector
	new_bank = g_target_bank;
	
	temp = get_psn(lba);

	if( (temp & (UINT32)BIT31) != 0 ){
		// If data, which located in same lba, is already in dram
		// copy sata host data to same merge buffer sector
		vsect_num = (temp ^ (UINT32)BIT31); 

		dst = MERGE_BUFFER_ADDR + new_bank * BYTES_PER_PAGE + vsect_num * BYTES_PER_SECTOR;
		src = WR_BUF_PTR(g_ftl_write_buf_id) + index * BYTES_PER_SECTOR;
		mem_copy(dst,src, BYTES_PER_SECTOR);
	}
	else{
		// copy sata host data to dram memory merge buffer page 
		//vsect_num = g_misc_meta[new_bank].g_merge_buff_sect;
		vsect_num = g_target_sect;

		dst = MERGE_BUFFER_ADDR + new_bank * BYTES_PER_PAGE + vsect_num * BYTES_PER_SECTOR;
		src = WR_BUF_PTR(g_ftl_write_buf_id) + index * BYTES_PER_SECTOR;

		// Because Firmware does not know 
		// about status of previous nand flash command, 
		// wait until target bank is IDLE 
		// ( target DRAM space is fully flashed ) 
		while(_BSP_FSM(new_bank) != BANK_IDLE);
		mem_copy(dst, src, BYTES_PER_SECTOR);

		// set psn to -1 , it means that data is in dram 
		set_psn(lba, ((UINT32)BIT31 | vsect_num ));

		// for change psn 
		g_merge_buffer_lsn[vsect_num] = lba;

		vsect_num++;

		// If merge_buffer of bank is full ,
		// than flush the merge buffer page to nand flash
		// and set a psn number of all sectors.
		if( vsect_num >= SECTORS_PER_PAGE ){
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
			g_target_bank = (g_target_bank + 1 ) % NUM_BANKS;
			// allocate new psn 
			//new_psn = new_row * SECTORS_PER_PAGE;

			new_psn = new_bank * SECTORS_PER_BANK + new_row * SECTORS_PER_PAGE;
			// vsn - > psn mapping  
			for(i = 0 ;i < SECTORS_PER_PAGE; i++ )
			{
				set_psn( g_merge_buffer_lsn[i],
						new_psn + i );
			}
		}
		else
		{
			//g_misc_meta[new_bank].g_merge_buff_sect++;
			g_target_sect++;
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
			set_psn( g_merge_buffer_lsn[i],	new_psn + i );
		}
	}
}
void ftl_flush(void)
{
	flush_merge_buffer();
	logging_misc_metadata();
	logging_smt_buf_pieces();
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
static void init_smt(void)
{
	UINT32 index, bank;
	for(bank = 0; bank < NUM_BANKS; bank++)
	{
		for(index = 0; index < NUM_PIECES_PER_BANK; index++)
			g_misc_meta[bank].smt_piece_index[index] = 0;
	}
	for(index = 0; index < NUM_BUF_PIECES; index++)
		smt_buf_pindex[index] = 0;
}
static void loadding_smt_buf_pieces(UINT32 const buf_index, UINT32 const piece_index)
{
	UINT32 bank, row, piece_offset;
	bank = piece_index / NUM_PIECES_PER_BANK;
	piece_offset = piece_index % NUM_PIECES_PER_BANK;
	row = g_misc_meta[bank].position_smt[(piece_index % NUM_PIECES_PER_BANK)]  * PAGES_PER_VBLK + g_misc_meta[bank].smt_piece_index[piece_offset] * (SECTORS_PER_SMT_PIECES / SECTORS_PER_PAGE);
	SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);		//FCP command for read one sector
	SETREG(FCP_DMA_CNT, SECTORS_PER_SMT_PIECES * sizeof(UINT32));
	SETREG(FCP_COL, 0);						
	SETREG(FCP_DMA_ADDR, SMT_PIECES_ADDR + (buf_index * SECTORS_PER_SMT_PIECES * sizeof(UINT32)));
	SETREG(FCP_OPTION, FO_P | FO_E );		
	SETREG(FCP_ROW_L(bank), row);				
	SETREG(FCP_ROW_H(bank), row);// change bm_read_limit
	flash_issue_cmd(bank, RETURN_WHEN_DONE);
	smt_buf_pindex[buf_index] = piece_index;
}
static void logging_smt_buf_piece(UINT32 const buf_index)
{
	UINT32 piece_index, row, bank, piece_offset;
	piece_index = smt_buf_pindex[buf_index];
	bank = piece_index / NUM_PIECES_PER_BANK;
	piece_offset = piece_index % NUM_PIECES_PER_BANK;
	if(g_misc_meta[bank].smt_piece_index[piece_offset] == (PAGES_PER_VBLK / (SECTORS_PER_SMT_PIECES / SECTORS_PER_PAGE)) - 1)
		g_misc_meta[bank].smt_piece_index[piece_offset] = 0;
	else
		g_misc_meta[bank].smt_piece_index[piece_offset]++;
	row = g_misc_meta[bank].position_smt[(piece_index % NUM_PIECES_PER_BANK)] * PAGES_PER_VBLK + g_misc_meta[bank].smt_piece_index[piece_offset] * (SECTORS_PER_SMT_PIECES / SECTORS_PER_PAGE);
	SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
	SETREG(FCP_BANK, bank);
	SETREG(FCP_OPTION, FO_E | FO_B_W_DRDY);
	SETREG(FCP_DMA_ADDR, SMT_PIECES_ADDR + (buf_index * SECTORS_PER_SMT_PIECES * sizeof(UINT32))); 	// DRAM -> flash
	SETREG(FCP_DMA_CNT, SECTORS_PER_SMT_PIECES * sizeof(UINT32));
 	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), row);
	SETREG(FCP_ROW_H(0), row);
	flash_issue_cmd(bank, RETURN_WHEN_DONE);
}
static void logging_smt_buf_pieces(void)
{
	UINT32 buf_index;
	for(buf_index = 0; buf_index < NUM_BUF_PIECES; buf_index++)
		logging_smt_buf_piece(buf_index);
}
static UINT32 get_buf_smt_index(UINT32 const piece_index)
{
    UINT32 p_index;
    for (p_index = 0; p_index < NUM_BUF_PIECES; p_index++) {
        if (smt_buf_pindex[p_index] == piece_index) {
            return p_index;
        }
    }
    return (UINT32)-1;
}
static UINT32 get_psn(UINT32 const lba)		
{   
    UINT32 piece_index, buf_index, sect_offset, psn;
	piece_index = lba / SECTORS_PER_SMT_PIECES;
    // Get SMT piece offset from given lba.
    buf_index = get_buf_smt_index(piece_index);
	sect_offset = lba % SECTORS_PER_SMT_PIECES;
    
    // Check if SMT piece is in 'dirty SMT piece' array.
    // If it is not in array, copy present SMT piece in DRAM to NAND and SMT piece needed in NAND to DRAM.
    if(buf_index == (UINT32)-1)
	{
		buf_index = get_victim_smt();
		if(smt_buf_dirty[buf_index] ==1)
			logging_smt_buf_piece(buf_index);
		loadding_smt_buf_pieces(buf_index, piece_index);
		smt_buf_dirty[buf_index] = 0;
	}
    
    // Get PSN from the SMT piece.
    return read_dram_32(SMT_PIECES_ADDR + buf_index * (sizeof(UINT32) * SECTORS_PER_SMT_PIECES) + sect_offset * sizeof(UINT32));
}
static void set_psn(UINT32 const lba, UINT32 const psn)		
{
    UINT32 piece_index, buf_index, sect_offset;
	piece_index = lba / SECTORS_PER_SMT_PIECES;
    // Get SMT piece offset from given lba.
    buf_index = get_buf_smt_index(piece_index);
	sect_offset = lba % SECTORS_PER_SMT_PIECES;
    
    // Check if SMT piece is in 'dirty SMT piece' array.
    // If it is not in array, copy present SMT piece in DRAM to NAND and SMT piece needed in NAND to DRAM.
    if(buf_index ==(UINT32) -1)
	{
		buf_index = get_victim_smt();
		if(smt_buf_dirty[buf_index] ==1)
			logging_smt_buf_piece(buf_index);
		loadding_smt_buf_pieces( buf_index,  piece_index);
	}
    
    // Update PSN on the SMT piece.
	smt_buf_dirty[buf_index] = 1;
    write_dram_32(SMT_PIECES_ADDR + buf_index * (sizeof(UINT32) * SECTORS_PER_SMT_PIECES) + sect_offset * sizeof(UINT32), psn);
}
static UINT32 get_victim_smt(void)
{
	UINT32 victim, index, result;
	victim = 0xffffffff;
	result = 0;
	for(index = 0 ; index < NUM_BUF_PIECES;index++)
	{
		if(victim > smt_buf_prio[index])
		{
			victim = smt_buf_prio[index];
			result = index;
		}
		smt_buf_prio[index] = 0;
	}
	return result;
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

	UINT32 vblk_offset, bank, count[bank];
	for(bank = 0 ;  bank < NUM_BANKS; bank++)
	{
		count[bank] = 0;
	}
	for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
	{
		for (bank = 0; bank < NUM_BANKS; bank++)
		{
			if (is_bad_block(bank, vblk_offset))
				continue;

			// You do not need to set the values of FCP_DMA_ADDR, FCP_DMA_CNT and FCP_COL for FC_ERASE.
			if(count[bank] < NUM_PIECES_PER_BANK)
			{
				g_misc_meta[bank].position_smt[count[bank]] = vblk_offset;
			}
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
	//init_smt();
	//init_metadata();								//Added by GYUHWA
	//ftl_flush();									//Added by GYUHWA
#if 1
	write_format_mark();
#endif

	led(1);
}
static void init_metadata(void)						//Added by GYUHWA
{
	UINT32 bank, sect_count;
	for(bank = 0; bank < NUM_BANKS; bank++)
	{
		g_misc_meta[bank].cur_miscblk_vpn = MISCBLK_VBN * PAGES_PER_BLK - 1; 
	}
	for(sect_count = 0; sect_count < SECTORS_PER_PAGE; sect_count++)
	{
		g_merge_buffer_lsn[sect_count] = 0;
	}
	mem_set_dram(SMT_PIECES_ADDR, NULL, SMT_PIECES_BYTES);
	mem_set_dram(SCAN_LIST_ADDR, 0, SCAN_LIST_BYTES);
}
static void logging_misc_metadata(void)			//Added by GYUHWA
{
	UINT32 bank;
	flash_finish();
	for(bank = 0; bank < NUM_BANKS; bank++)
	{
		g_misc_meta[bank].cur_miscblk_vpn++;
		if(g_misc_meta[bank].cur_miscblk_vpn / PAGES_PER_BLK != MISCBLK_VBN)
		{
			nand_block_erase(bank, MISCBLK_VBN);
			g_misc_meta[bank].cur_miscblk_vpn = MISCBLK_VBN * PAGES_PER_BLK;
		}
		mem_copy(FTL_BUF_ADDR + ((bank) * BYTES_PER_PAGE), &g_misc_meta[bank], sizeof(misc_metadata));
		nand_page_ptprogram(bank,
				MISCBLK_VBN,
				g_misc_meta[bank].cur_miscblk_vpn % PAGES_PER_BLK,
				0,
				NUM_MISC_META_SECT,
				FTL_BUF_ADDR + ((bank) * BYTES_PER_PAGE));
	}
	flash_finish();
}
static void loading_misc_metadata(void)			//Added by GYUHWA
{
    UINT32 load_flag = 0;
    UINT32 bank, page_num;
    UINT32 load_cnt = 0;

    flash_finish();

	disable_irq();
	flash_clear_irq();	// clear any flash interrupt flags that might have been set

    // scan valid metadata in descending order from last page offset
    for (page_num = PAGES_PER_BLK - 1; page_num != ((UINT32) -1); page_num--)
    {
        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (load_flag & (0x1 << bank))
            {
                continue;
            }
            // read valid metadata from misc. metadata area
            nand_page_ptread(bank,
                             MISCBLK_VBN,
                             page_num,
                             0,
                             NUM_MISC_META_SECT,
							 FTL_BUF_ADDR + ((bank) * BYTES_PER_PAGE),
                             RETURN_ON_ISSUE);
        }
        flash_finish();

        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (!(load_flag & (0x1 << bank)) && !(BSP_INTR(bank) & FIRQ_ALL_FF))
            {
                load_flag = load_flag | (0x1 << bank);
                load_cnt++;
            }
            CLR_BSP_INTR(bank, 0xFF);
        }
    }
    ASSERT(load_cnt == NUM_BANKS);

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        mem_copy(&g_misc_meta[bank], FTL_BUF_ADDR + ((bank) * BYTES_PER_PAGE), sizeof(misc_metadata));
    }
	enable_irq();
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
		+ HIL_BUF_BYTES + TEMP_BUF_BYTES + SCAN_LIST_BYTES + MERGE_BUFFER_BYTES + SMT_PIECES_BYTES;

	if (dram_requirement > DRAM_SIZE)
	{
		while (1);
	}
}
