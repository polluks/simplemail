/***************************************************************************
 SimpleMail - Copyright (C) 2000 Hynek Schlawack and Sebastian Bauer

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <CUnit/Basic.h>

#include "filter.h"

/********************************************************/

static const int positions[] = {1,7,12,18,24};
static int current_position_idx = 0;

int test_boyermoore_callback(char *x, unsigned int pos, void *user_data)
{
	CU_ASSERT(positions[current_position_idx++] == pos);
	return 1;
}

/* @Test */
void test_boyermoore(void)
{
	char *txt = "qhello2hellohello2hello2hello";
	char *pat = "hello";

	int rel_pos, pos;

	struct boyermoore_context *context;

	context = boyermoore_create_context(pat,strlen(pat));
	CU_ASSERT(context != NULL);

	boyermoore(context,txt,strlen(txt),test_boyermoore_callback,NULL);

	CU_ASSERT(current_position_idx == sizeof(positions)/sizeof(positions[0]));

	pos = 0;
	current_position_idx = 0;
	while ((rel_pos = boyermoore(context,txt+pos,strlen(txt+pos),NULL,NULL)) != -1)
	{
		pos += rel_pos;
		CU_ASSERT(positions[current_position_idx++] == pos);
		pos++;
	}
	boyermoore_delete_context(context);

	CU_ASSERT(current_position_idx == sizeof(positions)/sizeof(positions[0]));
}
