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

/*
** addressgrouplistclass.c
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libraries/iffparse.h>
#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/utility.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>

#include "addressbook.h"
#include "debug.h"
#include "smintl.h"

#include "addressgrouplistclass.h"
#include "muistuff.h"

/*
STATIC ASM SAVEDS LONG address_compare(REG(a0, struct Hook *h), REG(a2, Object *obj), REG(a1,struct MUIP_NListtree_CompareMessage *msg))
{
	struct addressbook_entry *entry1 = (struct addressbook_entry *)msg->TreeNode1->tn_User;
	struct addressbook_entry *entry2 = (struct addressbook_entry *)msg->TreeNode2->tn_User;

	return mystricmp(entry1->u.person.realname,entry2->u.person.realname);
}
*/

struct AddressGroupList_Data
{
	struct Hook construct_hook;
	struct Hook destruct_hook;
	struct Hook display_hook;
};

/********************************************
 Constructor for addressgroup entries
*********************************************/
STATIC ASM SAVEDS struct addressbook_group *addressgroup_construct(REG(a0, struct Hook *h), REG(a2, Object *obj),REG(a1,struct NList_ConstructMessage *msg))
{
	struct addressbook_group *entry = (struct addressbook_group *)msg->entry;
	return addressbook_duplicate_group(entry);
}

/********************************************
 Destructor for addressgroup entries
*********************************************/
STATIC ASM SAVEDS VOID addressgroup_destruct(REG(a0, struct Hook *h), REG(a2, Object *obj),REG(a1,struct NList_DestructMessage *msg))
{
	struct addressbook_group *entry = (struct addressbook_group *)msg->entry;
	addressbook_free_group(entry);
}

/********************************************
 Dislayfunction function for addressgroups
*********************************************/
STATIC ASM SAVEDS VOID addressgroup_display(REG(a0,struct Hook *h),REG(a2,Object *obj), REG(a1,struct NList_DisplayMessage *msg))
{
	char **array = msg->strings;
	char **preparse = msg->preparses;
	struct addressbook_group *grp = (struct addressbook_group*)msg->entry;

	if (grp)
		*array = grp->name;
	else *array = _("Name");
}

/********************************************
 OM_NEW
*********************************************/
STATIC ULONG AddressGroupList_New(struct IClass *cl,Object *obj,struct opSet *msg)
{
	struct AddressGroupList_Data *data;
	int type;

	if (!(obj=(Object *)DoSuperNew(cl,obj,
					TAG_MORE,msg->ops_AttrList)))
		return 0;

	data = (struct AddressGroupList_Data*)INST_DATA(cl,obj);

	init_hook(&data->construct_hook,(HOOKFUNC)addressgroup_construct);
	init_hook(&data->destruct_hook,(HOOKFUNC)addressgroup_destruct);
	init_hook(&data->display_hook,(HOOKFUNC)addressgroup_display);

	SetAttrs(obj,
						MUIA_NList_ConstructHook2, &data->construct_hook,
						MUIA_NList_DestructHook2, &data->destruct_hook,
						MUIA_NList_DisplayHook2, &data->display_hook,
						MUIA_NList_Title, TRUE,
						TAG_DONE);

	return (ULONG)obj;
}

/********************************************
 OM_DISPOSE
*********************************************/
STATIC ULONG AddressGroupList_Dispose(struct IClass *cl, Object *obj, Msg msg)
{
	struct AddressGroupList_Data *data = (struct AddressGroupList_Data*)INST_DATA(cl,obj);
	return DoSuperMethodA(cl,obj,msg);
}

/********************************************
 MUIM_AddressGroupList_Refresh
*********************************************/
STATIC ULONG AddressGroupList_Refresh(struct IClass *cl, Object *obj, Msg msg)
{
/*	struct AddressGroupList_Data *data = (struct AddressGroupList_Data*)INST_DATA(cl,obj); */
	struct addressbook_group *grp;

	DoMethod(obj,MUIM_NList_Clear);

	grp = addressbook_first_group();
	while (grp)
	{
		DoMethod(obj,MUIM_NList_InsertSingle,grp,MUIV_NList_Insert_Bottom);
		grp = addressbook_next_group(grp);
	}

	return 0;
}

/********************************************
 Boopsi Dispatcher
*********************************************/
STATIC BOOPSI_DISPATCHER(ULONG,AddressGroupList_Dispatcher,cl,obj,msg)
{
	switch(msg->MethodID)
	{
		case	OM_NEW: return AddressGroupList_New(cl,obj,(struct opSet*)msg);
		case	OM_DISPOSE: return AddressGroupList_Dispose(cl,obj,msg);
		case	MUIM_AddressGroupList_Refresh: return AddressGroupList_Refresh(cl,obj,msg);
		default: return DoSuperMethodA(cl,obj,msg);
	}
}

struct MUI_CustomClass *CL_AddressGroupList;

int create_addressgrouplist_class(void)
{
	SM_ENTER;
	if ((CL_AddressGroupList = CreateMCC(MUIC_NList,NULL,sizeof(struct AddressGroupList_Data),AddressGroupList_Dispatcher)))
	{
		SM_DEBUGF(15,("Create CL_AddressGroupList: 0x%lx\n",CL_AddressGroupList));
		SM_RETURN(1,"%ld");
	}
	SM_DEBUGF(5,("FAILED! Create CL_AddressGroupList\n"));
	SM_RETURN(0,"%ld");
}

void delete_addressgrouplist_class(void)
{
	SM_ENTER;
	if (CL_AddressGroupList)
	{
		if (MUI_DeleteCustomClass(CL_AddressGroupList))
		{
			SM_DEBUGF(15,("Deleted CL_AddressGroupList: 0x%lx\n",CL_AddressGroupList));
			CL_AddressGroupList = NULL;
		} else
		{
			SM_DEBUGF(5,("FAILED! Delete CL_AddressGroupList: 0x%lx\n",CL_AddressGroupList));
		}
	}
	SM_LEAVE;
}
