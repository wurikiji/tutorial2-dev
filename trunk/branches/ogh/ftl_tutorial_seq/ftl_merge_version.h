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


#ifndef FTL_H
#define FTL_H


/////////////////
// DRAM buffers
/////////////////

#define NUM_RW_BUFFERS		((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_RD_BUFFERS		(((NUM_RW_BUFFERS / 8) + NUM_BANKS - 1) / NUM_BANKS * NUM_BANKS)
#define NUM_WR_BUFFERS		(NUM_RW_BUFFERS - NUM_RD_BUFFERS)
#define NUM_COPY_BUFFERS	NUM_BANKS_MAX
#define NUM_FTL_BUFFERS		1
#define NUM_HIL_BUFFERS		1
#define NUM_TEMP_BUFFERS	1

#define DRAM_BYTES_OTHER	((NUM_COPY_BUFFERS + NUM_FTL_BUFFERS + NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS) * BYTES_PER_PAGE \
    + SCAN_LIST_BYTES + MERGE_BUFFER_BYTES + SMT_CACHE_BYTES)
// modified by GYUHWA, RED

#define WR_BUF_PTR(BUF_ID)	(WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define WR_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - WR_BUF_ADDR) / BYTES_PER_PAGE)
#define RD_BUF_PTR(BUF_ID)	(RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define RD_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - RD_BUF_ADDR) / BYTES_PER_PAGE)

#define _COPY_BUF(RBANK)	(COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)		_COPY_BUF(REAL_BANK(BANK))

//added by RED
//////////////////////
// Sector Map Table
//////////////////////

#define SMT_CACHE_PIECES    (TOTAL_SMT_PIECES)      // all pieces are on DRAM.

#define SMT_ELMTS_PER_BANK  (SECTORS_PER_BANK)
#define SMT_BYTES_PER_BANK  (SECTORS_PER_BANK * sizeof(UINT32))
#define TOTAL_SMT_ELMTS     (SMT_ELMTS_PER_BANK * NUM_BANKS)
#define TOTAL_SMT_BYTES     (SMT_BYTES_PER_BANK * NUM_BANKS)
#define TOTAL_SMT_PAGES     (TOTAL_SMT_BYTES / BYTES_PER_PAGE)
#define TOTAL_SMT_PIECES    (TOTAL_SMT_PAGES / NUM_BANKS)
#define PAGES_PER_PIECE     (NUM_BANKS)
#define BYTES_PER_PIECE     (BYTES_PER_PAGE * NUM_BANKS)
#define ELMTS_PER_PIECE     (BYTES_PER_PIECE / sizeof(UINT32))
#define ELMTS_PER_PAGE      (BYTES_PER_PAGE / sizeof(UINT32))

#define REQ_SMT_BLKS        (TOTAL_SMT_BNDLS * NUM_BANKS)
///////////////////////////////
// DRAM segmentation
///////////////////////////////

#define RD_BUF_ADDR			DRAM_BASE										// base address of SATA read buffers
#define RD_BUF_BYTES		(NUM_RD_BUFFERS * BYTES_PER_PAGE)

#define WR_BUF_ADDR			(RD_BUF_ADDR + RD_BUF_BYTES)					// base address of SATA write buffers
#define WR_BUF_BYTES		(NUM_WR_BUFFERS * BYTES_PER_PAGE)

#define COPY_BUF_ADDR		(WR_BUF_ADDR + WR_BUF_BYTES)					// base address of flash copy buffers
#define COPY_BUF_BYTES		(NUM_COPY_BUFFERS * BYTES_PER_PAGE)

#define FTL_BUF_ADDR		(COPY_BUF_ADDR + COPY_BUF_BYTES)				// a buffer dedicated to FTL internal purpose
#define FTL_BUF_BYTES		(NUM_FTL_BUFFERS * BYTES_PER_PAGE)

#define HIL_BUF_ADDR		(FTL_BUF_ADDR + FTL_BUF_BYTES)					// a buffer dedicated to HIL internal purpose
#define HIL_BUF_BYTES		(NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR		(HIL_BUF_ADDR + HIL_BUF_BYTES)					// general purpose buffer
#define TEMP_BUF_BYTES		(NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define SCAN_LIST_ADDR		(TEMP_BUF_ADDR + TEMP_BUF_BYTES)				// list of initial bad blocks
#define SCAN_LIST_BYTES		(SCAN_LIST_SIZE * NUM_BANKS)

#define MERGE_BUFFER_ADDR	(SCAN_LIST_ADDR + SCAN_LIST_BYTES)				// merge buffer (added by GYUHWA, modified by RED)
#define MERGE_BUFFER_BYTES	(((NUM_BANKS * BYTES_PER_PAGE + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR )* BYTES_PER_SECTOR)

// added by RED
#define SMT_CACHE_ADDR      (MERGE_BUFFER_ADDR + MERGE_BUFFER_BYTES)        // Sector-mapping table (by RED)
#define SMT_CACHE_BYTES     ((SMT_CACHE_PIECES * BYTES_PER_PIECE + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

///////////////////////////////
// FTL public functions
///////////////////////////////

void ftl_open(void);
//modified by RED (lba -> lba)
void ftl_read(UINT32 const lba, UINT32 const num_sectors);
void ftl_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_flush(void);
void ftl_isr(void);
void ftl_write_sector(UINT32 const lba);
void ftl_read_sector(UINT32 const lba,UINT32 const);

#endif //FTL_H
