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
** composewnd.c
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libraries/asl.h>
#include <libraries/iffparse.h> /* MAKE_ID */
#include <libraries/mui.h>
#include <mui/nlistview_mcc.h>
#include <mui/nlisttree_mcc.h>
#include <mui/betterstring_mcc.h> /* there also exists new newer version of this class */
#include <mui/texteditor_mcc.h>
#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "account.h"
#include "addressbook.h"
#include "codecs.h"
#include "codesets.h"
#include "configuration.h"
#include "taglines.h"
#include "folder.h"
#include "mail.h"
#include "parse.h"
#include "signature.h"
#include "simplemail.h"
#include "smintl.h"
#include "support_indep.h"

#include "addressstringclass.h"
#include "amigasupport.h"
#include "attachmentlistclass.h"
#include "composeeditorclass.h"
#include "compiler.h"
#include "composewnd.h"
#include "datatypesclass.h"
#include "mainwnd.h" /* main_refresh_mail() */
#include "muistuff.h"
#include "picturebuttonclass.h"
#include "utf8stringclass.h"

struct MUI_NListtree_TreeNode *FindListtreeUserData(Object *tree, APTR udata); /* in mainwnd.c */

#define MAX_COMPOSE_OPEN 10
static struct Compose_Data *compose_open[MAX_COMPOSE_OPEN];

struct Compose_Data /* should be a customclass */
{
	Object *wnd;
	Object *from_text;
	Object *to_string;
	Object *cc_string;
	Object *subject_string;
	Object *reply_string;
	Object *copy_button;
	Object *cut_button;
	Object *paste_button;
	Object *undo_button;
	Object *redo_button;
	Object *x_text;
	Object *y_text;
	Object *text_texteditor;
	Object *quick_attach_tree;
	Object *attach_tree;
	Object *attach_desc_string;
	Object *contents_page;
	Object *datatype_datatypes;
	Object *encrypt_button;
	Object *sign_button;

	char *filename; /* the emails filename if changed */
	char *folder; /* the emails folder if changed */
	char *reply_id; /* the emails reply-id if changed */
	int compose_action;
	struct mail *ref_mail; /* the mail which status should be changed after editing */

	struct FileRequester *file_req;

	struct attachment *last_attachment;

	int num; /* the number of the window */
	/* more to add */

	struct Hook from_objstr_hook;
	struct Hook from_strobj_hook;

	char **sign_array; /* The array which contains the signature names */

	int attachment_unique_id;
};

STATIC ASM VOID from_objstr(register __a2 Object *list, register __a1 Object *str)
{
	char *x;
	Object *reply = (Object*)xget(str,MUIA_UserData);
	DoMethod(list,MUIM_NList_GetEntry,MUIV_NList_GetEntry_Active,&x);
	if (!x) return;

	set(str,MUIA_Text_Contents,x);
	if (reply)
	{
		struct account *ac = (struct account*)list_find(&user.config.account_list,xget(list,MUIA_NList_Active));
		if (ac)
		{
			set(reply,MUIA_String_Contents,ac->reply);
		}
	}
}

STATIC ASM LONG from_strobj(register __a2 Object *list, register __a1 Object *str)
{
	char *x,*s;
	int i,entries = xget(list,MUIA_NList_Entries);

	get(str,MUIA_Text_Contents,&s);

	for (i=0;i<entries;i++)
	{
		DoMethod(list,MUIM_NList_GetEntry,i,&x);
		if (x)
		{
			if (!mystricmp(x,s))
	  	{
				set(list,MUIA_NList_Active,i);
				return 1;
			}
		}
	}

	set(list,MUIA_NList_Active,MUIV_NList_Active_Off);
	return 1;
}


/******************************************************************
 This close and disposed the window (note: this must not be called
 within a normal callback hook (because the object is disposed in
 this function))!
*******************************************************************/
static void compose_window_dispose(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	set(data->wnd,MUIA_Window_Open,FALSE);
	DoMethod(App,OM_REMMEMBER,data->wnd);
	set(data->datatype_datatypes, MUIA_DataTypes_FileName, NULL);
	MUI_DisposeObject(data->wnd);
	if (data->file_req) MUI_FreeAslRequest(data->file_req);
	if (data->filename) free(data->filename);
	if (data->folder) free(data->folder);
	if (data->reply_id) free(data->reply_id);
	if (data->num < MAX_COMPOSE_OPEN) compose_open[data->num] = 0;
	if (data->sign_array) free(data->sign_array);
	free(data);
}

/******************************************************************
 Expand the to string. Returns 1 for a success else 0
*******************************************************************/
static int compose_expand_to(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	char *str = addressbook_get_expand_str((char*)xget(data->to_string, MUIA_String_Contents));
	if (str)
	{
		set(data->to_string, MUIA_String_Contents, str);
		free(str);
		return 1;
	}
	DisplayBeep(NULL);
	set(data->wnd, MUIA_Window_ActiveObject,data->to_string);
	return 0;
}

/******************************************************************
 Expand the to string. Returns 1 for a success else 0
*******************************************************************/
static int compose_expand_cc(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	char *cc_contents = (char*)xget(data->cc_string, MUIA_String_Contents);
	char *str;

	if (cc_contents && *cc_contents)
	{
		if ((str = addressbook_get_expand_str(cc_contents)))
		{
			set(data->cc_string, MUIA_String_Contents, str);
			free(str);
			return 1;
		}
		DisplayBeep(NULL);
		set(data->wnd, MUIA_Window_ActiveObject,data->cc_string);
		return 1;
	}
	return 1;
}

/******************************************************************
 Add a attchment to the treelist
*******************************************************************/
static void compose_add_attachment(struct Compose_Data *data, struct attachment *attach, int list)
{
	struct MUI_NListtree_TreeNode *treenode = (struct MUI_NListtree_TreeNode *)xget(data->attach_tree, MUIA_NListtree_ActiveList);
	struct MUI_NListtree_TreeNode *activenode = (struct MUI_NListtree_TreeNode *)xget(data->attach_tree, MUIA_NListtree_Active);
	struct MUI_NListtree_TreeNode *insertlist = (struct MUI_NListtree_TreeNode *)MUIV_NListtree_Insert_ListNode_ActiveFallback;
	int quiet = 0;
	int act;

	if (activenode)
	{
		if (activenode->tn_Flags & TNF_LIST)
		{
			/* if the active node is a list add the new node to this list */
			insertlist = activenode;
			treenode = insertlist;
		}
	}

	if (!treenode)
	{
		/* no list */
		if ((treenode = (struct MUI_NListtree_TreeNode *)xget(data->attach_tree, MUIA_NListtree_Active)))
		{
			/* we have another entry inside but no list (multipart), so insert also a multipart node */
			struct attachment multipart;

			memset(&multipart, 0, sizeof(multipart));
			multipart.content_type = "multipart/mixed";
			multipart.unique_id = data->attachment_unique_id++;

			quiet = 1;
			set(data->attach_tree, MUIA_NListtree_Quiet, TRUE);

			insertlist = (struct MUI_NListtree_TreeNode *)DoMethod(data->attach_tree, MUIM_NListtree_Insert, "", &multipart,
					MUIV_NListtree_Insert_ListNode_ActiveFallback, MUIV_NListtree_Insert_PrevNode_Tail,TNF_OPEN|TNF_LIST);

			if (insertlist)
			{
				DoMethod(data->attach_tree, MUIM_NListtree_Move, MUIV_NListtree_Move_OldListNode_Active, MUIV_NListtree_Move_OldTreeNode_Active,
								insertlist, MUIV_NListtree_Move_NewTreeNode_Tail);
			} else
			{
				set(data->attach_tree, MUIA_NListtree_Quiet, FALSE);
				return;
			}
		}
	}

	act = !xget(data->attach_tree, MUIA_NListtree_Active);

	treenode = (struct MUI_NListtree_TreeNode *)DoMethod(data->attach_tree, MUIM_NListtree_Insert, "" /*name*/, attach, /* udata */
					 insertlist,MUIV_NListtree_Insert_PrevNode_Tail, (list?(TNF_OPEN|TNF_LIST):(act?MUIV_NListtree_Insert_Flag_Active:0)));

	/* for the quick attachments list */
	if (!list && treenode)
	{
		DoMethod(data->quick_attach_tree, MUIM_NListtree_Insert, "", attach, /* udata */
					MUIV_NListtree_Insert_ListNode_Root, MUIV_NListtree_Insert_PrevNode_Tail, (act?MUIV_NListtree_Insert_Flag_Active:0));
	}
					

	if (quiet)
	{
		set(data->attach_tree, MUIA_NListtree_Quiet, FALSE);
	}
}


/******************************************************************
 Add a multipart node to the list
*******************************************************************/
static void compose_add_text(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct attachment attach;

	memset(&attach, 0, sizeof(attach));
	attach.content_type = "text/plain";
	attach.editable = 1;
	attach.unique_id = data->attachment_unique_id++;

	compose_add_attachment(data,&attach,0);
}

/******************************************************************
 Add a multipart node to the list
*******************************************************************/
static void compose_add_multipart(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct attachment attach;

	memset(&attach, 0, sizeof(attach));
	attach.content_type = "multipart/mixed";
	attach.editable = 0;
	attach.unique_id = data->attachment_unique_id++;

	compose_add_attachment(data,&attach,1);
}

/******************************************************************
 Add files to the list
*******************************************************************/
static void compose_add_files(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct attachment attach;

	if (data->file_req)
	{
		if (MUI_AslRequestTags(data->file_req,ASLFR_DoMultiSelect, TRUE, TAG_DONE))
		{
			int i;
			memset(&attach, 0, sizeof(attach));

			for (i=0; i<data->file_req->fr_NumArgs;i++)
			{
				STRPTR drawer = NameOfLock(data->file_req->fr_ArgList[i].wa_Lock);
				if (drawer)
				{
					int len = strlen(drawer)+strlen(data->file_req->fr_ArgList[i].wa_Name)+4;
					STRPTR buf = (STRPTR)AllocVec(len,MEMF_PUBLIC);
					if (buf)
					{
						strcpy(buf,drawer);
						AddPart(buf,data->file_req->fr_ArgList[i].wa_Name,len);
						attach.content_type = identify_file(buf);
						attach.editable = 0;
						attach.filename = buf;
						attach.unique_id = data->attachment_unique_id++;

						compose_add_attachment(data,&attach,0);
						FreeVec(buf);
					}
					FreeVec(drawer);
				}
			}
		}
	}
}

/******************************************************************
 Add files to the list
*******************************************************************/
static void compose_remove_file(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct MUI_NListtree_TreeNode *treenode = (struct MUI_NListtree_TreeNode*)DoMethod(data->attach_tree, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Active, MUIV_NListtree_GetEntry_Position_Active,0);
	int rem;

	rem = DoMethod(data->attach_tree, MUIM_NListtree_GetNr, treenode, MUIV_NListtree_GetNr_Flag_CountLevel) == 2;

	treenode = (struct MUI_NListtree_TreeNode*)DoMethod(data->attach_tree, MUIM_NListtree_GetEntry, treenode, MUIV_NListtree_GetEntry_Position_Parent,0);

	if (treenode && rem) set(data->attach_tree, MUIA_NListtree_Quiet,TRUE);
	DoMethod(data->attach_tree, MUIM_NListtree_Remove, MUIV_NListtree_Remove_ListNode_Active, MUIV_NListtree_Remove_TreeNode_Active, 0);

	if (treenode && rem)
	{
		DoMethod(data->attach_tree, MUIM_NListtree_Move, treenode, MUIV_NListtree_Move_OldTreeNode_Head, MUIV_NListtree_Move_NewListNode_Root, MUIV_NListtree_Move_NewTreeNode_Head, 0); 
		DoMethod(data->attach_tree, MUIM_NListtree_Remove, MUIV_NListtree_Remove_ListNode_Root, treenode, 0); 
		set(data->attach_tree, MUIA_NListtree_Active, MUIV_NListtree_Active_First); 
		set(data->attach_tree, MUIA_NListtree_Quiet,FALSE);
	}
}

/******************************************************************
 A new attachment has been clicked
*******************************************************************/
static void compose_quick_attach_active(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct MUI_NListtree_TreeNode *activenode = (struct MUI_NListtree_TreeNode *)xget(data->quick_attach_tree, MUIA_NListtree_Active);

	if (activenode && activenode->tn_User)
	{
		if ((activenode = (struct MUI_NListtree_TreeNode*)DoMethod(data->attach_tree, MUIM_AttachmentList_FindUniqueID, ((struct attachment *)activenode->tn_User)->unique_id)))
		{
			SetAttrs(data->attach_tree,
					MUIA_NListtree_Active, activenode,
					TAG_DONE);
		}
	}
}

/******************************************************************
 A new attachment has been clicked
*******************************************************************/
static void compose_attach_active(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct MUI_NListtree_TreeNode *activenode = (struct MUI_NListtree_TreeNode *)xget(data->attach_tree, MUIA_NListtree_Active);
	struct attachment *attach = NULL;

	if (data->last_attachment && data->last_attachment->editable)
	{
		/* Try if the attachment is still in the list (could be not the case if removed) */
		struct MUI_NListtree_TreeNode *tn = FindListtreeUserData(data->attach_tree, data->last_attachment);
		if (tn)
		{
			STRPTR text_buf;

			set(data->text_texteditor, MUIA_TextEditor_ExportHook, MUIV_TextEditor_ExportHook_EMail);

			if ((text_buf = (STRPTR)DoMethod(data->text_texteditor, MUIM_TextEditor_ExportText)))
			{
				/* free the memory of the last contents */
				if (data->last_attachment->contents) free(data->last_attachment->contents);
				data->last_attachment->contents = mystrdup(text_buf);
				data->last_attachment->lastxcursor = xget(data->text_texteditor, MUIA_TextEditor_CursorX);
				data->last_attachment->lastycursor = xget(data->text_texteditor, MUIA_TextEditor_CursorY);
				FreeVec(text_buf);
			}
			set(data->text_texteditor, MUIA_TextEditor_ExportHook, MUIV_TextEditor_ExportHook_Plain);
		}
	}

	if (activenode)
	{
		attach = (struct attachment *)activenode->tn_User;
	}

	if (attach)
	{
		if (attach != data->last_attachment)
		{
			if (attach->editable)
			{
				set(data->text_texteditor, MUIA_TextEditor_ImportHook, MUIV_TextEditor_ImportHook_EMail);
				SetAttrs(data->text_texteditor,
						MUIA_TextEditor_Contents, attach->contents?attach->contents:"",
						MUIA_TextEditor_CursorX,attach->lastxcursor,
						MUIA_TextEditor_CursorY,attach->lastycursor,
						MUIA_NoNotify, TRUE,
						TAG_DONE);
				set(data->text_texteditor, MUIA_TextEditor_ImportHook, MUIV_TextEditor_ImportHook_Plain);

				DoMethod(data->x_text, MUIM_SetAsString, MUIA_Text_Contents, "%04ld", xget(data->text_texteditor,MUIA_TextEditor_CursorX));
				DoMethod(data->y_text, MUIM_SetAsString, MUIA_Text_Contents, "%04ld", xget(data->text_texteditor,MUIA_TextEditor_CursorY));

				set(data->wnd, MUIA_Window_ActiveObject, data->text_texteditor);
			}

			SetAttrs(data->contents_page,
					MUIA_Disabled, FALSE,
					MUIA_Group_ActivePage, attach->editable?0:1,
					TAG_DONE);

			set(data->datatype_datatypes, MUIA_DataTypes_FileName, attach->temporary_filename?attach->temporary_filename:attach->filename);
			set(data->attach_desc_string, MUIA_String_Contents, attach->description);
		}

		if ((activenode = (struct MUI_NListtree_TreeNode*)DoMethod(data->quick_attach_tree, MUIM_AttachmentList_FindUniqueID, attach->unique_id)))
		{
			SetAttrs(data->quick_attach_tree,
					MUIA_NoNotify, TRUE,
					MUIA_NListtree_Active,activenode,
					TAG_DONE);
		}
	} else
	{
/*		set(data->contents_page, MUIA_Disabled, TRUE);*/
	}
	data->last_attachment = attach;
}

/******************************************************************
 Attach the mail given in the treenode to the current mail
*******************************************************************/
static void compose_attach_desc(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	struct MUI_NListtree_TreeNode *activenode = (struct MUI_NListtree_TreeNode *)xget(data->attach_tree, MUIA_NListtree_Active);
	struct attachment *attach = NULL;

	if (activenode)
	{
		if ((attach = (struct attachment *)activenode->tn_User))
		{
			free(attach->description);
			attach->description = mystrdup((char*)xget(data->attach_desc_string,MUIA_String_Contents));
			DoMethod(data->attach_tree, MUIM_NListtree_Redraw, MUIV_NListtree_Redraw_Active, 0);
		}
	}
}

/******************************************************************
 Attach the mail given in the treenode to the current mail
 (recursive)
*******************************************************************/
static void compose_window_attach_mail(struct Compose_Data *data, struct MUI_NListtree_TreeNode *treenode, struct composed_mail *cmail)
{
	struct attachment *attach;

	if (!treenode) treenode = (struct MUI_NListtree_TreeNode *)DoMethod(data->attach_tree,
				MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, MUIV_NListtree_GetEntry_Position_Head, 0);

	if (!treenode) return;
	if (!(attach = (struct attachment *)treenode->tn_User)) return;

	cmail->content_type = mystrdup(attach->content_type);
	cmail->content_description = utf8create(attach->description,user.config.default_codeset?user.config.default_codeset->name:NULL);

	if (treenode->tn_Flags & TNF_LIST)
	{
		struct MUI_NListtree_TreeNode *tn = (struct MUI_NListtree_TreeNode *)DoMethod(data->attach_tree,
				MUIM_NListtree_GetEntry, treenode, MUIV_NListtree_GetEntry_Position_Head, 0);


		while (tn)
		{
			struct composed_mail *newcmail = (struct composed_mail *)malloc(sizeof(struct composed_mail));
			if (newcmail)
			{
				composed_mail_init(newcmail);
				compose_window_attach_mail(data,tn,newcmail);
				list_insert_tail(&cmail->list,&newcmail->node);
			}
			tn = (struct MUI_NListtree_TreeNode*)DoMethod(data->attach_tree, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Next,0);
		}
	} else
	{
		cmail->text = (attach->contents)?utf8create(attach->contents,user.config.default_codeset?user.config.default_codeset->name:NULL):NULL;
		cmail->filename = mystrdup(attach->filename);
		cmail->temporary_filename = mystrdup(attach->temporary_filename);
	}
}

/******************************************************************
 Compose a mail and close the window
*******************************************************************/
static void compose_mail(struct Compose_Data *data, int hold)
{
	if (compose_expand_to(&data) && compose_expand_cc(&data))
	{
		char *from = (char*)xget(data->from_text, MUIA_Text_Contents);
		char *to = (char*)xget(data->to_string, MUIA_UTF8String_Contents);
		char *cc = (char*)xget(data->cc_string, MUIA_UTF8String_Contents);
		char *subject = (char*)xget(data->subject_string, MUIA_UTF8String_Contents);
		char *reply = (char*)xget(data->reply_string, MUIA_String_Contents);
		struct composed_mail new_mail;

		/* update the current attachment */
		compose_attach_active(&data);

		/* Initialize the structure with default values */
		composed_mail_init(&new_mail);

		/* Attach the mails recursivly */
		compose_window_attach_mail(data, NULL /*root*/, &new_mail);

		/* TODO: free this stuff!! */

		new_mail.from = from;
		new_mail.replyto = reply;
		new_mail.to = to;
		new_mail.cc = cc;
		new_mail.subject = subject;
		new_mail.mail_filename = data->filename;
		new_mail.mail_folder = data->folder;
		new_mail.reply_message_id = data->reply_id;
		new_mail.encrypt = xget(data->encrypt_button,MUIA_Selected);
		new_mail.sign = xget(data->sign_button,MUIA_Selected);

		/* Move this out */
		if ((mail_compose_new(&new_mail,hold)))
		{
			/* Change the status of a mail if it was replied or forwarded */
			if (data->ref_mail && mail_get_status_type(data->ref_mail) != MAIL_STATUS_SENT
												 && mail_get_status_type(data->ref_mail) != MAIL_STATUS_WAITSEND)
			{
				if (data->compose_action == COMPOSE_ACTION_REPLY)
				{
					struct folder *f = folder_find_by_mail(data->ref_mail);
					if (f)
					{
						folder_set_mail_status(f, data->ref_mail, MAIL_STATUS_REPLIED|(data->ref_mail->status & MAIL_STATUS_FLAG_MARKED));
						main_refresh_mail(data->ref_mail);
					}
				} else
				{
					if (data->compose_action == COMPOSE_ACTION_FORWARD)
					{
						struct folder *f = folder_find_by_mail(data->ref_mail);
						folder_set_mail_status(f, data->ref_mail, MAIL_STATUS_FORWARD|(data->ref_mail->status & MAIL_STATUS_FLAG_MARKED));
						main_refresh_mail(data->ref_mail);
					}
				}
			}
			/* Close (and dispose) the compose window (data) */
			DoMethod(App, MUIM_Application_PushMethod, App, 4, MUIM_CallHook, &hook_standard, compose_window_dispose, data);
		}
	}
}

/******************************************************************
 The mail should be send immediatly
*******************************************************************/
static void compose_window_send_now(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	compose_mail(data,2);
}

/******************************************************************
 A mail should be send later
*******************************************************************/
static void compose_window_send_later(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	compose_mail(data,0);
}

/******************************************************************
 A mail should be hold
*******************************************************************/
static void compose_window_hold(struct Compose_Data **pdata)
{
	struct Compose_Data *data = *pdata;
	compose_mail(data,1);
}

/******************************************************************
 inserts a mail into the listtree (uses recursion)
*******************************************************************/
static void compose_add_mail(struct Compose_Data *data, struct mail *mail, struct MUI_NListtree_TreeNode *listnode)
{
	/* Note, the following three datas are static although the function is recursive
	 * It minimalizes the possible stack overflow
	*/
	static char buf[128];
	static char tmpname[L_tmpnam+1];
	static struct attachment attach;
	struct MUI_NListtree_TreeNode *treenode;
	int i,num_multiparts = mail->num_multiparts;

	memset(&attach,0,sizeof(attach));

	if (mail->content_type)
	{
		/* If the mail has a content type */
		sprintf(buf,"%s/%s",mail->content_type,mail->content_subtype);
	} else
	{
		/* Use text/plain as the default content type */
		strcpy(buf,"text/plain");
	}

	attach.content_type = buf;
	attach.unique_id = data->attachment_unique_id++;

	if (mail->content_description && *mail->content_description)
	{
		int len = strlen(mail->content_description)+1;
		if ((attach.description = malloc(len)))
		{
			utf8tostr(mail->content_description, attach.description, len, user.config.default_codeset);
		}
	}

	if (!num_multiparts)
	{
		void *cont;
		int cont_len;

		/* decode the mail */
		mail_decode(mail);

		mail_decoded_data(mail,&cont,&cont_len);

		/* if the content type is a text it can be edited */
		if (!mystricmp(buf,"text/plain"))
		{
			char *isobuf = NULL;
			char *cont_dup = mystrndup((char*)cont,cont_len); /* we duplicate this only because we need a null byte */

			if (cont_dup)
			{
				if ((isobuf = (char*)malloc(cont_len+1)))
					utf8tostr(cont_dup, isobuf, cont_len+1, user.config.default_codeset);
				free(cont_dup);
			}

			attach.contents = isobuf;
			attach.editable = 1;
			attach.lastxcursor = 0;
			attach.lastycursor = 0;
		} else
		{
			BPTR fh;

			tmpnam(tmpname);

			if ((fh = Open(tmpname,MODE_NEWFILE)))
			{
				Write(fh,cont,cont_len);
				Close(fh);
			}
			attach.filename = mail->filename?mail->filename:tmpname;
			attach.temporary_filename = tmpname;
		}
	}

	if (!num_multiparts)
	{
		DoMethod(data->quick_attach_tree,MUIM_NListtree_Insert,"",&attach,
						 MUIV_NListtree_Insert_ListNode_Root,
						 MUIV_NListtree_Insert_PrevNode_Tail,0);
	}

	treenode = (struct MUI_NListtree_TreeNode *)DoMethod(data->attach_tree,MUIM_NListtree_Insert,"",&attach,listnode,MUIV_NListtree_Insert_PrevNode_Tail,num_multiparts?(TNF_LIST|TNF_OPEN):0);

	free(attach.contents);
	free(attach.description);

	if (!treenode) return;

	for (i=0;i<num_multiparts; i++)
	{
		compose_add_mail(data,mail->multipart_array[i],treenode);
	}
}

/******************************************************************
 Add a signature if neccesary
*******************************************************************/
static void compose_add_signature(struct Compose_Data *data)
{
	struct signature *sign = (struct signature*)list_first(&user.config.signature_list);
	if (user.config.signatures_use && sign && sign->signature)
	{
		char *text = (char*)DoMethod(data->text_texteditor, MUIM_TextEditor_ExportText);
		int add_sign = 0;

		if (text)
		{
			add_sign = strstr(text,"\n-- \n")?(0):(!!strncmp("-- \n",text,4));
		}
		if (add_sign)
		{
			char *sign_iso = utf8tostrcreate(sign->signature,user.config.default_codeset);
			if (sign_iso)
			{
				char *new_text = (char*)malloc(strlen(text) + strlen(sign_iso) + 50);
				if (new_text)
				{
					strcpy(new_text,text);
					strcat(new_text,"\n-- \n");
					strcat(new_text,sign_iso);

					new_text = taglines_add_tagline(new_text);
				
/*
					DoMethod(data->text_texteditor,MUIM_TextEditor_InsertText,"\n-- \n", MUIV_TextEditor_InsertText_Bottom);
					DoMethod(data->text_texteditor,MUIM_TextEditor_InsertText,sign->signature, MUIV_TextEditor_InsertText_Bottom);
*/
					SetAttrs(data->text_texteditor,
							MUIA_TextEditor_CursorX,0,
							MUIA_TextEditor_CursorY,0,
							MUIA_TextEditor_Contents,new_text,
							TAG_DONE);

					free(new_text);
				}
				free(sign_iso);
			}
		}

		if (text) FreeVec(text);
	}
}

/******************************************************************
 Set a signature
*******************************************************************/
static void compose_set_signature(void **msg)
{
	struct Compose_Data *data = (struct Compose_Data*)msg[0];
	int val = (int)msg[1];
	struct signature *sign = (struct signature*)list_find(&user.config.signature_list,val);
	char *text;
	int x = xget(data->text_texteditor,MUIA_TextEditor_CursorX);
	int y = xget(data->text_texteditor,MUIA_TextEditor_CursorY);

	if (!sign)
	{
		if ((text = (char*)DoMethod(data->text_texteditor, MUIM_TextEditor_ExportText)))
		{
			char *sign_text = strstr(text,"\n-- \n");
			if (sign_text)
			{
				*sign_text = 0;

				SetAttrs(data->text_texteditor,
						MUIA_TextEditor_Contents,text,
						MUIA_TextEditor_CursorX,x,
						MUIA_TextEditor_CursorY,y,
						TAG_DONE);
			}
			FreeVec(text);
		}
		return;
	}
	if (!sign->signature) return;

	if ((text = (char*)DoMethod(data->text_texteditor, MUIM_TextEditor_ExportText)))
	{
		char *sign_text = strstr(text,"\n-- \n");
		char *sign_iso = utf8tostrcreate(sign->signature,user.config.default_codeset);
		char *new_text;

		if (sign_text) *sign_text = 0;

		if (sign_iso)
		{
			if ((new_text = (char*)malloc(strlen(text)+strlen(sign_iso)+10)))
			{
				strcpy(new_text,text);
				strcat(new_text,"\n-- \n");
				strcat(new_text,sign_iso);

				new_text = taglines_add_tagline(new_text);

				SetAttrs(data->text_texteditor,
						MUIA_TextEditor_Contents,new_text,
						MUIA_TextEditor_CursorX,x,
						MUIA_TextEditor_CursorY,y,
						TAG_DONE);

				free(new_text);
			}
			free(sign_iso);
		}
		FreeVec(text);
	}
}

/******************************************************************
 New Gadget should be activated
*******************************************************************/
static void compose_new_active(void **msg)
{
	struct Compose_Data *data = (struct Compose_Data *)msg[0];
	Object *obj = (Object*)msg[1];

	if ((ULONG)obj == (ULONG)MUIV_Window_ActiveObject_Next &&
		  (ULONG)xget(data->wnd, MUIA_Window_ActiveObject) == (ULONG)data->copy_button)
	{
		set(data->wnd, MUIA_Window_ActiveObject, data->text_texteditor);
	}
}

/******************************************************************
 Opens a compose window
*******************************************************************/
int compose_window_open(struct compose_args *args)
{
	Object *wnd, *send_later_button, *hold_button, *cancel_button, *send_now_button;
	Object *from_text, *from_list, *reply_string, *to_string, *cc_string, *subject_string;
	Object *copy_button, *cut_button, *paste_button,*undo_button,*redo_button;
	Object *text_texteditor, *xcursor_text, *ycursor_text, *slider;
	Object *datatype_datatypes;
	Object *expand_to_button, *expand_cc_button;
	Object *quick_attach_tree;
	Object *attach_tree, *attach_desc_string, *add_text_button, *add_multipart_button, *add_files_button, *remove_button;
	Object *contents_page;
	Object *from_popobject;
	Object *signatures_group;
	Object *signatures_cycle;
	Object *add_attach_button;
	Object *encrypt_button;
	Object *sign_button;

	struct signature *sign;
	char **sign_array = NULL;
	int num;
	int i;

	static char *register_titles[3];
	static int register_titles_are_translated;

	if (!register_titles_are_translated)
	{
		register_titles[0] = _("Mail");
		register_titles[1] = _("Attachments");
		register_titles_are_translated = 1;
	};


	for (num=0; num < MAX_COMPOSE_OPEN; num++)
		if (!compose_open[num]) break;

	if (num == MAX_COMPOSE_OPEN) return -1;

	i = list_length(&user.config.signature_list);

	if (user.config.signatures_use && i)
	{
		if ((sign_array = (char**)malloc((i+2)*sizeof(char*))))
		{
			int j=0;
			sign = (struct signature*)list_first(&user.config.signature_list);
			while (sign)
			{
				sign_array[j]=sign->name;
				sign = (struct signature*)node_next(&sign->node);
				j++;
			}
			sign_array[j] = _("No Signature");
			sign_array[j+1] = NULL;
		}

		signatures_group = HGroup,
			MUIA_Weight, 33,
			Child, MakeLabel(_("Use signature")),
			Child, signatures_cycle = MakeCycle(_("Use signature"),sign_array),
			End;
	} else
	{
		signatures_group = NULL;
		signatures_cycle = NULL;
	}

	slider = ScrollbarObject, End;

	wnd = WindowObject,
		MUIA_HelpNode, "WR_W",
		(num < MAX_COMPOSE_OPEN)?MUIA_Window_ID:TAG_IGNORE, MAKE_ID('C','O','M',num),
		MUIA_Window_Title, _("SimpleMail - Compose Message"),
		  
		WindowContents, VGroup,
			Child, RegisterGroup(register_titles),
				/* First register */
				Child, VGroup,
					Child, HGroup,
						Child, reply_string = StringObject, MUIA_ShowMe, FALSE, End,
						Child, ColGroup(2),
							Child, MakeLabel(_("_From")),
							Child, from_popobject = PopobjectObject,
								MUIA_Popstring_Button, PopButton(MUII_PopUp),
								MUIA_Popstring_String, from_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack, End,
								MUIA_Popobject_Object, NListviewObject,
									MUIA_NListview_NList, from_list = NListObject,
										MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
										MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
										End,
									End,
								End,
							Child, MakeLabel(_("_To")),
							Child, HGroup,
								MUIA_Group_Spacing,0,
								Child, to_string = AddressStringObject,
									StringFrame,
									MUIA_CycleChain, 1,
									MUIA_ControlChar, GetControlChar(_("_To")),
									MUIA_String_AdvanceOnCR, TRUE,
									End,
								Child, expand_to_button = PopButton(MUII_ArrowLeft),
								End,
							Child, MakeLabel(_("Copies To")),
							Child, HGroup,
								MUIA_Group_Spacing,0,
								Child, cc_string = AddressStringObject,
									StringFrame,
									MUIA_CycleChain, 1,
									MUIA_ControlChar, GetControlChar(_("Copies To")),
									MUIA_String_AdvanceOnCR, TRUE,
									End,
								Child, expand_cc_button = PopButton(MUII_ArrowLeft),
								End,
							Child, MakeLabel(_("S_ubject")),
							Child, subject_string = UTF8StringObject,
								StringFrame,
								MUIA_CycleChain, 1,
								MUIA_ControlChar, GetControlChar(_("S_ubject")),
								End,
							End,
						Child, BalanceObject, End,
						Child, NListviewObject,
							MUIA_Weight, 50,
							MUIA_CycleChain, 1,
							MUIA_NListview_NList, quick_attach_tree = AttachmentListObject,
								MUIA_AttachmentList_Quick, TRUE,
								End,
							End,
						End,
					Child, contents_page = PageGroup,
						MUIA_Group_ActivePage, 0,
						Child, VGroup,
							Child, HGroup,
								MUIA_VertWeight,0,
								Child, HGroup,
									MUIA_Group_Spacing,0,
									Child, copy_button = MakePictureButton(_("Copy"),"PROGDIR:Images/Copy"),
									Child, cut_button = MakePictureButton(_("Cut"),"PROGDIR:Images/Cut"),
									Child, paste_button = MakePictureButton(_("Paste"),"PROGDIR:Images/Paste"),
									End,
								Child, HGroup,
									MUIA_Weight, 66,
									MUIA_Group_Spacing,0,
									Child, undo_button = MakePictureButton(_("Undo"),"PROGDIR:Images/Undo"),
									Child, redo_button = MakePictureButton(_("Redo"),"PROGDIR:Images/Redo"),
									End,
								Child, HGroup,
									MUIA_Weight, 33,
									MUIA_Group_Spacing,0,
									Child, add_attach_button = MakePictureButton(_("_Attach"),"PROGDIR:Images/AddAttachment"),
									End,
								Child, HGroup,
									MUIA_Weight, 33,
									MUIA_Group_Spacing,0,
									Child, encrypt_button = MakePictureButton(_("_Encrypt"),"PROGDIR:Images/Encrypt"),
									Child, sign_button = MakePictureButton(_("Si_gn"),"PROGDIR:Images/Sign"),
									End,
								Child, RectangleObject,
									MUIA_FixHeight,1,
									MUIA_HorizWeight,signatures_group?33:100,
									End,
								signatures_group?Child:TAG_IGNORE,signatures_group,
								signatures_group?Child:TAG_IGNORE,RectangleObject,
									MUIA_FixHeight,1,
									MUIA_HorizWeight,signatures_group?33:100,
									End,
								Child, VGroup,
									TextFrame,
									MUIA_Background, MUII_TextBack,
									MUIA_Group_Spacing, 0,
									Child, xcursor_text = TextObject,
										MUIA_Font, MUIV_Font_Fixed,
										MUIA_Text_Contents, "0000",
										MUIA_Text_SetMax, TRUE,
										MUIA_Text_SetMin, TRUE,
										End,
									Child, ycursor_text = TextObject,
										MUIA_Font, MUIV_Font_Fixed,
										MUIA_Text_Contents, "0000",
										MUIA_Text_SetMax, TRUE,
										MUIA_Text_SetMin, TRUE,
										End,
									End,
								End,
							Child, HGroup,
								MUIA_Group_Spacing, 0,
								Child, text_texteditor = (Object*)ComposeEditorObject,
									InputListFrame,
									MUIA_CycleChain, 1,
									MUIA_TextEditor_Slider, slider,
									MUIA_TextEditor_FixedFont, TRUE,
									MUIA_TextEditor_WrapBorder, user.config.write_wrap_type == 1 ? user.config.write_wrap : 0,
									End,
								Child, slider,
								End,
							End,
						Child, VGroup,
							Child, datatype_datatypes = DataTypesObject, TextFrame, End,
							End,
						End,
					End,

				/* New register page */
				Child, VGroup,
					Child, NListviewObject,
						MUIA_CycleChain, 1,
						MUIA_NListview_NList, attach_tree = AttachmentListObject,
							End,
						End,
					Child, HGroup,
						Child, MakeLabel(_("Description")),
						Child, attach_desc_string = BetterStringObject,
							StringFrame,
							MUIA_ControlChar, GetControlChar(_("Description")),
							End,
						End,
					Child, HGroup,
						Child, add_text_button = MakeButton(_("Add text")),
						Child, add_multipart_button = MakeButton(_("Add multipart")),
						Child, add_files_button = MakeButton(_("Add file(s)")),
						Child, remove_button = MakeButton(_("Remove")),
						End,
					End,
				End,
			Child, HGroup,
				Child, send_now_button = MakeButton(_("_Send now")),
				Child, send_later_button = MakeButton(_("Send _later")),
				Child, hold_button = MakeButton(_("_Hold")),
				Child, cancel_button = MakeButton(_("_Cancel")),
				End,
			End,
		End;
	
	if (wnd)
	{
		struct Compose_Data *data = (struct Compose_Data*)malloc(sizeof(struct Compose_Data));
		if (data)
		{
			char buf[512];

			set(sign_button,MUIA_ShowMe, FALSE); /* temporary not shown because not implemented */

			memset(data,0,sizeof(struct Compose_Data));
			data->wnd = wnd;
			data->num = num;
			data->from_text = from_text;
			data->to_string = to_string;
			data->cc_string = cc_string;
			data->reply_string = reply_string;
			data->subject_string = subject_string;
			data->text_texteditor = text_texteditor;
			data->x_text = xcursor_text;
			data->y_text = ycursor_text;
			data->attach_tree = attach_tree;
			data->attach_desc_string = attach_desc_string;
			data->quick_attach_tree = quick_attach_tree;
			data->contents_page = contents_page;
			data->datatype_datatypes = datatype_datatypes;
			data->encrypt_button = encrypt_button;
			data->sign_button = sign_button;
			data->copy_button = copy_button;
			data->cut_button = cut_button;
			data->paste_button = paste_button;
			data->undo_button = undo_button;
			data->redo_button = redo_button;

			init_hook(&data->from_objstr_hook, (HOOKFUNC)from_objstr);
			init_hook(&data->from_strobj_hook, (HOOKFUNC)from_strobj);

			SetAttrs(from_popobject,
					MUIA_Popobject_ObjStrHook, &data->from_objstr_hook,
					MUIA_Popobject_StrObjHook, &data->from_strobj_hook,
					TAG_DONE);

			set(encrypt_button, MUIA_InputMode, MUIV_InputMode_Toggle);
			set(sign_button, MUIA_InputMode, MUIV_InputMode_Toggle);
			set(from_text, MUIA_UserData, reply_string);

			data->file_req = MUI_AllocAslRequestTags(ASL_FileRequest, TAG_DONE);

			/* mark the window as opened */
			compose_open[num] = data;

			/* Insert all from addresss */
			{
				struct account *account = (struct account*)list_first(&user.config.account_list);
				int first = 1;
				while ((account))
				{
					if (account->smtp->name && *account->smtp->name && account->email)
					{
						if (account->name)
						{
							if (needs_quotation(account->name))
								sprintf(buf, "\"%s\"",account->name);
							else strcpy(buf,account->name);
						}

						sprintf(buf+strlen(buf)," <%s> (%s)",account->email, account->smtp->name);
						DoMethod(from_list,MUIM_NList_InsertSingle,buf,MUIV_NList_Insert_Bottom);
						if (first)
						{
							set(from_text, MUIA_Text_Contents, buf);
							set(reply_string, MUIA_String_Contents, account->reply);
							first = 0;
						}
					}
					account = (struct account*)node_next(&account->node);
				}
			}

			DoMethod(wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, App, 7, MUIM_Application_PushMethod, App, 4, MUIM_CallHook, &hook_standard, compose_window_dispose, data);
			DoMethod(expand_to_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_expand_to, data);
			DoMethod(expand_cc_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_expand_cc, data);
			DoMethod(to_string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, App, 4, MUIM_CallHook, &hook_standard, compose_expand_to, data);
			DoMethod(cc_string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, App, 4, MUIM_CallHook, &hook_standard, compose_expand_cc, data);
			DoMethod(text_texteditor, MUIM_Notify, MUIA_TextEditor_CursorX, MUIV_EveryTime, xcursor_text, 4, MUIM_SetAsString, MUIA_Text_Contents, "%04ld", MUIV_TriggerValue);
			DoMethod(text_texteditor, MUIM_Notify, MUIA_TextEditor_CursorY, MUIV_EveryTime, ycursor_text, 4, MUIM_SetAsString, MUIA_Text_Contents, "%04ld", MUIV_TriggerValue);
			DoMethod(add_text_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_add_text, data);
			DoMethod(add_multipart_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_add_multipart, data);
			DoMethod(add_files_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_add_files, data);
			DoMethod(add_attach_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_add_files, data);
			DoMethod(remove_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_remove_file, data);
			DoMethod(attach_tree, MUIM_Notify, MUIA_NListtree_Active, MUIV_EveryTime, attach_tree, 4, MUIM_CallHook, &hook_standard, compose_attach_active, data);
			DoMethod(attach_desc_string, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, App, 4, MUIM_CallHook, &hook_standard, compose_attach_desc, data);
			DoMethod(quick_attach_tree, MUIM_Notify, MUIA_NListtree_Active, MUIV_EveryTime, quick_attach_tree, 4, MUIM_CallHook, &hook_standard, compose_quick_attach_active, data);
			DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 7, MUIM_Application_PushMethod, App, 4, MUIM_CallHook, &hook_standard, compose_window_dispose, data);
			DoMethod(hold_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 7, MUIM_Application_PushMethod, App, 4, MUIM_CallHook, &hook_standard, compose_window_hold, data);
			DoMethod(send_now_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 7, MUIM_Application_PushMethod, App, 4, MUIM_CallHook, &hook_standard, compose_window_send_now, data);
			DoMethod(send_later_button, MUIM_Notify, MUIA_Pressed, FALSE, App, 4, MUIM_CallHook, &hook_standard, compose_window_send_later, data);
			DoMethod(copy_button,MUIM_Notify, MUIA_Pressed, FALSE, text_texteditor, 2, MUIM_TextEditor_ARexxCmd,"Copy");
			DoMethod(cut_button,MUIM_Notify, MUIA_Pressed, FALSE, text_texteditor, 2, MUIM_TextEditor_ARexxCmd,"Cut");
			DoMethod(paste_button,MUIM_Notify, MUIA_Pressed, FALSE, text_texteditor, 2, MUIM_TextEditor_ARexxCmd,"Paste");
			DoMethod(undo_button,MUIM_Notify, MUIA_Pressed, FALSE, text_texteditor, 2, MUIM_TextEditor_ARexxCmd,"Undo");
			DoMethod(redo_button,MUIM_Notify, MUIA_Pressed, FALSE, text_texteditor, 2 ,MUIM_TextEditor_ARexxCmd,"Redo");
			DoMethod(subject_string,MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, wnd, 3, MUIM_Set, MUIA_Window_ActiveObject, text_texteditor);
			DoMethod(wnd, MUIM_Notify, MUIA_Window_ActiveObject, MUIV_EveryTime, App, 5, MUIM_CallHook, &hook_standard, compose_new_active, data, MUIV_TriggerValue);
			DoMethod(from_list, MUIM_Notify, MUIA_NList_DoubleClick, TRUE, from_popobject, 2, MUIM_Popstring_Close, 1);
			DoMethod(signatures_cycle, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, signatures_cycle, 5, MUIM_CallHook, &hook_standard, compose_set_signature, data, MUIV_TriggerValue);

			DoMethod(App,OM_ADDMEMBER,wnd);

			if (args->to_change)
			{
				/* A mail should be changed */
				int entries;
				char *from, *to, *cc;

				/* Find and set the correct account */
				if ((from = mail_find_header_contents(args->to_change, "from")))
				{
					struct account *ac = account_find_by_from(from);
					if (ac)
					{
						if (ac->smtp && ac->smtp->name && *ac->smtp->name && ac->email)
						{
							if (ac->name)
							{
								if (needs_quotation(ac->name))
									sprintf(buf, "\"%s\"",ac->name);
								else strcpy(buf,ac->name);
							}

							sprintf(buf+strlen(buf)," <%s> (%s)",ac->email, ac->smtp->name);
							set(from_text, MUIA_Text_Contents, buf);
							set(reply_string, MUIA_String_Contents, ac->reply);
						}
					}
				}

				compose_add_mail(data,args->to_change,NULL);

				entries = xget(attach_tree,MUIA_NList_Entries);
				if (entries==0)
				{
					compose_add_text(&data);
				} else
				{
					/* Active the first entry if there is only one entry */
					if (entries==1) set(attach_tree,MUIA_NList_Active,0);
					else set(attach_tree,MUIA_NList_Active,1);
				}

				if ((to = mail_find_header_contents(args->to_change,"to")))
				{
					/* set the To string */
					char *decoded_to;
					parse_text_string(to,&decoded_to);
					set(to_string,MUIA_UTF8String_Contents,decoded_to);
					free(decoded_to);
				}

				if ((cc = mail_find_header_contents(args->to_change,"cc")))
				{
					/* set the To string */
					char *decoded_cc;
					parse_text_string(cc,&decoded_cc);
					set(cc_string,MUIA_UTF8String_Contents,decoded_cc);
					free(decoded_cc);
				}

				set(subject_string,MUIA_UTF8String_Contents,args->to_change->subject);

				if (args->action == COMPOSE_ACTION_REPLY)
					set(wnd,MUIA_Window_ActiveObject, data->text_texteditor);
				else
				{
					if (mystrlen((char*)xget(to_string,MUIA_String_Contents)))
					{
						set(wnd,MUIA_Window_ActiveObject, data->subject_string);
					}
					else set(wnd,MUIA_Window_ActiveObject, data->to_string);
				}

				data->filename = mystrdup(args->to_change->filename);
				data->folder = mystrdup("Outgoing");
				data->reply_id = mystrdup(args->to_change->message_reply_id);
			} else
			{
				compose_add_text(&data);
				/* activate the "To" field */
				set(wnd,MUIA_Window_ActiveObject,data->to_string);
			}

			compose_add_signature(data);
			data->sign_array = sign_array;

			data->compose_action = args->action;
			data->ref_mail = args->ref_mail;

			set(wnd,MUIA_Window_Open,TRUE);

			return num;
		}
		MUI_DisposeObject(wnd);
	}
	return -1;
}

/******************************************************************
 Activate a read window
*******************************************************************/
void compose_window_activate(int num)
{
	if (num < 0 || num >= MAX_COMPOSE_OPEN) return;
	if (compose_open[num] && compose_open[num]->wnd) set(compose_open[num]->wnd,MUIA_Window_Open,TRUE);
}

/******************************************************************
 Closes a read window
*******************************************************************/
void compose_window_close(int num, int action)
{
	if (num < 0 || num >= MAX_COMPOSE_OPEN) return;
	if (compose_open[num] && compose_open[num]->wnd)
	{
		switch (action)
		{
			case COMPOSE_CLOSE_CANCEL: compose_window_dispose(&compose_open[num]); break;
			case COMPOSE_CLOSE_SEND:  compose_mail(compose_open[num],2);
			case COMPOSE_CLOSE_LATER: compose_mail(compose_open[num],0);
			case COMPOSE_CLOSE_HOLD: compose_mail(compose_open[num],1);
		}
	}
}

