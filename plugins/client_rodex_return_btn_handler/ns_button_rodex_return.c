//===== Hercules Plugin ======================================
//= Client Rodex Return Button Handler
//===== By: =================================================
//= AcidMarco
//===== Description: =========================================
//= Enables the "Return" button in the Rodex mail UI.
//= Allows recipients to return a mail back to the sender
//= by clicking the button in the client interface.
//===== Note =================================================
//= The "Return" button is available in client versions
//= from approximately 2022-03-30 and later.
//= Successfully tested on 2022-04-06 and 2025-03-19 clients.
//===== Repository Link: =====================================
//= https://github.com/AcidMarco/ro-releases
//============================================================

#include "common/hercules.h"
#include "common/random.h"
#include "common/memmgr.h"
#include "common/socket.h"
#include "common/packets.h"
#include "common/nullpo.h"
#include "common/sql.h"

#include "map/clif.h"
#include "map/pc.h"
#include "map/rodex.h"
#include "map/intif.h"
#include "map/packets.h"

#include "plugins/HPMHooking.h"
#include "common/HPMDataCheck.h"

HPExport struct hplugin_info pinfo = {
	"ns_button_rodex_return",
	SERVER_TYPE_MAP,
	"1.0",
	HPM_VERSION,
};

//=========================================================================
//===== Config: Client Rodex Return Button Handler ========================
//=========================================================================
char rodex_db[64] = "rodex_mail";	// Name of the SQL table used for Rodex mail data
char char_db[64] = "char";			// Name of the SQL table containing character data (used for mail auto-deletion)
bool is_auto_del_mail = true;		// Automatically delete returned mail if the sender character no longer exists

enum rodex_return_status {
	RODEX_RETURN_STATUS_SUCCESS = 0,
	RODEX_RETURN_STATUS_FAILED = 1,
};

enum rodex_return_headers {
	HEADER_CZ_RODEX_RETURN = 0x0B98,
	HEADER_ZC_RODEX_RETURN_RESULT = 0x0B99,
};

#pragma pack(push, 1)
struct PACKET_CZ_RODEX_RETURN {
	int16 packetType;
	uint32 msgId;
} __attribute__((packed));

struct PACKET_ZC_RODEX_RETURN_RESULT {
	int16 packetType;
	uint32  msgId;
	uint32  status;
} __attribute__((packed));
#pragma pack(pop)

// Sends result packet to client confirming the return status of a Rodex mail
void clif_mail_return_result(struct map_session_data* sd, uint32 mail_id, enum rodex_return_status result)
{
	nullpo_retv(sd);
	struct PACKET_ZC_RODEX_RETURN_RESULT p;
	p.packetType = HEADER_ZC_RODEX_RETURN_RESULT;
	p.msgId = mail_id;
	p.status = (uint32)result;
	clif->send(&p, sizeof(p), &sd->bl, SELF);
}

// Handles the "Return" button pressed in Rodex UI, updates the mail, 
// notifies the sender, or auto-deletes if the sender no longer exists
void clif_parse_mail_return_btn(int fd)
{
	struct map_session_data* sd = sockt->session[fd]->session_data;
	if (!sd || sd->state.trading || pc_isdead(sd) || pc_isvending(sd))
		return;

	const struct PACKET_CZ_RODEX_RETURN* p = (struct PACKET_CZ_RODEX_RETURN*)RFIFOP(fd, 0);
	uint32 mail_id = p->msgId;

	if (mail_id == 0) {
		clif_mail_return_result(sd, mail_id, RODEX_RETURN_STATUS_FAILED);
		return;
	}

	struct rodex_message* msg = rodex->get_mail(sd, mail_id);
	if (!msg) {
		clif_mail_return_result(sd, mail_id, RODEX_RETURN_STATUS_FAILED);
		return;
	}

	if (SQL_ERROR == SQL->Query(map->mysql_handle, "UPDATE `%s` SET `expire_date` = `send_date`, `is_read` = 0 WHERE `mail_id` = '%u' AND `receiver_id` = '%d'", rodex_db, mail_id, sd->status.char_id)) {
		Sql_ShowDebug(map->mysql_handle);
	}

	clif_mail_return_result(sd, mail_id, RODEX_RETURN_STATUS_SUCCESS);

	if (msg->sender_id > 0) {
		struct map_session_data* snd_sd = map->charid2sd(msg->sender_id);
		if (snd_sd && snd_sd->fd > 0) {
			rodex->refresh(snd_sd, RODEX_OPENTYPE_RETURN, 0);
			rodex->refresh(snd_sd, RODEX_OPENTYPE_UNSET, 0);
			clif->rodex_icon(snd_sd->fd, true);
			clif_disp_onlyself(snd_sd, "You've got a returned mail!");
		}
		else if (is_auto_del_mail) {
			if (SQL_ERROR == SQL->Query(map->mysql_handle, "SELECT `char_id` FROM `%s` WHERE `char_id` = '%d' LIMIT 1", char_db, msg->sender_id)) {
				Sql_ShowDebug(map->mysql_handle);
			} else {
				if (SQL_SUCCESS != SQL->NextRow(map->mysql_handle))
					intif->rodex_updatemail(sd, mail_id, 0, 3);
				SQL->FreeResult(map->mysql_handle);
			}
		}
	}

	rodex->refresh(sd, RODEX_OPENTYPE_UNSET, 0);
}

#if PACKETVER >= 20220330
HPExport void plugin_init(void)
{
	addPacket(HEADER_CZ_RODEX_RETURN, sizeof(struct PACKET_CZ_RODEX_RETURN), clif_parse_mail_return_btn, hpClif_Parse);
	packets->addLen(HEADER_ZC_RODEX_RETURN_RESULT, sizeof(struct PACKET_ZC_RODEX_RETURN_RESULT));
}
#else
HPExport void plugin_init(void)
{
	ShowWarning("ns_button_rodex_return: This plugin requires PACKETVER >= 20220330. Plugin will not be loaded.\n");
}
#endif