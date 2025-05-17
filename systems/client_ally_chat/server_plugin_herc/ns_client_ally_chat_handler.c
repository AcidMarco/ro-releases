//===== Hercules Plugin ======================================
//= Client Ally Chat Handler
//===== By: =================================================
//= AcidMarco
//===== Description: =========================================
//= This plugin handles guild alliance chat messages and
//= broadcasts them to all allied guild members.
//===== Note 1: ==============================================
//= Supports packet format CZ/ZC_ALLY_CHAT introduced in
//= client versions around 2023-06-07.
//= Tested on client version 2025-03-19.
//===== Note 2: ==============================================
//= By default, the client sends alliance messages using the
//= '#' prefix in public chat. This conflicts with charcommands.
//= Consider one of the following:
//= 1. Change the '#' symbol in the client to something else
//= 2. Disable symbol-based ally chat access entirely
//= Patches for this are available on GitHub (link below).
//===== Setup: ===============================================
//= 1. Set PACKETVER >= 20230607 in \src\common\mmo.h
//= 2. Use a compatible client supporting the feature
//= 3. Optionally patch the client symbol behavior if needed
//===== Repository Link: =====================================
//= https://github.com/AcidMarco/ro-releases
//============================================================

#include "common/hercules.h"
#include "common/random.h"
#include "common/memmgr.h"
#include "common/socket.h"
#include "common/nullpo.h"
#include "common/packets.h"

#include "map/clif.h"
#include "map/guild.h"
#include "map/pc.h"
#include "map/packets.h"

#include "plugins/HPMHooking.h"
#include "common/HPMDataCheck.h"

HPExport struct hplugin_info pinfo = {
	"ns_client_ally_chat_handler",
	SERVER_TYPE_MAP,
	"1.0",
	HPM_VERSION,
};

// Packet headers for alliance chat
enum ally_chat_packet_headers {
	HEADER_ZC_ALLY_CHAT = 0x0bde,
	HEADER_CZ_ALLY_CHAT = 0x0bdd,
};

#pragma pack(push, 1)
// Structure of the client-to-server alliance chat message
// 0x0BDD <packet len>.W <message>.?B
struct PACKET_CZ_ALLY_CHAT {
	int16 packetType;
	int16 packetLength;
	char message[];
} __attribute__((packed));

// Structure of the server-to-client alliance chat message
// 0x0BDE <packet len>.W <message>.?B
struct PACKET_ZC_ALLY_CHAT {
	int16 packetType;
	int16 packetLength;
	char message[];
} __attribute__((packed));
#pragma pack(pop)

// Sends the chat message to all guild and allied members
static void clif_send_guild_alliance_message(struct guild* g, const char* mes, int len)
{
	struct map_session_data* sd = guild->getavailablesd(g);
	size_t max_len = CHAT_SIZE_MAX - sizeof(struct PACKET_ZC_ALLY_CHAT) - 1;

	if (!sd || len <= 0)
		return;

	if ((size_t)len > max_len) {
		ShowWarning("clif_send_guild_alliance_message: Truncated message '%s' (len=%d, max=%zu, guild_id=%u).\n",
			mes, len, max_len, (unsigned int)g->guild_id);
		len = (int)max_len;
	}

	size_t packet_len = sizeof(struct PACKET_ZC_ALLY_CHAT) + len + 1;
	struct PACKET_ZC_ALLY_CHAT* p = (struct PACKET_ZC_ALLY_CHAT*)aMalloc(packet_len);
	if (!p)
		return;

	p->packetType = HEADER_ZC_ALLY_CHAT;
	p->packetLength = (uint16)packet_len;
	memcpy(p->message, mes, len);
	p->message[len] = '\0';

	clif->send(p, p->packetLength, &sd->bl, GUILD);

	for (int i = 0; i < MAX_GUILDALLIANCE; i++) {
		if (g->alliance[i].guild_id && g->alliance[i].opposition == 0) {
			struct guild* ag = guild->search(g->alliance[i].guild_id);
			struct map_session_data* allysd;

			if (!ag)
				continue;

			allysd = guild->getavailablesd(ag);
			if (!allysd)
				continue;

			clif->send(p, p->packetLength, &allysd->bl, GUILD);
		}
	}

	aFree(p);
}

// Handles incoming client packet for alliance chat
static void clif_parse_guild_alliance_message(int fd)
{
	struct map_session_data* sd = sockt->session[fd]->session_data;
	if (!sd)
		return;

	char output[CHAT_SIZE_MAX + NAME_LENGTH * 2];
	const struct packet_chat_message* packet = RP2PTR(fd);

	if (!clif->process_chat_message(sd, packet, output, sizeof output))
		return;

	if (sd->status.guild_id == 0)
		return;

	struct guild* g = guild->search(sd->status.guild_id);
	if (!g)
		return;

	clif_send_guild_alliance_message(g, output, (int)strlen(output));
}

#if PACKETVER >= 20230607
HPExport void plugin_init(void)
{
	addPacket(HEADER_CZ_ALLY_CHAT, -1, clif_parse_guild_alliance_message, hpClif_Parse);
	packets->addLen(HEADER_CZ_ALLY_CHAT, -1);
	packets->addLen(HEADER_ZC_ALLY_CHAT, -1);
}
#else
HPExport void plugin_init(void)
{
	ShowWarning("ns_client_ally_chat_handler: This plugin requires PACKETVER >= 20230607. Plugin will not be loaded.\n");
}
#endif