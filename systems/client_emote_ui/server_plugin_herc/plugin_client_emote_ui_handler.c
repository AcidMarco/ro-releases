//===== Hercules Plugin ======================================
//= Client New Emotion System and UI Handler
//===== By: =================================================
//= AcidMarco
//===== Description: =========================================
//= This plugin provides the following features:
//= 1. Handling of new emotion playback packets
//= 2. Management and parsing of the emotion pack database
//= 3. Functions for purchasing emotion packs in-game
//===== Note: ================================================
//= This plugin is designed to support the new emotion system
//= introduced in client versions 2023-08-02 and above.
//= Tested on client version 2025-03-05.
//===== Setup: ===============================================
//= 1. To use this plugin, set PACKETVER >= 20230802 in \src\common\mmo.h.
//=    You must also use a compatible client that supports this feature.
//= 2. Move the emotion_pack_db.conf file into your database folder: \db\
//= 3. You can customize UI_CURRENCY_ID and disable debug output via SHOW_DEBUG_MES.
//=    To change the UI_CURRENCY_ID on client side, you must patch the client using a HEX patch.
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
#include "map/script.h"
#include "map/pc.h"
#include "map/packets.h"

#include "plugins/HPMHooking.h"
#include "common/HPMDataCheck.h"

HPExport struct hplugin_info pinfo = {
	"ns_client_emote_ui_handler",
	SERVER_TYPE_MAP,
	"1.0",
	HPM_VERSION,
};

//===== Global Config =====
int16 UI_CURRENCY_ID = 6909;	// Default value (6909) hard-coded in the client. Use the NewstyleChangeEmoteCurrencyItemID hex patch to change it.
bool SHOW_DEBUG_MES = true;		// Console debug messages for emotion system (useful for development and testing)
#define MAX_EMOTE_PACKS 100		// Maximum number of emotes allowed in a single emote pack

//===== Packet header definitions for custom emotion system =====
// Used for parsing client requests and sending responses for emotion pack interactions.
enum emotion_packet_headers {
	HEADER_CZ_REQ_EMOTION2 = 0x0be9,
	HEADER_ZC_EMOTION_SUCCESS = 0x0bea,
	HEADER_ZC_EMOTION_FAIL = 0x0beb,
	HEADER_CZ_EMOTION_EXPANSION_REQ = 0x0bec,
	HEADER_ZC_EMOTION_EXPANSION_SUCCESS = 0x0bed,
	HEADER_ZC_EMOTION_EXPANSION_FAIL = 0x0bee,
#if PACKETVER >= 20230920
	HEADER_ZC_EMOTION_EXPANSION_LIST = 0x0bf6,
#else
	HEADER_ZC_EMOTION_EXPANSION_LIST = 0x0bef,
#endif
};

#pragma pack(push, 1)
struct PACKET_CZ_REQ_EMOTION2 {
	uint16 packetType;
	uint16 packId;
	uint16 emoteId;
} __attribute__((packed));

struct PACKET_ZC_EMOTION_SUCCESS {
	uint16 packetType;
	uint32 GID;
	uint16 packId;
	uint16 emoteId;
} __attribute__((packed));

struct PACKET_ZC_EMOTION_FAIL {
	uint16 packetType;
	uint16 packId;
	uint16 emoteId;
	uint8 status;
} __attribute__((packed));

struct PACKET_CZ_EMOTION_EXPANSION_REQ {
	uint16 packetType;
	uint16 packId;
	uint16 itemId;
	uint8 amount;
} __attribute__((packed));

struct PACKET_ZC_EMOTION_EXPANSION_SUCCESS {
	uint16 packetType;
	uint16 packId;
	uint8 isRented;
	uint32 timestamp;
} __attribute__((packed));

struct PACKET_ZC_EMOTION_EXPANSION_FAIL {
	uint16 packetType;
	uint16 packId;
	uint8 status;
} __attribute__((packed));

struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub {
	uint16 packId;
	uint8 isRented;
	uint32 timestamp;
} __attribute__((packed));

#if PACKETVER >= 20230920
struct PACKET_ZC_EMOTION_EXPANSION_LIST {
	int16 packetType;
	int16 packetLength;
	uint32 timestamp;
	int16 timezone;
	struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub list[];
} __attribute__((packed));
#else
struct PACKET_ZC_EMOTION_EXPANSION_LIST {
	int16 packetType;
	int16 packetLength;
	uint32 timestamp;
	struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub list[];
} __attribute__((packed));
#endif
#pragma pack(pop)

//===== Client emotion types (client_emotion_type) =====
// Enumeration of all supported client emotion constants.
// Used for sending emotion playback results to the client.
enum emotion_expansion_msg {
	EMSG_EMOTION_EXPANSION_NOT_ENOUGH_NYANGVINE = 0,
	EMSG_EMOTION_EXPANSION_FAIL_DATE = 1,
	EMSG_EMOTION_EXPANSION_FAIL_ALREADY_BUY = 2,
	EMSG_EMOTION_EXPANSION_FAIL_ANOTHER_SALE_BUY = 3,
	EMSG_EMOTION_EXPANSION_NOT_ENOUGH_BASICSKILL_LEVEL = 4,
	EMSG_NOT_YET_SALE_START_TIME = 5,
	EMSG_EMOTION_EXPANSION_FAIL_UNKNOWN = 6,
};

enum emote_msg {
	EMSG_EMOTION_EXPANSION_USE_FAIL_DATE = 0,
	EMSG_EMOTION_EXPANSION_USE_FAIL_UNPURCHASED = 1,
	EMSG_EMOTION_USE_FAIL_SKILL_LEVEL = 2,
	EMSG_EMOTION_EXPANSION_USE_FAIL_UNKNOWN = 3,
};

typedef enum client_emotion_type {
	ET_BLANK = -1,
	ET_SURPRISE = 0,
	ET_QUESTION,
	ET_DELIGHT,
	ET_THROB,
	ET_SWEAT,
	ET_AHA,
	ET_FRET,
	ET_ANGER,
	ET_MONEY,
	ET_THINK,
	ET_SCISSOR,
	ET_ROCK,
	ET_WRAP,
	ET_FLAG,
	ET_BIGTHROB,
	ET_THANKS,
	ET_KEK,
	ET_SORRY,
	ET_SMILE,
	ET_PROFUSELY_SWEAT,
	ET_SCRATCH,
	ET_BEST,
	ET_STARE_ABOUT,
	ET_HUK,
	ET_O,
	ET_X,
	ET_HELP,
	ET_GO,
	ET_CRY,
	ET_KIK,
	ET_CHUP,
	ET_CHUPCHUP,
	ET_HNG,
	ET_OK,
	ET_CHAT_PROHIBIT,
	ET_INDONESIA_FLAG,
	ET_STARE,
	ET_HUNGRY,
	ET_COOL,
	ET_MERONG,
	ET_SHY,
	ET_GOODBOY,
	ET_SPTIME,
	ET_SEXY,
	ET_COMEON,
	ET_SLEEPY,
	ET_CONGRATULATION,
	ET_HPTIME,
	ET_PH_FLAG,
	ET_MY_FLAG,
	ET_SI_FLAG,
	ET_BR_FLAG,
	ET_SPARK,
	ET_CONFUSE,
	ET_OHNO,
	ET_HUM,
	ET_BLABLA,
	ET_OTL,
	ET_DICE1,
	ET_DICE2,
	ET_DICE3,
	ET_DICE4,
	ET_DICE5,
	ET_DICE6,
	ET_INDIA_FLAG,
	ET_LUV,
	ET_FLAG8,
	ET_FLAG9,
	ET_MOBILE,
	ET_MAIL,
	ET_ANTENNA0,
	ET_ANTENNA1,
	ET_ANTENNA2,
	ET_ANTENNA3,
	ET_HUM2,
	ET_ABS,
	ET_OOPS,
	ET_SPIT,
	ET_ENE,
	ET_PANIC,
	ET_WHISP,
	ET_YUT1,
	ET_YUT2,
	ET_YUT3,
	ET_YUT4,
	ET_YUT5,
	ET_YUT6,
	ET_YUT7,
	ET_CLICK_ME,
	ET_DAILY_QUEST,
	ET_EVENT,
	ET_JOB_QUEST,
	ET_TRAFFIC_LINE_QUEST,
	ET_CUSTOM_1,
	ET_CUSTOM_2,
	ET_CUSTOM_3,
	ET_CUSTOM_4,
	ET_CUSTOM_5,
	ET_CUSTOM_6,
	ET_CUSTOM_7,
	ET_CUSTOM_8,
	ET_CUSTOM_9,
	ET_CUSTOM_10,
	ET_CUSTOM_11,
	ET_CUSTOM_12,
	ET_CUSTOM_13,
	ET_CUSTOM_14,
	ET_CUSTOM_15,
	ET_EMOTION_LAST
} client_emotion_type;

//===== Emotion Pack Database =====
// Stores all emotion pack metadata such as ID, price, availability,
// rental duration, and emote list. Loaded from emotion_pack_db.conf.
struct s_emotion_db {
	uint16 packId;
	uint16 packType;
	uint16 packPrice;
	time_t sale_start;
	time_t sale_end;
	uint64 rental_period;
	uint16 emote_count;
	client_emotion_type emoteIds[MAX_EMOTE_PACKS];
};

struct DBMap* emotion_db = NULL;

time_t plugin_convert_to_unix_timestamp(uint64_t date_val)
{
	struct tm tmStruct = { 0 };
	tmStruct.tm_year = (int)(date_val / 10000) - 1900;
	tmStruct.tm_mon = (int)((date_val / 100) % 100) - 1;
	tmStruct.tm_mday = (int)(date_val % 100);
	return mktime(&tmStruct);
}

void emote_db_final(void)
{
	if (emotion_db) {
		db_destroy(emotion_db);
		emotion_db = NULL;
	}
}

bool emote_db_init(void)
{
	const char* filename = "emotion_pack_db.conf";
	char filepath[256];
	libconfig->format_db_path(filename, filepath, sizeof(filepath));

	struct config_t conf;
	if (libconfig->load_file(&conf, filepath) == 0)
		return false;

	struct config_setting_t* root = libconfig->setting_get_member(conf.root, "emotion_pack_db");
	if (!root || !config_setting_is_list(root)) {
		ShowError("emote_db_init: Setting 'emotion_pack_db' not found or not a list.\n");
		libconfig->destroy(&conf);
		return false;
	}

	if (!emotion_db)
		emotion_db = idb_alloc(DB_OPT_RELEASE_DATA);

	int total_packs = 0;
	int total_emotes = 0;

	for (int i = 0; i < libconfig->setting_length(root); ++i) {
		struct config_setting_t* entry = libconfig->setting_get_elem(root, i);
		if (!entry)
			continue;

		int temp_packId, temp_packType, temp_packPrice;
		int64_t temp_saleStart, temp_saleEnd, temp_rentalPeriod;

		if (!libconfig->setting_lookup_int(entry, "PackId", &temp_packId)) continue;
		if (!libconfig->setting_lookup_int(entry, "PackType", &temp_packType)) continue;
		if (!libconfig->setting_lookup_int(entry, "PackPrice", &temp_packPrice)) continue;
		if (!libconfig->setting_lookup_int64(entry, "SaleStart", &temp_saleStart)) continue;
		if (!libconfig->setting_lookup_int64(entry, "SaleEnd", &temp_saleEnd)) continue;
		if (!libconfig->setting_lookup_int64(entry, "RentalPeriod", &temp_rentalPeriod)) continue;

		struct s_emotion_db* ce = (struct s_emotion_db*)aMalloc(sizeof(struct s_emotion_db));
		memset(ce, 0, sizeof(struct s_emotion_db));

		ce->packId = (uint16)temp_packId;
		ce->packType = (uint16)temp_packType;
		ce->packPrice = (uint16)temp_packPrice;
		ce->sale_start = temp_saleStart ? plugin_convert_to_unix_timestamp((uint64_t)temp_saleStart) : 0;
		ce->sale_end = temp_saleEnd ? plugin_convert_to_unix_timestamp((uint64_t)temp_saleEnd) : 0;
		ce->rental_period = (uint64_t)temp_rentalPeriod * 60 * 60 * 24;

		struct config_setting_t* emotes = libconfig->setting_get_member(entry, "EmotesList");
		if (emotes && config_setting_is_array(emotes)) {
			int count = libconfig->setting_length(emotes);
			for (int j = 0; j < count && ce->emote_count < MAX_EMOTE_PACKS; ++j) {
				const char* ename = libconfig->setting_get_string_elem(emotes, j);
				if (!ename) continue;

				int val;
				if (script->get_constant(ename, &val)) {
					if (val >= 0 && val < ET_EMOTION_LAST) {
						ce->emoteIds[ce->emote_count++] = (client_emotion_type)val;
					}
					else {
						ShowWarning("emote_db_init: Invalid emotion constant (out of range): %s\n", ename);
					}
				}
				else {
					ShowWarning("emote_db_init: Unknown emotion constant: %s\n", ename);
				}
			}
		}

		idb_put(emotion_db, temp_packId, ce);
		total_packs++;
		total_emotes += ce->emote_count;
	}

	libconfig->destroy(&conf);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' packs and '"CL_WHITE"%d"CL_RESET"' total emotes in '"CL_WHITE"%s"CL_RESET"'.\n",
		total_packs, total_emotes, filepath);

	return true;
}

//===== Handling emotion pack purchases =====
// Validates purchase requests: checks item existence, ownership type,
// rental validity, and writes result status to the client.
void clif_send_emote_expansion_success(struct map_session_data* sd, int16 packId, int8 isRented, uint32 RentEndTime)
{
	if (!sd) return;

	struct PACKET_ZC_EMOTION_EXPANSION_SUCCESS p;
	memset(&p, 0, sizeof(p));
	p.packetType = HEADER_ZC_EMOTION_EXPANSION_SUCCESS;
	p.packId = packId;
	p.isRented = isRented;
	p.timestamp = RentEndTime;
	clif->send(&p, sizeof(p), &sd->bl, SELF);
}

void clif_send_emote_expansion_fail(struct map_session_data* sd, int16 packId, enum emotion_expansion_msg emote_status)
{
	if (!sd) return;

	struct PACKET_ZC_EMOTION_EXPANSION_FAIL p;
	memset(&p, 0, sizeof(p));
	p.packetType = HEADER_ZC_EMOTION_EXPANSION_FAIL;
	p.packId = packId;
	p.status = emote_status;
	clif->send(&p, sizeof(p), &sd->bl, SELF);
}

void emote_expansion_purchase(struct map_session_data* sd, int16 packId, int16 itemId, int8 amount)
{
	if (!sd) return;

	if (itemId != UI_CURRENCY_ID) {
		clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_FAIL_UNKNOWN);
		return;
	}

	if (battle->bc->basic_skill_check != 0 && pc->checkskill(sd, NV_BASIC) < 2 && pc->checkskill(sd, SU_BASIC_SKILL) < 1)
	{
		clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_NOT_ENOUGH_BASICSKILL_LEVEL);
		return;
	}

	struct s_emotion_db* ce = (struct s_emotion_db*)idb_get(emotion_db, packId);
	if (!ce) {
		clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_FAIL_UNKNOWN);
		return;
	}

	if (amount != ce->packPrice) {
		clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_FAIL_UNKNOWN);
		return;
	}

	time_t now = time(NULL);
	if (ce->sale_start != 0 && ce->sale_start > now) {
		clif_send_emote_expansion_fail(sd, packId, EMSG_NOT_YET_SALE_START_TIME);
		return;
	}

	if (ce->sale_end != 0 && ce->sale_end < now) {
		clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_FAIL_DATE);
		return;
	}

	char var_name[64];
	sprintf(var_name, "%scashemote_%d", ce->packType == 1 ? "#" : "", ce->packId);
	if (pc_readglobalreg(sd, script->add_variable(var_name)) != 0) {
		clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_FAIL_ALREADY_BUY);
		return;
	}

	if (amount > 0) {
		int idx = pc->search_inventory(sd, itemId);
		if (idx == INDEX_NOT_FOUND || sd->status.inventory[idx].amount < amount) {
			clif_send_emote_expansion_fail(sd, packId, EMSG_EMOTION_EXPANSION_NOT_ENOUGH_NYANGVINE);
			return;
		}
		pc->delitem(sd, idx, amount, 0, 0, LOG_TYPE_CONSUME);
	}

	sprintf(var_name, "%scashemote_%d", ce->packType == 1 ? "#" : "", ce->packId);
	pc_setglobalreg(sd, script->add_variable(var_name), 1);

	if (ce->rental_period != 0) {
		char var_expire[64];
		uint64 expire_time = (uint64)(now + ce->rental_period);
		sprintf(var_expire, "%scashemoteexpire_%d", ce->packType == 1 ? "#" : "", ce->packId);
		pc_setglobalreg(sd, script->add_variable(var_expire), (int32)expire_time);
		clif_send_emote_expansion_success(sd, packId, 1, (uint32)expire_time);
		return;
	}

	clif_send_emote_expansion_success(sd, packId, 0, 0);
}

void clif_parse_emote_expansion_request(int fd)
{
	struct map_session_data* sd = sockt->session[fd]->session_data;
	if (!sd) return;

	struct PACKET_CZ_EMOTION_EXPANSION_REQ* p = (struct PACKET_CZ_EMOTION_EXPANSION_REQ*)RFIFOP(fd, 0);

	if (SHOW_DEBUG_MES)
		ShowDebug("clif_parse_emote_expansion_request: AID=%d, packId=%d, itemId=%d, amount=%d\n",
			sd->status.account_id, p->packId, p->itemId, p->amount);

	emote_expansion_purchase(sd, p->packId, p->itemId, p->amount);
}

//===== Sending active emotes to the client =====
// Constructs and sends a list of all active (owned and valid) emotion packs
// upon login or request from the player.
void clif_send_emote_expansion_list(struct map_session_data* sd, struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub* list, uint16 count)
{
	nullpo_retv(sd);

	uint16 packet_len = sizeof(struct PACKET_ZC_EMOTION_EXPANSION_LIST) + count * sizeof(struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub);

	struct PACKET_ZC_EMOTION_EXPANSION_LIST* p = (struct PACKET_ZC_EMOTION_EXPANSION_LIST*)aMalloc(packet_len);
	if (!p)
		return;

	p->packetType = HEADER_ZC_EMOTION_EXPANSION_LIST;
	p->packetLength = packet_len;
	p->timestamp = (uint32)time(NULL);

#if PACKETVER >= 20230920
	p->timezone = 540; // UTC+9
#endif

	if (count > 0 && list != NULL) {
		memcpy(p->list, list, count * sizeof(struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub));
	}

	if (SHOW_DEBUG_MES)
		ShowDebug("clif_send_emote_expansion_list: AID=%d, count=%d, timestamp=%u\n",
			sd->status.account_id, count, p->timestamp);

	clif->send(p, packet_len, &sd->bl, SELF);
	aFree(p);
}

void emote_get_player_packs(struct map_session_data* sd)
{
	if (!sd || sd->fd == 0 || !emotion_db)
		return;

	struct PACKET_ZC_EMOTION_EXPANSION_LIST_sub packs[MAX_EMOTE_PACKS];
	uint16 count = 0;
	time_t now = time(NULL);

	struct DBIterator* iter = db_iterator(emotion_db);
	struct s_emotion_db* entry;

	for (entry = dbi_first(iter); dbi_exists(iter); entry = dbi_next(iter)) {
		char var_name[64], var_name2[64];
		uint64 has_pack = 0, expire_time = 0;

		snprintf(var_name, sizeof(var_name), "%scashemote_%d", entry->packType == 1 ? "#" : "", entry->packId);
		has_pack = pc_readglobalreg(sd, script->add_variable(var_name));

		snprintf(var_name2, sizeof(var_name2), "%scashemoteexpire_%d", entry->packType == 1 ? "#" : "", entry->packId);
		expire_time = pc_readglobalreg(sd, script->add_variable(var_name2));

		if (has_pack != 0 && entry->rental_period != 0 && now > (time_t)expire_time) {
			pc_setglobalreg(sd, script->add_variable(var_name), 0);
			pc_setglobalreg(sd, script->add_variable(var_name2), 0);
			continue;
		}

		if (has_pack != 0 && count < MAX_EMOTE_PACKS) {
			packs[count].packId = entry->packId;
			packs[count].isRented = (uint8)(entry->rental_period != 0);
			packs[count].timestamp = (uint32)expire_time;
			count++;
		}
	}
	dbi_destroy(iter);

	clif_send_emote_expansion_list(sd, packs, count);
}

//===== Handling emotion playback requests =====
// Validates whether the requested emotion exists in the pack, whether the player owns it,
// and whether the pack is active. Sends success or failure packet accordingly.
void clif_send_emote_success(struct block_list* bl, int16 packId, int16 emoteId)
{
	nullpo_retv(bl);

	struct PACKET_ZC_EMOTION_SUCCESS p;
	p.packetType = HEADER_ZC_EMOTION_SUCCESS;
	p.GID = bl->id;
	p.packId = packId;
	p.emoteId = emoteId;

	if (SHOW_DEBUG_MES)
		ShowDebug("clif_send_emote_success: GID=%u, packId=%u, emoteId=%u\n",
			(unsigned int)p.GID, (unsigned int)p.packId, (unsigned int)p.emoteId);

	clif->send(&p, sizeof(p), bl, AREA);
}

void clif_send_emote_fail(struct map_session_data* sd, int16 packId, int16 emoteId, enum emote_msg emote_status)
{
	nullpo_retv(sd);

	struct PACKET_ZC_EMOTION_FAIL p;
	p.packetType = HEADER_ZC_EMOTION_FAIL;
	p.packId = packId;
	p.emoteId = emoteId;
	p.status = (uint8)emote_status;

	ShowDebug("clif_send_emote_fail: AID=%d, packId=%d, emoteId=%d, status=%d\n",
		sd->status.account_id, p.packId, p.emoteId, p.status);

	clif->send(&p, sizeof(p), &sd->bl, SELF);
}

void emote_check_before_use(struct map_session_data* sd, int16 packId, int16 emoteId)
{
	nullpo_retv(sd);

	if (battle->bc->basic_skill_check != 0 && pc->checkskill(sd, NV_BASIC) < 2 && pc->checkskill(sd, SU_BASIC_SKILL) < 1) {
		clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_USE_FAIL_SKILL_LEVEL);
		return;
	}

	if (emoteId == ET_CHAT_PROHIBIT) {
		clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_EXPANSION_USE_FAIL_UNKNOWN);
		return;
	}

	if (sd->emotionlasttime + 1 >= time(NULL)) {
		sd->emotionlasttime = time(NULL);
		clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_EXPANSION_USE_FAIL_UNKNOWN);
		return;
	}
	sd->emotionlasttime = time(NULL);
	pc->update_idle_time(sd, BCIDLE_EMOTION);

	struct s_emotion_db* ce = (struct s_emotion_db*)idb_get(emotion_db, packId);
	if (!ce) {
		clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_EXPANSION_USE_FAIL_UNKNOWN);
		return;
	}

	bool found = false;
	for (int i = 0; i < ce->emote_count; i++) {
		if (ce->emoteIds[i] == emoteId) {
			found = true;
			break;
		}
	}
	if (!found) {
		clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_EXPANSION_USE_FAIL_UNKNOWN);
		return;
	}

	if (ce->packId != 0) {
		char var_name[64];
		snprintf(var_name, sizeof(var_name), "%scashemote_%d", ce->packType == 1 ? "#" : "", ce->packId);
		int64 has_pack = pc_readglobalreg(sd, script->add_variable(var_name));
		if (has_pack == 0) {
			clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_EXPANSION_USE_FAIL_UNPURCHASED);
			return;
		}

		snprintf(var_name, sizeof(var_name), "%scashemoteexpire_%d", ce->packType == 1 ? "#" : "", ce->packId);
		int64 expire_time = pc_readglobalreg(sd, script->add_variable(var_name));
		if (ce->rental_period != 0 && time(NULL) > expire_time) {
			clif_send_emote_fail(sd, packId, emoteId, EMSG_EMOTION_EXPANSION_USE_FAIL_DATE);
			return;
		}
	}
	
	if (battle->bc->client_reshuffle_dice && emoteId >= E_DICE1 && emoteId <= E_DICE6) {
		emoteId = rnd() % 6 + E_DICE1;
	}

	clif_send_emote_success(&sd->bl, packId, emoteId);
}

void clif_parse_emotion2(int fd)
{
	struct map_session_data* sd = sockt->session[fd]->session_data;

	nullpo_retv(sd);
	struct PACKET_CZ_REQ_EMOTION2* p = (struct PACKET_CZ_REQ_EMOTION2*)RFIFOP(fd, 0);

	if (SHOW_DEBUG_MES)
		ShowDebug("clif_parse_emotion2: fd=%d, AID=%d, packId=%d, emoteId=%d\n",
			fd, sd->status.account_id, p->packId, p->emoteId);

	emote_check_before_use(sd, p->packId, p->emoteId);
}

//===== Hook implementations for core functions =====
// - clif_emotion_pre: overrides default emotion behavior to send ZC_EMOTION_SUCCESS.
// - clif_parse_Emotion_pre: prevents default emotion parsing when custom handling is active.
// - clif_parse_LoadEndAck_pre: sends emotion pack list to player on initial login only.
static void clif_emotion_pre(struct block_list** bl, enum emotion_type* type)
{
	client_emotion_type client_type_emote = (client_emotion_type)(*type);

	if (client_type_emote <= ET_BLANK || client_type_emote >= ET_EMOTION_LAST) {
		hookStop();
		return;
	}

	if ((*bl)->type == BL_PC) {
		clif_send_emote_success(*bl, 0, client_type_emote);
		hookStop();
		return;
	}
}

static void clif_parse_Emotion_pre(int* fd, struct map_session_data** sd)
{
	hookStop();
}

static void clif_parse_LoadEndAck_pre(int* fd, struct map_session_data** sd)
{
	if ((*sd)->state.connect_new) {
		emote_get_player_packs(*sd);
	}
}

#if PACKETVER >= 20230802
HPExport void plugin_init(void)
{
	addPacket(0x0be9, 6, clif_parse_emotion2, hpClif_Parse);
	addPacket(0x0bec, 7, clif_parse_emote_expansion_request, hpClif_Parse);
	packets->addLen(0x0bea, 10);
	packets->addLen(0x0beb, 7);
	packets->addLen(0x0bed, 9);
	packets->addLen(0x0bee, 5);
	packets->addLen(0x0bf6, -1); // Dynamic packet: header + list

	addHookPre(clif, emotion, clif_emotion_pre);
	addHookPre(clif, pEmotion, clif_parse_Emotion_pre);
	addHookPre(clif, pLoadEndAck, clif_parse_LoadEndAck_pre);

	script->set_constant("ET_SURPRISE", ET_SURPRISE, false, false);
	script->set_constant("ET_QUESTION", ET_QUESTION, false, false);
	script->set_constant("ET_DELIGHT", ET_DELIGHT, false, false);
	script->set_constant("ET_THROB", ET_THROB, false, false);
	script->set_constant("ET_SWEAT", ET_SWEAT, false, false);
	script->set_constant("ET_AHA", ET_AHA, false, false);
	script->set_constant("ET_FRET", ET_FRET, false, false);
	script->set_constant("ET_ANGER", ET_ANGER, false, false);
	script->set_constant("ET_MONEY", ET_MONEY, false, false);
	script->set_constant("ET_THINK", ET_THINK, false, false);
	script->set_constant("ET_SCISSOR", ET_SCISSOR, false, false);
	script->set_constant("ET_ROCK", ET_ROCK, false, false);
	script->set_constant("ET_WRAP", ET_WRAP, false, false);
	script->set_constant("ET_FLAG", ET_FLAG, false, false);
	script->set_constant("ET_BIGTHROB", ET_BIGTHROB, false, false);
	script->set_constant("ET_THANKS", ET_THANKS, false, false);
	script->set_constant("ET_KEK", ET_KEK, false, false);
	script->set_constant("ET_SORRY", ET_SORRY, false, false);
	script->set_constant("ET_SMILE", ET_SMILE, false, false);
	script->set_constant("ET_PROFUSELY_SWEAT", ET_PROFUSELY_SWEAT, false, false);
	script->set_constant("ET_SCRATCH", ET_SCRATCH, false, false);
	script->set_constant("ET_BEST", ET_BEST, false, false);
	script->set_constant("ET_STARE_ABOUT", ET_STARE_ABOUT, false, false);
	script->set_constant("ET_HUK", ET_HUK, false, false);
	script->set_constant("ET_O", ET_O, false, false);
	script->set_constant("ET_X", ET_X, false, false);
	script->set_constant("ET_HELP", ET_HELP, false, false);
	script->set_constant("ET_GO", ET_GO, false, false);
	script->set_constant("ET_CRY", ET_CRY, false, false);
	script->set_constant("ET_KIK", ET_KIK, false, false);
	script->set_constant("ET_CHUP", ET_CHUP, false, false);
	script->set_constant("ET_CHUPCHUP", ET_CHUPCHUP, false, false);
	script->set_constant("ET_HNG", ET_HNG, false, false);
	script->set_constant("ET_OK", ET_OK, false, false);
	script->set_constant("ET_CHAT_PROHIBIT", ET_CHAT_PROHIBIT, false, false);
	script->set_constant("ET_INDONESIA_FLAG", ET_INDONESIA_FLAG, false, false);
	script->set_constant("ET_STARE", ET_STARE, false, false);
	script->set_constant("ET_HUNGRY", ET_HUNGRY, false, false);
	script->set_constant("ET_COOL", ET_COOL, false, false);
	script->set_constant("ET_MERONG", ET_MERONG, false, false);
	script->set_constant("ET_SHY", ET_SHY, false, false);
	script->set_constant("ET_GOODBOY", ET_GOODBOY, false, false);
	script->set_constant("ET_SPTIME", ET_SPTIME, false, false);
	script->set_constant("ET_SEXY", ET_SEXY, false, false);
	script->set_constant("ET_COMEON", ET_COMEON, false, false);
	script->set_constant("ET_SLEEPY", ET_SLEEPY, false, false);
	script->set_constant("ET_CONGRATULATION", ET_CONGRATULATION, false, false);
	script->set_constant("ET_HPTIME", ET_HPTIME, false, false);
	script->set_constant("ET_PH_FLAG", ET_PH_FLAG, false, false);
	script->set_constant("ET_MY_FLAG", ET_MY_FLAG, false, false);
	script->set_constant("ET_SI_FLAG", ET_SI_FLAG, false, false);
	script->set_constant("ET_BR_FLAG", ET_BR_FLAG, false, false);
	script->set_constant("ET_SPARK", ET_SPARK, false, false);
	script->set_constant("ET_CONFUSE", ET_CONFUSE, false, false);
	script->set_constant("ET_OHNO", ET_OHNO, false, false);
	script->set_constant("ET_HUM", ET_HUM, false, false);
	script->set_constant("ET_BLABLA", ET_BLABLA, false, false);
	script->set_constant("ET_OTL", ET_OTL, false, false);
	script->set_constant("ET_DICE1", ET_DICE1, false, false);
	script->set_constant("ET_DICE2", ET_DICE2, false, false);
	script->set_constant("ET_DICE3", ET_DICE3, false, false);
	script->set_constant("ET_DICE4", ET_DICE4, false, false);
	script->set_constant("ET_DICE5", ET_DICE5, false, false);
	script->set_constant("ET_DICE6", ET_DICE6, false, false);
	script->set_constant("ET_INDIA_FLAG", ET_INDIA_FLAG, false, false);
	script->set_constant("ET_LUV", ET_LUV, false, false);
	script->set_constant("ET_FLAG8", ET_FLAG8, false, false);
	script->set_constant("ET_FLAG9", ET_FLAG9, false, false);
	script->set_constant("ET_MOBILE", ET_MOBILE, false, false);
	script->set_constant("ET_MAIL", ET_MAIL, false, false);
	script->set_constant("ET_ANTENNA0", ET_ANTENNA0, false, false);
	script->set_constant("ET_ANTENNA1", ET_ANTENNA1, false, false);
	script->set_constant("ET_ANTENNA2", ET_ANTENNA2, false, false);
	script->set_constant("ET_ANTENNA3", ET_ANTENNA3, false, false);
	script->set_constant("ET_HUM2", ET_HUM2, false, false);
	script->set_constant("ET_ABS", ET_ABS, false, false);
	script->set_constant("ET_OOPS", ET_OOPS, false, false);
	script->set_constant("ET_SPIT", ET_SPIT, false, false);
	script->set_constant("ET_ENE", ET_ENE, false, false);
	script->set_constant("ET_PANIC", ET_PANIC, false, false);
	script->set_constant("ET_WHISP", ET_WHISP, false, false);
	script->set_constant("ET_YUT1", ET_YUT1, false, false);
	script->set_constant("ET_YUT2", ET_YUT2, false, false);
	script->set_constant("ET_YUT3", ET_YUT3, false, false);
	script->set_constant("ET_YUT4", ET_YUT4, false, false);
	script->set_constant("ET_YUT5", ET_YUT5, false, false);
	script->set_constant("ET_YUT6", ET_YUT6, false, false);
	script->set_constant("ET_YUT7", ET_YUT7, false, false);
	script->set_constant("ET_CLICK_ME", ET_CLICK_ME, false, false);
	script->set_constant("ET_DAILY_QUEST", ET_DAILY_QUEST, false, false);
	script->set_constant("ET_EVENT", ET_EVENT, false, false);
	script->set_constant("ET_JOB_QUEST", ET_JOB_QUEST, false, false);
	script->set_constant("ET_TRAFFIC_LINE_QUEST", ET_TRAFFIC_LINE_QUEST, false, false);
	script->set_constant("ET_CUSTOM_1", ET_CUSTOM_1, false, false);
	script->set_constant("ET_CUSTOM_2", ET_CUSTOM_2, false, false);
	script->set_constant("ET_CUSTOM_3", ET_CUSTOM_3, false, false);
	script->set_constant("ET_CUSTOM_4", ET_CUSTOM_4, false, false);
	script->set_constant("ET_CUSTOM_5", ET_CUSTOM_5, false, false);
	script->set_constant("ET_CUSTOM_6", ET_CUSTOM_6, false, false);
	script->set_constant("ET_CUSTOM_7", ET_CUSTOM_7, false, false);
	script->set_constant("ET_CUSTOM_8", ET_CUSTOM_8, false, false);
	script->set_constant("ET_CUSTOM_9", ET_CUSTOM_9, false, false);
	script->set_constant("ET_CUSTOM_10", ET_CUSTOM_10, false, false);
	script->set_constant("ET_CUSTOM_11", ET_CUSTOM_11, false, false);
	script->set_constant("ET_CUSTOM_12", ET_CUSTOM_12, false, false);
	script->set_constant("ET_CUSTOM_13", ET_CUSTOM_13, false, false);
	script->set_constant("ET_CUSTOM_14", ET_CUSTOM_14, false, false);
	script->set_constant("ET_CUSTOM_15", ET_CUSTOM_15, false, false);

	emote_db_init();
}

HPExport void plugin_final(void)
{
	emote_db_final();
}
#else
HPExport void plugin_init(void)
{
	ShowWarning("ns_client_emote_ui_handler: This plugin requires PACKETVER >= 20230802. Plugin will not be loaded.\n");
}
#endif