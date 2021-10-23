#include "mod.h"
#include "stub/baseentity.h"
#include "stub/entities.h"
#include "stub/gamerules.h"
#include "stub/tfbot.h"
#include "stub/tf_shareddefs.h"
#include "stub/misc.h"
#include "stub/strings.h"
#include "stub/server.h"
#include "stub/objects.h"
#include "util/scope.h"
#include "util/iterate.h"
#include "util/misc.h"
#include <boost/algorithm/string.hpp>
#include <regex>
#include <string_view>
#include "stub/sendprop.h"

namespace Mod::Etc::Mapentity_Additions
{

    void FireCustomOutput(CBaseEntity *entity, const char *name, CBaseEntity *activator, CBaseEntity *caller, variant_t variant);
    static const char *SPELL_TYPE[] = {
        "Fireball",
        "Ball O' Bats",
        "Healing Aura",
        "Pumpkin MIRV",
        "Superjump",
        "Invisibility",
        "Teleport",
        "Tesla Bolt",
        "Minify",
        "Meteor Shower",
        "Summon Monoculus",
        "Summon Skeletons"
    };

    class CaseMenuHandler : public IMenuHandler
    {
    public:

        CaseMenuHandler(CTFPlayer * pPlayer, CLogicCase *pProvider) : IMenuHandler() {
            this->player = pPlayer;
            this->provider = pProvider;
        }

        void OnMenuSelect(IBaseMenu *menu, int client, unsigned int item) {

            if (provider == nullptr)
                return;
                
            const char *info = menu->GetItemInfo(item, nullptr);

            provider->FireCase(item + 1, player);
            variant_t variant;
            FireCustomOutput(provider, "onselect", player, provider, variant);
        }

        virtual void OnMenuCancel(IBaseMenu *menu, int client, MenuCancelReason reason)
		{
            if (provider == nullptr)
                return;

            variant_t variant;
            provider->m_OnDefault->FireOutput(variant, player, provider);
		}

        virtual void OnMenuEnd(IBaseMenu *menu, MenuEndReason reason)
		{
            menu->Destroy(false);
		}

        void OnMenuDestroy(IBaseMenu *menu) {
            DevMsg("Menu destroy\n");
            delete this;
        }

        CHandle<CTFPlayer> player;
        CHandle<CLogicCase> provider;
    };
    
    struct SendPropCacheEntry {
        ServerClass *serverClass;
        std::string name;
        int offset;
        SendProp *prop;
    };

    struct DatamapCacheEntry {
        datamap_t *datamap;
        std::string name;
        int offset;
        fieldtype_t fieldType;
        int size;
    };

    std::unordered_map<CBaseEntity *, std::unordered_map<std::string, CBaseEntityOutput>> custom_output;
    std::unordered_map<CBaseEntity *, std::unordered_map<std::string, string_t>> custom_variables;

    std::vector<SendPropCacheEntry> send_prop_cache;
    std::vector<DatamapCacheEntry> datamap_cache;

    bool FindSendProp(int& off, SendTable *s_table, const char *name, SendProp *&prop)
    {
        for (int i = 0; i < s_table->GetNumProps(); ++i) {
            SendProp *s_prop = s_table->GetProp(i);
            
            if (s_prop->GetName() != nullptr && strcmp(s_prop->GetName(), name) == 0) {
                off += s_prop->GetOffset();
                prop = s_prop;
                return true;
            }
            
            if (s_prop->GetDataTable() != nullptr) {
                off += s_prop->GetOffset();
                if (FindSendProp(off, s_prop->GetDataTable(), name, prop)) {
                    return true;
                }
                off -= s_prop->GetOffset();
            }
        }
        
        return false;
    }

    SendPropCacheEntry &GetSendPropOffset(ServerClass *serverClass, const char *name) {
        
        for (auto &entry : send_prop_cache) {
            if (entry.serverClass == serverClass && entry.name == name) {
                return entry;
            }
        }

        SendProp *prop = nullptr;
        int offset = 0;
        FindSendProp(offset,serverClass->m_pTable, name, prop);

        send_prop_cache.push_back({serverClass, name, offset, prop});
        return send_prop_cache.back();
    }

    DatamapCacheEntry &GetDataMapOffset(datamap_t *datamap, const char *name) {
        
        for (auto &entry : datamap_cache) {
            if (entry.datamap == datamap && entry.name == name) {
                return entry;
            }
        }
        for (datamap_t *dmap = datamap; dmap != NULL; dmap = dmap->baseMap) {
            // search through all the readable fields in the data description, looking for a match
            for (int i = 0; i < dmap->dataNumFields; i++) {
                if (strcmp(dmap->dataDesc[i].fieldName, name) == 0) {
                    datamap_cache.push_back({datamap, name, dmap->dataDesc[i].fieldOffset[ TD_OFFSET_NORMAL ], dmap->dataDesc[i].fieldType, dmap->dataDesc[i].fieldSize});
                    return datamap_cache.back();
                }
            }
        }

        datamap_cache.push_back({datamap, name, 0, FIELD_VOID, 0});
        return datamap_cache.back();
    }

    void ParseCustomOutput(CBaseEntity *entity, const char *name, const char *value) {
        std::string namestr = name;
        boost::algorithm::to_lower(namestr);
    //  DevMsg("Add custom output %d %s %s\n", entity, namestr.c_str(), value);
        custom_output[entity][namestr].ParseEventAction(value);
        custom_variables[entity][namestr] = AllocPooledString(value);
    }

    // Alert! Custom outputs must be defined in lowercase
    void FireCustomOutput(CBaseEntity *entity, const char *name, CBaseEntity *activator, CBaseEntity *caller, variant_t variant) {
        if (custom_output.empty())
            return;

        //DevMsg("Fire custom output %d %s %d\n", entity, name, custom_output.size());
        auto find = custom_output.find(entity);
        if (find != custom_output.end()) {
           // DevMsg("Found entity\n");
            auto findevent = find->second.find(name);
            if (findevent != find->second.end()) {
               // DevMsg("Found output\n");
                findevent->second.FireOutput(variant, activator, caller);
            }
        }
    }

    void FireFormatInput(CLogicCase *entity, CBaseEntity *activator, CBaseEntity *caller)
    {
        std::string fmtstr = STRING(entity->m_nCase[15]);
        unsigned int pos = 0;
        unsigned int index = 1;
        while ((pos = fmtstr.find('%', pos)) != std::string::npos ) {
            if (pos != fmtstr.size() - 1 && fmtstr[pos + 1] == '%') {
                fmtstr.erase(pos, 1);
                pos++;
            }
            else
            {
                string_t str = entity->m_nCase[index - 1];
                fmtstr.replace(pos, 1, STRING(str));
                index++;
                pos += strlen(STRING(str));
            }
        }

        variant_t variant1;
        variant1.SetString(AllocPooledString(fmtstr.c_str()));
        entity->m_OnDefault->FireOutput(variant1, activator, entity);
    }

    enum GetInputType {
        VARIABLE,
        KEYVALUE,
        DATAMAP,
        SENDPROP
    };

    bool GetEntityVariable(CBaseEntity *entity, GetInputType type, const char *name, variant_t &variable) {
        bool found = false;

        if (type == VARIABLE) {
            auto find = custom_variables.find(entity);
            if (find != custom_variables.end()) {
                auto find_var = find->second.find(name);
                if (find_var != find->second.end()) {
                    variable.SetString(find_var->second);
                    found = true;
                }
            }
        }
        else if (type == KEYVALUE) {
            found = entity->ReadKeyField(name, &variable);
        }
        else if (type == DATAMAP) {
            auto &entry = GetDataMapOffset(entity->GetDataDescMap(), name);
            if (entry.offset > 0) {
                if (entry.fieldType == FIELD_CHARACTER) {
                    variable.SetString(AllocPooledString(((char*)entity) + entry.offset));
                }
                else {
                    variable.Set(entry.fieldType, ((char*)entity) + entry.offset);
                }
                found = true;
            }
        }
        else if (type == SENDPROP) {
            auto &entry = GetSendPropOffset(entity->GetServerClass(), name);

            if (entry.offset > 0) {
                int offset = entry.offset;
                auto propType = entry.prop->GetType();
                if (propType == DPT_Int) {
                    variable.SetInt(*(int*)(((char*)entity) + offset));
                    if (entry.prop->m_nBits == 21 && strncmp(name, "m_h", 3)) {
                        variable.Set(FIELD_EHANDLE, (CHandle<CBaseEntity>*)(((char*)entity) + offset));
                    }
                }
                else if (propType == DPT_Float) {
                    variable.SetFloat(*(float*)(((char*)entity) + offset));
                }
                else if (propType == DPT_String) {
                    variable.SetString(*(string_t*)(((char*)entity) + offset));
                }
                else if (propType == DPT_Vector) {
                    variable.SetVector3D(*(Vector*)(((char*)entity) + offset));
                }
                found = true;
            }
        }
        return found;
    }

    void FireGetInput(CBaseEntity *entity, GetInputType type, const char *name, CBaseEntity *activator, CBaseEntity *caller, variant_t &value) {
        char param_tokenized[256] = "";
        V_strncpy(param_tokenized, value.String(), sizeof(param_tokenized));
        char *targetstr = strtok(param_tokenized,"|");
        char *action = strtok(NULL,"|");
        char *defvalue = strtok(NULL,"|");
        
        variant_t variable;

        if (targetstr != nullptr && action != nullptr) {

            bool found = GetEntityVariable(entity, type, name, variable);

            if (!found && defvalue != nullptr) {
                variable.SetString(AllocPooledString(defvalue));
            }

            if (found || defvalue != nullptr) {
                for (CBaseEntity *target = nullptr; (target = servertools->FindEntityGeneric(target, targetstr, entity, activator, caller)) != nullptr ;) {
                    target->AcceptInput(action, activator, entity, variable, 0);
                }
            }
        }
    }

    DETOUR_DECL_MEMBER(void, CTFPlayer_InputIgnitePlayer, inputdata_t &inputdata)
    {
        CTFPlayer *activator = inputdata.pActivator != nullptr && inputdata.pActivator->IsPlayer() ? ToTFPlayer(inputdata.pActivator) : reinterpret_cast<CTFPlayer *>(this);
        reinterpret_cast<CTFPlayer *>(this)->m_Shared->Burn(activator, nullptr, 10.0f);
    }

    const char *logic_case_classname;
    const char *tf_gamerules_classname;
    const char *player_classname;
    const char *point_viewcontrol_classname;

    bool allow_create_dropped_weapon = false;
	DETOUR_DECL_MEMBER(bool, CBaseEntity_AcceptInput, const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID)
    {
        CBaseEntity *ent = reinterpret_cast<CBaseEntity *>(this);
        if (szInputName[0] == '$') {
            if (ent->GetClassname() == logic_case_classname) {
                CLogicCase *logic_case = static_cast<CLogicCase *>(ent);
                if (strnicmp(szInputName, "$FormatInput", strlen("$FormatInput")) == 0) {
                    int num = strtol(szInputName + strlen("$FormatInput"), nullptr, 10);
                    if (num > 0 && num < 16) {
                        logic_case->m_nCase[num - 1] = AllocPooledString(Value.String());
                        FireFormatInput(logic_case, pActivator, pCaller);
                        
                        return true;
                    }
                }
                else if (strnicmp(szInputName, "$FormatInputNoFire", strlen("$FormatInputNoFire")) == 0) {
                    int num = strtol(szInputName + strlen("$FormatInputNoFire"), nullptr, 10);
                    if (num > 0 && num < 16) {
                        logic_case->m_nCase[num - 1] = AllocPooledString(Value.String());
                        return true;
                    }
                }
                else if (FStrEq(szInputName, "$FormatString")) {
                    logic_case->m_nCase[15] = AllocPooledString(Value.String());
                    FireFormatInput(logic_case, pActivator, pCaller);
                    return true;
                }
                else if (FStrEq(szInputName, "$FormatStringNoFire")) {
                    logic_case->m_nCase[15] = AllocPooledString(Value.String());
                    return true;
                }
                else if (FStrEq(szInputName, "$Format")) {
                    FireFormatInput(logic_case, pActivator, pCaller);
                    return true;
                }
                else if (FStrEq(szInputName, "$TestSigsegv")) {
                    ent->m_OnUser1->FireOutput(Value, pActivator, pCaller);
                    return true;
                }
                else if (FStrEq(szInputName, "$ToInt")) {
                    variant_t convert;
                    convert.SetInt(strtol(Value.String(), nullptr, 10));
                    logic_case->m_OnDefault->FireOutput(convert, pActivator, ent);
                    return true;
                }
                else if (FStrEq(szInputName, "$ToFloat")) {
                    variant_t convert;
                    convert.SetFloat(strtof(Value.String(), nullptr));
                    logic_case->m_OnDefault->FireOutput(convert, pActivator, ent);
                    return true;
                }
                else if (FStrEq(szInputName, "$CallerToActivator")) {
                    logic_case->m_OnDefault->FireOutput(Value, pCaller, ent);
                    return true;
                }
                else if (FStrEq(szInputName, "$GetKeyValueFromActivator")) {
                    variant_t variant;
                    pActivator->ReadKeyField(Value.String(), &variant);
                    logic_case->m_OnDefault->FireOutput(variant, pActivator, ent);
                    return true;
                }
                else if (FStrEq(szInputName, "$GetConVar")) {
                    ConVarRef cvref(Value.String());
                    if (cvref.IsValid() && cvref.IsFlagSet(FCVAR_REPLICATED) && !cvref.IsFlagSet(FCVAR_PROTECTED)) {
                        variant_t variant;
                        variant.SetFloat(cvref.GetFloat());
                        logic_case->m_OnDefault->FireOutput(variant, pActivator, ent);
                    }
                    return true;
                }
                else if (FStrEq(szInputName, "$GetConVarString")) {
                    ConVarRef cvref(Value.String());
                    if (cvref.IsValid() && cvref.IsFlagSet(FCVAR_REPLICATED) && !cvref.IsFlagSet(FCVAR_PROTECTED)) {
                        variant_t variant;
                        variant.SetString(AllocPooledString(cvref.GetString()));
                        logic_case->m_OnDefault->FireOutput(variant, pActivator, ent);
                    }
                    return true;
                }
                else if (FStrEq(szInputName, "$DisplayMenu")) {
                    
                    for (CBaseEntity *target = nullptr; (target = servertools->FindEntityGeneric(target, Value.String(), ent, pActivator, pCaller)) != nullptr ;) {
                        if (target != nullptr && target->IsPlayer() && !ToTFPlayer(target)->IsBot()) {
                            CaseMenuHandler *handler = new CaseMenuHandler(ToTFPlayer(target), logic_case);
                            IBaseMenu *menu = menus->GetDefaultStyle()->CreateMenu(handler);

                            int i;
                            for (i = 1; i < 16; i++) {
                                string_t str = logic_case->m_nCase[i - 1];
                                const char *name = STRING(str);
                                if (strlen(name) != 0) {
                                    bool enabled = name[0] != '!';
                                    ItemDrawInfo info1(enabled ? name : name + 1, enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
                                    menu->AppendItem("it", info1);
                                }
                                else {
                                    break;
                                }
                            }
                            if (i < 11) {
                                menu->SetPagination(MENU_NO_PAGINATION);
                            }

                            variant_t variant;
                            ent->ReadKeyField("Case16", &variant);
                            
                            char param_tokenized[256];
                            V_strncpy(param_tokenized, variant.String(), sizeof(param_tokenized));
                            
                            char *name = strtok(param_tokenized,"|");
                            char *timeout = strtok(NULL,"|");

                            menu->SetDefaultTitle(name);

                            char *flag;
                            while ((flag = strtok(NULL,"|")) != nullptr) {
                                if (FStrEq(flag, "Cancel")) {
                                    menu->SetMenuOptionFlags(menu->GetMenuOptionFlags() | MENUFLAG_BUTTON_EXIT);
                                }
                            }

                            menu->Display(ENTINDEX(target), timeout == nullptr ? 0 : atoi(timeout));
                        }
                    }
                    return true;
                }
                else if (FStrEq(szInputName, "$HideMenu")) {
                    auto target = servertools->FindEntityByName(nullptr, Value.String(), ent, pActivator, pCaller);
                    if (target != nullptr && target->IsPlayer()) {
                        menus->GetDefaultStyle()->CancelClientMenu(ENTINDEX(target), false);
                    }
                }
                else if (FStrEq(szInputName, "$BitTest")) {
                    int val = atoi(Value.String());
                    for (int i = 1; i <= 16; i++) {
                        string_t str = logic_case->m_nCase[i - 1];

                        if (val & atoi(STRING(str))) {
                            logic_case->FireCase(i, pActivator);
                        }
                        else {
                            break;
                        }
                    }
                }
                else if (FStrEq(szInputName, "$BitTestAll")) {
                    int val = atoi(Value.String());
                    for (int i = 1; i <= 16; i++) {
                       string_t str = logic_case->m_nCase[i - 1];

                        int test = atoi(STRING(str));
                        if ((val & test) == test) {
                            logic_case->FireCase(i, pActivator);
                        }
                        else {
                            break;
                        }
                    }
                }
            }
            else if (ent->GetClassname() == tf_gamerules_classname) {
                if (stricmp(szInputName, "$StopVO") == 0) {
                    TFGameRules()->BroadcastSound(SOUND_FROM_LOCAL_PLAYER, Value.String(), SND_STOP);
                    return true;
                }
                else if (stricmp(szInputName, "$StopVORed") == 0) {
                    TFGameRules()->BroadcastSound(TF_TEAM_RED, Value.String(), SND_STOP);
                    return true;
                }
                else if (stricmp(szInputName, "$StopVOBlue") == 0) {
                    TFGameRules()->BroadcastSound(TF_TEAM_BLUE, Value.String(), SND_STOP);
                    return true;
                }
                else if (stricmp(szInputName, "$SetBossHealthPercentage") == 0) {
                    float val = atof(Value.String());
                    if (g_pMonsterResource.GetRef() != nullptr)
                        g_pMonsterResource->m_iBossHealthPercentageByte = (int) (val * 255.0f);
                    return true;
                }
                else if (stricmp(szInputName, "$SetBossState") == 0) {
                    int val = atoi(Value.String());
                    if (g_pMonsterResource.GetRef() != nullptr)
                        g_pMonsterResource->m_iBossState = val;
                    return true;
                }
                else if (stricmp(szInputName, "$AddCurrencyGlobal") == 0) {
                    int val = atoi(Value.String());
                    TFGameRules()->DistributeCurrencyAmount(val, nullptr, true, true, false);
                    return true;
                }
            }
            else if (ent->GetClassname() == player_classname) {
                if (stricmp(szInputName, "$AllowClassAnimations") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    if (player != nullptr) {
                        player->GetPlayerClass()->m_bUseClassAnimations = Value.Bool();
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$SwitchClass") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    
                    if (player != nullptr) {
                        // Disable setup to allow class changing during waves in mvm
                        bool setup = TFGameRules()->InSetup();
                        TFGameRules()->SetInSetup(true);

                        int index = strtol(Value.String(), nullptr, 10);
                        if (index > 0 && index < 10) {
                            player->HandleCommand_JoinClass(g_aRawPlayerClassNames[index]);
                        }
                        else {
                            player->HandleCommand_JoinClass(Value.String());
                        }
                        
                        TFGameRules()->SetInSetup(setup);
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$SwitchClassInPlace") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    
                    if (player != nullptr) {
                        // Disable setup to allow class changing during waves in mvm
                        bool setup = TFGameRules()->InSetup();
                        TFGameRules()->SetInSetup(true);

                        Vector pos = player->GetAbsOrigin();
                        QAngle ang = player->GetAbsAngles();
                        Vector vel = player->GetAbsVelocity();

                        int index = strtol(Value.String(), nullptr, 10);
                        int oldState = player->m_Shared->GetState();
                        player->m_Shared->SetState(TF_STATE_DYING);
                        if (index > 0 && index < 10) {
                            player->HandleCommand_JoinClass(g_aRawPlayerClassNames[index]);
                        }
                        else {
                            player->HandleCommand_JoinClass(Value.String());
                        }
                        player->m_Shared->SetState(oldState);
                        TFGameRules()->SetInSetup(setup);
                        player->ForceRespawn();
                        player->Teleport(&pos, &ang, &vel);
                        
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$ForceRespawn") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    if (player != nullptr) {
                        if (player->GetTeamNumber() >= TF_TEAM_RED && player->GetPlayerClass() != nullptr && player->GetPlayerClass()->GetClassIndex() != TF_CLASS_UNDEFINED) {
                            player->ForceRespawn();
                        }
                        else {
                            player->m_bAllowInstantSpawn = true;
                        }
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$ForceRespawnDead") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    if (player != nullptr && !player->IsAlive()) {
                        if (player->GetTeamNumber() >= TF_TEAM_RED && player->GetPlayerClass() != nullptr && player->GetPlayerClass()->GetClassIndex() != TF_CLASS_UNDEFINED) {
                            player->ForceRespawn();
                        }
                        else {
                            player->m_bAllowInstantSpawn = true;
                        }
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$DisplayTextCenter") == 0) {
                    using namespace std::string_literals;
                    std::string text{Value.String()};
                    text = std::regex_replace(text, std::regex{"\\{newline\\}", std::regex_constants::icase}, "\n");
                    text = std::regex_replace(text, std::regex{"\\{player\\}", std::regex_constants::icase}, ToTFPlayer(ent)->GetPlayerName());
                    text = std::regex_replace(text, std::regex{"\\{activator\\}", std::regex_constants::icase}, 
                            (pActivator != nullptr && pActivator->IsPlayer() ? ToTFPlayer(pActivator) : ToTFPlayer(ent))->GetPlayerName());
                    gamehelpers->TextMsg(ENTINDEX(ent), TEXTMSG_DEST_CENTER, text.c_str());
                    return true;
                }
                else if (stricmp(szInputName, "$DisplayTextChat") == 0) {
                    using namespace std::string_literals;
                    std::string text{"\x01"s + Value.String() + "\x01"s};
                    text = std::regex_replace(text, std::regex{"\\{reset\\}", std::regex_constants::icase}, "\x01");
                    text = std::regex_replace(text, std::regex{"\\{blue\\}", std::regex_constants::icase}, "\x07" "99ccff");
                    text = std::regex_replace(text, std::regex{"\\{red\\}", std::regex_constants::icase}, "\x07" "ff3f3f");
                    text = std::regex_replace(text, std::regex{"\\{green\\}", std::regex_constants::icase}, "\x07" "99ff99");
                    text = std::regex_replace(text, std::regex{"\\{darkgreen\\}", std::regex_constants::icase}, "\x07" "40ff40");
                    text = std::regex_replace(text, std::regex{"\\{yellow\\}", std::regex_constants::icase}, "\x07" "ffb200");
                    text = std::regex_replace(text, std::regex{"\\{grey\\}", std::regex_constants::icase}, "\x07" "cccccc");
                    text = std::regex_replace(text, std::regex{"\\{newline\\}", std::regex_constants::icase}, "\n");
                    text = std::regex_replace(text, std::regex{"\\{player\\}", std::regex_constants::icase}, ToTFPlayer(ent)->GetPlayerName());
                    text = std::regex_replace(text, std::regex{"\\{activator\\}", std::regex_constants::icase}, 
                            (pActivator != nullptr && pActivator->IsPlayer() ? ToTFPlayer(pActivator) : ToTFPlayer(ent))->GetPlayerName());
                    auto pos{text.find("{")};
                    while(pos != std::string::npos){
                        if(text.substr(pos).length() > 7){
                            text[pos] = '\x07';
                            text.erase(pos+7, 1);
                            pos = text.find("{");
                        } else break; 
                    }
                    gamehelpers->TextMsg(ENTINDEX(ent), TEXTMSG_DEST_CHAT , text.c_str());
                    return true;
                }
                else if (stricmp(szInputName, "$Suicide") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    if (player != nullptr) {
                        player->CommitSuicide(false, true);
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$ChangeAttributes") == 0) {
                    CTFBot *bot = ToTFBot(ent);
                    if (bot != nullptr) {
                        auto *attrib = bot->GetEventChangeAttributes(Value.String());
                        if (attrib != nullptr){
                            bot->OnEventChangeAttributes(attrib);
                        }
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$RollCommonSpell") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    CBaseEntity *weapon = player->GetEntityForLoadoutSlot(LOADOUT_POSITION_ACTION);
                    
                    if (weapon == nullptr || !FStrEq(weapon->GetClassname(), "tf_weapon_spellbook")) return true;

                    CTFSpellBook *spellbook = rtti_cast<CTFSpellBook *>(weapon);
                    spellbook->RollNewSpell(0);

                    return true;
                }
                else if (stricmp(szInputName, "$SetSpell") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    CBaseEntity *weapon = player->GetEntityForLoadoutSlot(LOADOUT_POSITION_ACTION);
                    
                    if (weapon == nullptr || !FStrEq(weapon->GetClassname(), "tf_weapon_spellbook")) return true;
                    
                    const char *str = Value.String();
                    int index = strtol(str, nullptr, 10);
                    for (int i = 0; i < ARRAYSIZE(SPELL_TYPE); i++) {
                        if (FStrEq(str, SPELL_TYPE[i])) {
                            index = i;
                        }
                    }

                    CTFSpellBook *spellbook = rtti_cast<CTFSpellBook *>(weapon);
                    spellbook->m_iSelectedSpellIndex = index;
                    spellbook->m_iSpellCharges = 1;

                    return true;
                }
                else if (stricmp(szInputName, "$AddSpell") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    
                    CBaseEntity *weapon = player->GetEntityForLoadoutSlot(LOADOUT_POSITION_ACTION);
                    
                    if (weapon == nullptr || !FStrEq(weapon->GetClassname(), "tf_weapon_spellbook")) return true;
                    
                    const char *str = Value.String();
                    int index = strtol(str, nullptr, 10);
                    for (int i = 0; i < ARRAYSIZE(SPELL_TYPE); i++) {
                        if (FStrEq(str, SPELL_TYPE[i])) {
                            index = i;
                        }
                    }

                    CTFSpellBook *spellbook = rtti_cast<CTFSpellBook *>(weapon);
                    if (spellbook->m_iSelectedSpellIndex != index) {
                        spellbook->m_iSpellCharges = 0;
                    }
                    spellbook->m_iSelectedSpellIndex = index;
                    spellbook->m_iSpellCharges += 1;

                    return true;
                }
                else if (stricmp(szInputName, "$AddCond") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    int index = 0;
                    float duration = -1.0f;
                    sscanf(Value.String(), "%d %f", &index, &duration);
                    if (player != nullptr) {
                        player->m_Shared->AddCond((ETFCond)index, duration);
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$RemoveCond") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    int index = strtol(Value.String(), nullptr, 10);
                    if (player != nullptr) {
                        player->m_Shared->RemoveCond((ETFCond)index);
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$AddPlayerAttribute") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    char param_tokenized[256];
                    V_strncpy(param_tokenized, Value.String(), sizeof(param_tokenized));
                    
                    char *attr = strtok(param_tokenized,"|");
                    char *value = strtok(NULL,"|");

                    if (player != nullptr) {
                        player->AddCustomAttribute(attr, atof(value), -1.0f);
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$RemovePlayerAttribute") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    if (player != nullptr) {
                        player->RemoveCustomAttribute(Value.String());
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$AddItemAttribute") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    char param_tokenized[256];
                    V_strncpy(param_tokenized, Value.String(), sizeof(param_tokenized));
                    
                    char *attr = strtok(param_tokenized,"|");
                    char *value = strtok(NULL,"|");
                    char *slot = strtok(NULL,"|");

                    if (player != nullptr) {
                        CEconEntity *item = nullptr;
                        if (slot != nullptr) {
                            ForEachTFPlayerEconEntity(player, [&](CEconEntity *entity){
                                if (entity->GetItem() != nullptr && FStrEq(entity->GetItem()->GetItemDefinition()->GetName(), Value.String())) {
                                    item = entity;
                                }
                            });
                            if (item == nullptr)
                                item = GetEconEntityAtLoadoutSlot(player, atoi(slot));
                        }
                        else {
                            item = player->GetActiveTFWeapon();
                        }
                        if (item != nullptr) {
                            CEconItemAttributeDefinition *attr_def = GetItemSchema()->GetAttributeDefinitionByName(attr);
                            if (attr_def == nullptr) {
                                int idx = -1;
                                if (StringToIntStrict(attr, idx)) {
                                    attr_def = GetItemSchema()->GetAttributeDefinition(idx);
                                }
                            }
				            item->GetItem()->GetAttributeList().AddStringAttribute(attr_def, value);

                        }
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$RemoveItemAttribute") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    char param_tokenized[256];
                    V_strncpy(param_tokenized, Value.String(), sizeof(param_tokenized));
                    
                    char *attr = strtok(param_tokenized,"|");
                    char *slot = strtok(NULL,"|");

                    if (player != nullptr) {
                        CEconEntity *item = nullptr;
                        if (slot != nullptr) {
                            ForEachTFPlayerEconEntity(player, [&](CEconEntity *entity){
                                if (entity->GetItem() != nullptr && FStrEq(entity->GetItem()->GetItemDefinition()->GetName(), Value.String())) {
                                    item = entity;
                                }
                            });
                            if (item == nullptr)
                                item = GetEconEntityAtLoadoutSlot(player, atoi(slot));
                        }
                        else {
                            item = player->GetActiveTFWeapon();
                        }
                        if (item != nullptr) {
                            CEconItemAttributeDefinition *attr_def = GetItemSchema()->GetAttributeDefinitionByName(attr);
                            if (attr_def == nullptr) {
                                int idx = -1;
                                if (StringToIntStrict(attr, idx)) {
                                    attr_def = GetItemSchema()->GetAttributeDefinition(idx);
                                }
                            }
				            item->GetItem()->GetAttributeList().RemoveAttribute(attr_def);

                        }
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$PlaySoundToSelf") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    CRecipientFilter filter;
		            filter.AddRecipient(player);
                    
                    if (!enginesound->PrecacheSound(Value.String(), true))
						CBaseEntity::PrecacheScriptSound(Value.String());

                    EmitSound_t params;
                    params.m_pSoundName = Value.String();
                    params.m_flSoundTime = 0.0f;
                    params.m_pflSoundDuration = nullptr;
                    params.m_bWarnOnDirectWaveReference = true;
                    CBaseEntity::EmitSound(filter, ENTINDEX(player), params);
                    return true;
                }
                else if (stricmp(szInputName, "$IgnitePlayerDuration") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    CTFPlayer *activator = pActivator != nullptr && pActivator->IsPlayer() ? ToTFPlayer(pActivator) : player;
                    player->m_Shared->Burn(activator, nullptr, atof(Value.String()));
                    return true;
                }
                else if (stricmp(szInputName, "$WeaponSwitchSlot") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    player->Weapon_Switch(player->Weapon_GetSlot(atoi(Value.String())));
                    return true;
                }
                else if (stricmp(szInputName, "$WeaponStripSlot") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    int slot = atoi(Value.String());
                    CBaseCombatWeapon *weapon = player->GetActiveTFWeapon();
                    if (slot != -1) {
                        weapon = player->Weapon_GetSlot(slot);
                    }
                    if (weapon != nullptr)
                        weapon->Remove();
                    return true;
                }
                else if (stricmp(szInputName, "$RemoveItem") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    ForEachTFPlayerEconEntity(player, [&](CEconEntity *entity){
						if (entity->GetItem() != nullptr && FStrEq(entity->GetItem()->GetItemDefinition()->GetName(), Value.String())) {
							if (entity->MyCombatWeaponPointer() != nullptr) {
								player->Weapon_Detach(entity->MyCombatWeaponPointer());
							}
							entity->Remove();
						}
					});
                    return true;
                }
                else if (stricmp(szInputName, "$GiveItem") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    auto item_def = GetItemSchema()->GetItemDefinitionByName(Value.String());
                    if (item_def != nullptr) {
                        const char *classname = TranslateWeaponEntForClass_improved(item_def->GetItemClass(), player->GetPlayerClass()->GetClassIndex());
                        CEconEntity *entity = static_cast<CEconEntity *>(ItemGeneration()->SpawnItem(item_def->m_iItemDefIndex, player->WorldSpaceCenter(), vec3_angle, 1, 6, classname));
                        DispatchSpawn(entity);

                        if (entity != nullptr) {
                            GiveItemToPlayer(player, entity, false, true, Value.String());
                        }
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$DropItem") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    int slot = atoi(Value.String());
                    CBaseCombatWeapon *weapon = player->GetActiveTFWeapon();
                    if (slot != -1) {
                        weapon = player->Weapon_GetSlot(slot);
                    }

                    if (weapon != nullptr) {
                        CEconItemView *item_view = weapon->GetItem();

                        allow_create_dropped_weapon = true;
                        auto dropped = CTFDroppedWeapon::Create(player, player->EyePosition(), vec3_angle, weapon->GetWorldModel(), item_view);
                        if (dropped != nullptr)
                            dropped->InitDroppedWeapon(player, static_cast<CTFWeaponBase *>(weapon), false, false);

                        allow_create_dropped_weapon = false;
                    }
                }
                else if (stricmp(szInputName, "$SetCurrency") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    player->RemoveCurrency(player->GetCurrency() - atoi(Value.String()));
                }
                else if (stricmp(szInputName, "$AddCurrency") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    player->RemoveCurrency(atoi(Value.String()) * -1);
                }
                else if (stricmp(szInputName, "$RemoveCurrency") == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    player->RemoveCurrency(atoi(Value.String()));
                }
                else if (strnicmp(szInputName, "$CurrencyOutput", 15) == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    int cost = atoi(szInputName + 15);
                    if(player->GetCurrency() >= cost){
                        char param_tokenized[2048] = "";
                        V_strncpy(param_tokenized, Value.String(), sizeof(param_tokenized));
                        if(strcmp(param_tokenized, "") != 0){
                            char *target = strtok(param_tokenized,",");
                            char *action = NULL;
                            char *value = NULL;
                            if(target != NULL)
                                action = strtok(NULL,",");
                            if(action != NULL)
                                value = strtok(NULL,"");
                            if(value != NULL){
                                CEventQueue &que = g_EventQueue;
                                variant_t actualvalue;
                                string_t stringvalue = AllocPooledString(value);
                                actualvalue.SetString(stringvalue);
                                que.AddEvent(STRING(AllocPooledString(target)), STRING(AllocPooledString(action)), actualvalue, 0.0, player, player, -1);
                            }
                        }
                        player->RemoveCurrency(cost);
                    }
                    return true;
                }
                else if (strnicmp(szInputName, "$CurrencyInvertOutput", 21) == 0) {
                    CTFPlayer *player = ToTFPlayer(ent);
                    int cost = atoi(szInputName + 21);
                    if(player->GetCurrency() < cost){
                        char param_tokenized[2048] = "";
                        V_strncpy(param_tokenized, Value.String(), sizeof(param_tokenized));
                        if(strcmp(param_tokenized, "") != 0){
                            char *target = strtok(param_tokenized,",");
                            char *action = NULL;
                            char *value = NULL;
                            if(target != NULL)
                                action = strtok(NULL,",");
                            if(action != NULL)
                                value = strtok(NULL,"");
                            if(value != NULL){
                                CEventQueue &que = g_EventQueue;
                                variant_t actualvalue;
                                string_t stringvalue = AllocPooledString(value);
                                actualvalue.SetString(stringvalue);
                                que.AddEvent(STRING(AllocPooledString(target)), STRING(AllocPooledString(action)), actualvalue, 0.0, player, player, -1);
                            }
                        }
                        //player->RemoveCurrency(cost);
                    }
                    return true;
                }
                else if (stricmp(szInputName, "$RefillAmmo") == 0) {
                    CTFPlayer* player = ToTFPlayer(ent);
                    for(int i = 0; i < 7; ++i){
                        player->SetAmmoCount(player->GetMaxAmmo(i), i);
                    }
                    return true;
                }
                else if(stricmp(szInputName, "$Regenerate") == 0){
                    CTFPlayer* player = ToTFPlayer(ent);
                    player->Regenerate(true);
                    return true;
                }
            }
            else if (ent->GetClassname() == point_viewcontrol_classname) {
                auto camera = static_cast<CTriggerCamera *>(ent);
                if (stricmp(szInputName, "$EnableAll") == 0) {
                    ForEachTFPlayer([&](CTFPlayer *player) {
                        if (player->IsBot())
                            return;
                        else {

                            camera->m_hPlayer = player;
                            camera->Enable();
                            camera->m_spawnflags |= 512;
                        }
                    });
                }
                else if (stricmp(szInputName, "$DisableAll") == 0) {
                    ForEachTFPlayer([&](CTFPlayer *player) {
                        if (player->IsBot())
                            return;
                        else {
                            camera->m_hPlayer = player;
                            camera->Disable();
                            player->m_takedamage = player->IsObserver() ? 0 : 2;
                            camera->m_spawnflags &= ~(512);
                        }
                    });
                }
                else if (stricmp(szInputName, "$SetTarget") == 0) {
                    camera->m_hTarget = servertools->FindEntityByName(nullptr, Value.String(), ent, pActivator, pCaller);
                }
            }
            if (stricmp(szInputName, "$FireUserAsActivator1") == 0) {
                ent->m_OnUser1->FireOutput(Value, ent, ent);
                return true;
            }
            else if (stricmp(szInputName, "$FireUserAsActivator2") == 0) {
                ent->m_OnUser2->FireOutput(Value, ent, ent);
                return true;
            }
            else if (stricmp(szInputName, "$FireUserAsActivator3") == 0) {
                ent->m_OnUser3->FireOutput(Value, ent, ent);
                return true;
            }
            else if (stricmp(szInputName, "$FireUserAsActivator4") == 0) {
                ent->m_OnUser4->FireOutput(Value, ent, ent);
                return true;
            }
            else if (stricmp(szInputName, "$FireUser5") == 0) {
                FireCustomOutput(ent, "onuser5", pActivator, ent, Value);
                return true;
            }
            else if (stricmp(szInputName, "$FireUser6") == 0) {
                FireCustomOutput(ent, "onuser6", pActivator, ent, Value);
                return true;
            }
            else if (stricmp(szInputName, "$FireUser7") == 0) {
                FireCustomOutput(ent, "onuser7", pActivator, ent, Value);
                return true;
            }
            else if (stricmp(szInputName, "$FireUser8") == 0) {
                FireCustomOutput(ent, "onuser8", pActivator, ent, Value);
                return true;
            }
            else if (stricmp(szInputName, "$TakeDamage") == 0) {
                int damage = strtol(Value.String(), nullptr, 10);
                CBaseEntity *attacker = ent;
                
                CTakeDamageInfo info(attacker, attacker, nullptr, vec3_origin, ent->GetAbsOrigin(), damage, DMG_PREVENT_PHYSICS_FORCE, 0 );
                ent->TakeDamage(info);
                return true;
            }
            else if (stricmp(szInputName, "$TakeDamageFromActivator") == 0) {
                int damage = strtol(Value.String(), nullptr, 10);
                CBaseEntity *attacker = pActivator;
                
                CTakeDamageInfo info(attacker, attacker, nullptr, vec3_origin, ent->GetAbsOrigin(), damage, DMG_PREVENT_PHYSICS_FORCE, 0 );
                ent->TakeDamage(info);
                return true;
            }
            else if (stricmp(szInputName, "$SetModelOverride") == 0) {
                int replace_model = CBaseEntity::PrecacheModel(Value.String());
                if (replace_model != -1) {
                    for (int i = 0; i < MAX_VISION_MODES; ++i) {
                        ent->SetModelIndexOverride(i, replace_model);
                    }
                }
                return true;
            }
            else if (stricmp(szInputName, "$SetModel") == 0) {
                CBaseEntity::PrecacheModel(Value.String());
                ent->SetModel(Value.String());
                return true;
            }
            else if (stricmp(szInputName, "$SetModelSpecial") == 0) {
                int replace_model = CBaseEntity::PrecacheModel(Value.String());
                if (replace_model != -1) {
                    ent->SetModelIndex(replace_model);
                }
                return true;
            }
            else if (stricmp(szInputName, "$SetOwner") == 0) {
                auto owner = servertools->FindEntityByName(nullptr, Value.String(), ent, pActivator, pCaller);
                if (owner != nullptr) {
                    ent->SetOwnerEntity(owner);
                }
                return true;
            }
            else if (stricmp(szInputName, "$GetKeyValue") == 0) {
                variant_t variant;
                ent->ReadKeyField(Value.String(), &variant);
                ent->m_OnUser1->FireOutput(variant, pActivator, ent);
                return true;
            }
            else if (stricmp(szInputName, "$InheritOwner") == 0) {
                auto owner = servertools->FindEntityByName(nullptr, Value.String(), ent, pActivator, pCaller);
                if (owner != nullptr) {
                    ent->SetOwnerEntity(owner->GetOwnerEntity());
                }
                return true;
            }
            else if (stricmp(szInputName, "$InheritParent") == 0) {
                auto owner = servertools->FindEntityByName(nullptr, Value.String(), ent, pActivator, pCaller);
                if (owner != nullptr) {
                    ent->SetParent(owner->GetMoveParent(), -1);
                }
                return true;
            }
            else if (stricmp(szInputName, "$MoveType") == 0) {
                variant_t variant;
                int val1=0;
                int val2=MOVECOLLIDE_DEFAULT;

                sscanf(Value.String(), "%d,%d", &val1, &val2);
                ent->SetMoveType((MoveType_t)val1, (MoveCollide_t)val2);
                return true;
            }
            else if (stricmp(szInputName, "$PlaySound") == 0) {
                
                if (!enginesound->PrecacheSound(Value.String(), true))
                    CBaseEntity::PrecacheScriptSound(Value.String());

                ent->EmitSound(Value.String());
                return true;
            }
            else if (stricmp(szInputName, "$StopSound") == 0) {
                ent->StopSound(Value.String());
                return true;
            }
            else if (stricmp(szInputName, "$SetLocalOrigin") == 0) {
                Vector vec;
                sscanf(Value.String(), "%f %f %f", &vec.x, &vec.y, &vec.z);
                ent->SetLocalOrigin(vec);
                return true;
            }
            else if (stricmp(szInputName, "$SetLocalAngles") == 0) {
                QAngle vec;
                sscanf(Value.String(), "%f %f %f", &vec.x, &vec.y, &vec.z);
                ent->SetLocalAngles(vec);
                return true;
            }
            else if (stricmp(szInputName, "$SetLocalVelocity") == 0) {
                Vector vec;
                sscanf(Value.String(), "%f %f %f", &vec.x, &vec.y, &vec.z);
                ent->SetLocalVelocity(vec);
                return true;
            }
            else if (stricmp(szInputName, "$TeleportToEntity") == 0) {
                auto target = servertools->FindEntityByName(nullptr, Value.String(), ent, pActivator, pCaller);
                if (target != nullptr) {
                    Vector targetpos = target->GetAbsOrigin();
                    ent->Teleport(&targetpos, nullptr, nullptr);
                }
                return true;
            }
            else if (stricmp(szInputName, "$MoveRelative") == 0) {
                Vector vec;
                sscanf(Value.String(), "%f %f %f", &vec.x, &vec.y, &vec.z);
                vec = vec + ent->GetLocalOrigin();
                ent->SetLocalOrigin(vec);
                return true;
            }
            else if (stricmp(szInputName, "$RotateRelative") == 0) {
                QAngle vec;
                sscanf(Value.String(), "%f %f %f", &vec.x, &vec.y, &vec.z);
                vec = vec + ent->GetLocalAngles();
                ent->SetLocalAngles(vec);
                return true;
            }
            else if (stricmp(szInputName, "$TestEntity") == 0) {
                for (CBaseEntity *target = nullptr; (target = servertools->FindEntityGeneric(target, Value.String(), ent, pActivator, pCaller)) != nullptr ;) {
                    auto filter = rtti_cast<CBaseFilter *>(ent);
                    if (filter != nullptr && filter->PassesFilter(pCaller, target)) {
                        filter->m_OnPass->FireOutput(Value, pActivator, target);
                    }
                }
                return true;
            }
            else if (stricmp(szInputName, "$StartTouchEntity") == 0) {
                for (CBaseEntity *target = nullptr; (target = servertools->FindEntityGeneric(target, Value.String(), ent, pActivator, pCaller)) != nullptr ;) {
                    auto filter = rtti_cast<CBaseTrigger *>(ent);
                    if (filter != nullptr) {
                        filter->StartTouch(target);
                    }
                }
                return true;
            }
            else if (stricmp(szInputName, "$EndTouchEntity") == 0) {
                for (CBaseEntity *target = nullptr; (target = servertools->FindEntityGeneric(target, Value.String(), ent, pActivator, pCaller)) != nullptr ;) {
                    auto filter = rtti_cast<CBaseTrigger *>(ent);
                    if (filter != nullptr) {
                        filter->EndTouch(target);
                    }
                }
                return true;
            }
            else if (strnicmp(szInputName, "$SetVar$", strlen("$SetVar$")) == 0) {
                custom_variables[ent][szInputName + strlen("$SetVar$")] = AllocPooledString(Value.String());
                return true;
            }
            else if (strnicmp(szInputName, "$GetVar$", strlen("$GetVar$")) == 0) {
                FireGetInput(ent, VARIABLE, szInputName + strlen("$GetVar$"), pActivator, pCaller, Value);
                return true;
            }
            else if (strnicmp(szInputName, "$SetKey$", strlen("$SetKey$")) == 0) {
                ent->KeyValue(szInputName + strlen("$SetKey$"), Value.String());
                return true;
            }
            else if (strnicmp(szInputName, "$GetKey$", strlen("$GetKey$")) == 0) {
                FireGetInput(ent, KEYVALUE, szInputName + strlen("$GetKey$"), pActivator, pCaller, Value);
                return true;
            }
            else if (strnicmp(szInputName, "$SetData$", strlen("$SetData$")) == 0) {
                const char *name = szInputName + strlen("$SetData$");
                auto &entry = GetDataMapOffset(ent->GetDataDescMap(), name);

                if (entry.offset > 0) {
                    if (entry.fieldType == FIELD_CHARACTER) {
                        V_strncpy(((char*)ent) + entry.offset, Value.String(), entry.size);
                    }
                    else {
                        Value.Convert(entry.fieldType);
                        Value.SetOther(((char*)ent) + entry.offset);
                    }
                }
                return true;
            }
            else if (strnicmp(szInputName, "$GetData$", strlen("$GetData$")) == 0) {
                FireGetInput(ent, DATAMAP, szInputName + strlen("$GetData$"), pActivator, pCaller, Value);
                return true;
            }
            else if (strnicmp(szInputName, "$SetProp$", strlen("$SetProp$")) == 0) {
                const char *name = szInputName + strlen("$SetProp$");

                auto &entry = GetSendPropOffset(ent->GetServerClass(), name);

                if (entry.offset > 0) {

                    int offset = entry.offset;
                    auto propType = entry.prop->GetType();
                    if (propType == DPT_Int) {
                        *(int*)(((char*)ent) + offset) = atoi(Value.String());
                        if (entry.prop->m_nBits == 21 && strncmp(name, "m_h", 3)) {
                            *(CHandle<CBaseEntity>*)(((char*)ent) + offset) = servertools->FindEntityByName(nullptr, Value.String());
                        }
                    }
                    else if (propType == DPT_Float) {
                        *(float*)(((char*)ent) + offset) = strtof(Value.String(), nullptr);
                    }
                    else if (propType == DPT_String) {
                        *(string_t*)(((char*)ent) + offset) = AllocPooledString(Value.String());
                    }
                    else if (propType == DPT_Vector) {
                        Vector tmpVec = vec3_origin;
                        if (sscanf(Value.String(), "[%f %f %f]", &tmpVec[0], &tmpVec[1], &tmpVec[2]) == 0)
                        {
                            sscanf(Value.String(), "%f %f %f", &tmpVec[0], &tmpVec[1], &tmpVec[2]);
                        }
                        *(Vector*)(((char*)ent) + offset) = tmpVec;
                    }
                }

                return true;
            }
            else if (strnicmp(szInputName, "$GetProp$", strlen("$GetProp$")) == 0) {
                FireGetInput(ent, SENDPROP, szInputName + strlen("$GetProp$"), pActivator, pCaller, Value);
                return true;
            }
            else if (stricmp(szInputName, "$GetEntIndex") == 0) {
                char param_tokenized[256] = "";
                V_strncpy(param_tokenized, Value.String(), sizeof(param_tokenized));
                char *targetstr = strtok(param_tokenized,"|");
                char *action = strtok(NULL,"|");
                
                variant_t variable;
                variable.SetInt(ent->entindex());
                if (targetstr != nullptr && action != nullptr) {
                    for (CBaseEntity *target = nullptr; (target = servertools->FindEntityGeneric(target, targetstr, ent, pActivator, pCaller)) != nullptr ;) {
                        target->AcceptInput(action, pActivator, ent, variable, 0);
                    }
                }
                return true;
            }
        }
        return DETOUR_MEMBER_CALL(CBaseEntity_AcceptInput)(szInputName, pActivator, pCaller, Value, outputID);
    }

    void ActivateLoadedInput()
    {
        DevMsg("ActivateLoadedInput\n");
        auto entity = servertools->FindEntityByName(nullptr, "sigsegv_load");
        
        if (entity != nullptr) {
            variant_t variant1;
            variant1.SetString(NULL_STRING);

            entity->AcceptInput("FireUser1", UTIL_EntityByIndex(0), UTIL_EntityByIndex(0) ,variant1,-1);
        }
    }

    DETOUR_DECL_MEMBER(void, CTFGameRules_CleanUpMap)
	{
		DETOUR_MEMBER_CALL(CTFGameRules_CleanUpMap)();
        ActivateLoadedInput();
	}

    CBaseEntity *DoSpecialParsing(const char *szName, CBaseEntity *pStartEntity, const std::function<CBaseEntity *(CBaseEntity *, const char *)>& functor)
    {
        if (szName[0] == '@' && szName[1] != '\0') {
            if (szName[2] == '@') {
                const char *realname = szName + 3;
                CBaseEntity *nextentity = pStartEntity;
                // Find parent of entity
                if (szName[1] == 'p') {
                    static CBaseEntity *last_parent = nullptr;
                    if (pStartEntity == nullptr)
                        last_parent = nullptr;

                    while (true) {
                        last_parent = functor(last_parent, realname); 
                        nextentity = last_parent;
                        if (nextentity != nullptr) {
                            if (nextentity->GetMoveParent() != nullptr) {
                                return nextentity->GetMoveParent();
                            }
                            else {
                                continue;
                            }
                        }
                        else {
                            return nullptr;
                        }
                    }
                }
                // Find children of entity
                else if (szName[1] == 'c') {
                    bool skipped = false;
                    while (true) {
                        if (pStartEntity != nullptr && !skipped ) {
                            if (pStartEntity->NextMovePeer() != nullptr) {
                                return pStartEntity->NextMovePeer();
                            }
                            else{
                                pStartEntity = pStartEntity->GetMoveParent();
                            }
                        }
                        pStartEntity = functor(pStartEntity, realname); 
                        if (pStartEntity == nullptr) {
                            return nullptr;
                        }
                        else {
                            if (pStartEntity->FirstMoveChild() != nullptr) {
                                return pStartEntity->FirstMoveChild();
                            }
                            else {
                                skipped = true;
                                continue;
                            }
                        }
                    }
                }
                // Find entity with filter
                else if (szName[1] == 'f') {
                    bool skipped = false;
                    const char *filtername = realname;
                    CBaseFilter *filter = rtti_cast<CBaseFilter *>(servertools->FindEntityByName(nullptr, filtername));
                    realname = strchr(filtername, '@');
                    if (realname != nullptr && filter != nullptr) {
                        realname += 1;
                        while (true) {
                            pStartEntity = functor(pStartEntity, realname);
                            if (pStartEntity == nullptr) return nullptr;

                            if (filter->PassesFilter(pStartEntity, pStartEntity)) return pStartEntity;
                        }
                    }
                }
            }
            else if (szName[1] == 'b' && szName[2] == 'b') {
                Vector min;
                Vector max;
                int scannum = sscanf(szName+3, "%f %f %f %f %f %f", &min.x, &min.y, &min.z, &max.x, &max.y, &max.z);
                if (scannum == 6) {
                    const char *realname = strchr(szName + 3, '@');
                    if (realname != nullptr) {
                        realname += 1;
                        while (true) {
                            pStartEntity = functor(pStartEntity, realname); 
                            if (pStartEntity != nullptr && !pStartEntity->GetAbsOrigin().WithinAABox((const Vector)min, (const Vector)max)) {
                                continue;
                            }
                            else {
                                return pStartEntity;
                            }
                        }
                    }
                }
            }
        }

        return functor(pStartEntity, szName);
    }

    DETOUR_DECL_MEMBER(CBaseEntity *, CGlobalEntityList_FindEntityByClassname, CBaseEntity *pStartEntity, const char *szName)
	{
        return DoSpecialParsing(szName, pStartEntity, [&](CBaseEntity *entity, const char *realname) {return DETOUR_MEMBER_CALL(CGlobalEntityList_FindEntityByClassname)(entity, realname);});

		//return DETOUR_MEMBER_CALL(CGlobalEntityList_FindEntityByClassname)(pStartEntity, szName);
    }

    DETOUR_DECL_MEMBER(CBaseEntity *, CGlobalEntityList_FindEntityByName, CBaseEntity *pStartEntity, const char *szName, CBaseEntity *pSearchingEntity, CBaseEntity *pActivator, CBaseEntity *pCaller, IEntityFindFilter *pFilter)
	{
        return DoSpecialParsing(szName, pStartEntity, [&](CBaseEntity *entity, const char *realname) {return DETOUR_MEMBER_CALL(CGlobalEntityList_FindEntityByName)(entity, realname, pSearchingEntity, pActivator, pCaller, pFilter);});
        
		//return DETOUR_MEMBER_CALL(CGlobalEntityList_FindEntityByName)(pStartEntity, szName, pSearchingEntity, pActivator, pCaller, pFilter);
	}

    DETOUR_DECL_MEMBER(void, CTFMedigunShield_RemoveShield)
	{
        CTFMedigunShield *shield = reinterpret_cast<CTFMedigunShield *>(this);
        int spawnflags = shield->m_spawnflags;
        //DevMsg("ShieldRemove %d f\n", spawnflags);
        
        if (spawnflags & 2) {
            DevMsg("Spawnflags is 3\n");
            shield->SetModel("models/props_mvm/mvm_player_shield2.mdl");
        }

        if (!(spawnflags & 1)) {
            //DevMsg("Spawnflags is 0\n");
        }
        else{
            //DevMsg("Spawnflags is not 0\n");
            shield->SetBlocksLOS(false);
            return;
        }

        
		DETOUR_MEMBER_CALL(CTFMedigunShield_RemoveShield)();
	}

    DETOUR_DECL_MEMBER(void, CTFMedigunShield_UpdateShieldPosition)
	{   
		DETOUR_MEMBER_CALL(CTFMedigunShield_UpdateShieldPosition)();
	}

    DETOUR_DECL_MEMBER(void, CTFMedigunShield_ShieldThink)
	{
        
		DETOUR_MEMBER_CALL(CTFMedigunShield_ShieldThink)();
	}
    
    RefCount rc_CTriggerHurt_HurtEntity;
    DETOUR_DECL_MEMBER(bool, CTriggerHurt_HurtEntity, CBaseEntity *other, float damage)
	{
        SCOPED_INCREMENT(rc_CTriggerHurt_HurtEntity);
		return DETOUR_MEMBER_CALL(CTriggerHurt_HurtEntity)(other, damage);
	}
    
    DETOUR_DECL_MEMBER(int, CBaseEntity_TakeDamage, CTakeDamageInfo &info)
	{
		//DevMsg("Take damage damage %f\n", info.GetDamage());
        if (rc_CTriggerHurt_HurtEntity) {
            auto owner = info.GetAttacker()->GetOwnerEntity();
            if (owner != nullptr && owner->IsPlayer()) {
                info.SetAttacker(owner);
                info.SetInflictor(owner);
            }
        }
        CBaseEntity *entity = reinterpret_cast<CBaseEntity *>(this);
        bool alive = entity->IsAlive();
		auto damage = DETOUR_MEMBER_CALL(CBaseEntity_TakeDamage)(info);
        if (damage != 0) {
            variant_t variant;
            variant.SetInt(damage);
            FireCustomOutput(entity, "ondamagereceived", info.GetAttacker() != nullptr ? info.GetAttacker() : entity, entity, variant);
        }
        else {
            variant_t variant;
            variant.SetInt(damage);
            FireCustomOutput(entity, "ondamageblocked", info.GetAttacker() != nullptr ? info.GetAttacker() : entity, entity, variant);
        }
        if (alive && !entity->IsAlive()) {
            variant_t variant;
            variant.SetInt(damage);
            FireCustomOutput(entity, "ondeath", info.GetAttacker() != nullptr ? info.GetAttacker() : entity, entity, variant);
        }
        return damage;
	}

    DETOUR_DECL_MEMBER(void, CBaseObject_InitializeMapPlacedObject)
	{
        DETOUR_MEMBER_CALL(CBaseObject_InitializeMapPlacedObject)();
    
        auto sentry = reinterpret_cast<CBaseObject *>(this);
        variant_t variant;
        sentry->ReadKeyField("spawnflags", &variant);
		int spawnflags = variant.Int();

        if (spawnflags & 64) {
			sentry->SetModelScale(0.75f);
			sentry->m_bMiniBuilding = true;
	        sentry->SetHealth(sentry->GetHealth() * 0.66f);
            sentry->SetMaxHealth(sentry->GetMaxHealth() * 0.66f);
            sentry->m_nSkin += 2;
            sentry->SetBodygroup( sentry->FindBodygroupByName( "mini_sentry_light" ), 1 );
		}
	}

    DETOUR_DECL_MEMBER(void, CBasePlayer_CommitSuicide, bool explode , bool force)
	{
        auto player = reinterpret_cast<CBasePlayer *>(this);
        // No commit suicide if the camera is active
        CBaseEntity *view = player->m_hViewEntity;
        if (rtti_cast<CTriggerCamera *>(view) != nullptr) {
            return;
        }
        DETOUR_MEMBER_CALL(CBasePlayer_CommitSuicide)(explode, force);
	}

	DETOUR_DECL_STATIC(CTFDroppedWeapon *, CTFDroppedWeapon_Create, const Vector& vecOrigin, const QAngle& vecAngles, CBaseEntity *pOwner, const char *pszModelName, const CEconItemView *pItemView)
	{
		// this is really ugly... we temporarily override m_bPlayingMannVsMachine
		// because the alternative would be to make a patch
		
		bool is_mvm_mode = TFGameRules()->IsMannVsMachineMode();

		if (allow_create_dropped_weapon) {
			TFGameRules()->Set_m_bPlayingMannVsMachine(false);
		}
		
		auto result = DETOUR_STATIC_CALL(CTFDroppedWeapon_Create)(vecOrigin, vecAngles, pOwner, pszModelName, pItemView);
		
		if (allow_create_dropped_weapon) {
			TFGameRules()->Set_m_bPlayingMannVsMachine(is_mvm_mode);
		}
		
		return result;
	}

    DETOUR_DECL_MEMBER(void, CBaseEntity_UpdateOnRemove)
	{
		auto entity = reinterpret_cast<CBaseEntity *>(this);

        auto name = STRING(entity->GetEntityName());
		if (name[0] == '!' && name[1] == '$') {
            variant_t variant;
            entity->m_OnUser4->FireOutput(variant, entity, entity);
        }
        
        if (!custom_output.empty()) {
            variant_t variant;
            variant.SetInt(entity->entindex());
            FireCustomOutput(entity, "onkilled", entity, entity, variant);
            custom_output.erase(entity);
        }

        if (!custom_variables.empty()) {
            custom_variables.erase(entity);
        }
        
		DETOUR_MEMBER_CALL(CBaseEntity_UpdateOnRemove)();
	}

    CBaseEntity *parse_ent = nullptr;
    DETOUR_DECL_STATIC(bool, ParseKeyvalue, void *pObject, typedescription_t *pFields, int iNumFields, const char *szKeyName, const char *szValue)
	{
		bool result = DETOUR_STATIC_CALL(ParseKeyvalue)(pObject, pFields, iNumFields, szKeyName, szValue);
        if (!result && szKeyName[0] == '$') {
            ParseCustomOutput(parse_ent, szKeyName + 1, szValue);
            result = true;
        }
        return result;
	}

    DETOUR_DECL_MEMBER(bool, CBaseEntity_KeyValue, const char *szKeyName, const char *szValue)
	{
        parse_ent = reinterpret_cast<CBaseEntity *>(this);
        return DETOUR_MEMBER_CALL(CBaseEntity_KeyValue)(szKeyName, szValue);
	}

    const char *filter_keyvalue_class;
    const char *filter_variable_class;
    const char *filter_datamap_class;
    const char *filter_sendprop_class;
    const char *filter_proximity_class;
    const char *filter_bbox_class;
    const char *empty;
    const char *less;
    const char *equal;
    const char *greater;
    const char *less_or_equal;
    const char *greater_or_equal;

    DETOUR_DECL_MEMBER(bool, CBaseFilter_PassesFilterImpl, CBaseEntity *pCaller, CBaseEntity *pEntity)
	{
        auto filter = reinterpret_cast<CBaseEntity *>(this);
        const char *classname = filter->GetClassname();
        if (classname[0] == '$') {
            if (classname == filter_variable_class || classname == filter_datamap_class || classname == filter_sendprop_class || classname == filter_keyvalue_class) {
                GetInputType type = KEYVALUE;

                if (classname == filter_variable_class) {
                    type = VARIABLE;
                } else if (classname == filter_datamap_class) {
                    type = DATAMAP;
                } else if (classname == filter_sendprop_class) {
                    type = SENDPROP;
                }

                auto &vars = custom_variables[filter];
                const char *name = STRING(vars["name"]);
                const char *valuecmp = STRING(vars["value"]);
                const char *compare = STRING(vars["compare"]);

                variant_t variable; 
                bool found = GetEntityVariable(pEntity, type, name, variable);

                if (found) {
                    if (compare == nullptr || compare == empty) {
                        const char *valuestring = variable.String();
                        return valuestring == valuecmp || strcmp(valuestring, valuecmp) == 0;
                    } 
                    else {
                        variable.Convert(FIELD_FLOAT);
                        float value = variable.Float();
                        float valuecmpconv = strtof(valuecmp, nullptr);
                        if (compare == equal) {
                            return value == valuecmpconv;
                        }
                        else if (compare == less) {
                            return value < valuecmpconv;
                        }
                        else if (compare == greater) {
                            return value > valuecmpconv;
                        }
                        else if (compare == less_or_equal) {
                            return value <= valuecmpconv;
                        }
                        else if (compare == greater_or_equal) {
                            return value >= valuecmpconv;
                        }
                    }
                }
                return false;
            }
            else if(classname == filter_proximity_class) {
                auto &vars = custom_variables[filter];
                const char *target = STRING(vars["target"]);
                float range = strtof(STRING(vars["range"]), nullptr);
                range *= range;
                Vector center;
                if (sscanf(target, "%f %f %f", &center.x, &center.y, &center.z) != 3) {
                    CBaseEntity *ent = servertools->FindEntityByName(nullptr, target);
                    if (ent == nullptr) return false;

                    center = ent->GetAbsOrigin();
                }

                return center.DistToSqr(pEntity->GetAbsOrigin()) <= range;
            }
            else if(classname == filter_bbox_class) {
                auto &vars = custom_variables[filter];
                const char *target = STRING(vars["target"]);
                float range = strtof(STRING(vars["range"]), nullptr);

                Vector min;
                Vector max;
                sscanf(STRING(vars["min"]), "%f %f %f", &min.x, &min.y, &min.z);
                sscanf(STRING(vars["max"]), "%f %f %f", &max.x, &max.y, &max.z);

                range *= range;
                Vector center;
                if (sscanf(target, "%f %f %f", &center.x, &center.y, &center.z) != 3) {
                    CBaseEntity *ent = servertools->FindEntityByName(nullptr, target);
                    if (ent == nullptr) return false;

                    center = ent->GetAbsOrigin();
                }

                return pEntity->GetAbsOrigin().WithinAABox(min + center, max + center);
            }
        }
        return DETOUR_MEMBER_CALL(CBaseFilter_PassesFilterImpl)(pCaller, pEntity);
	}

    void OnCameraRemoved(CTriggerCamera *camera)
    {
        if (camera->m_spawnflags & 512) {
            ForEachTFPlayer([&](CTFPlayer *player) {
                if (player->IsBot())
                    return;
                else {
                    camera->m_hPlayer = player;
                    camera->Disable();
                    player->m_takedamage = player->IsObserver() ? 0 : 2;
                }
            });
        }
    }

    DETOUR_DECL_MEMBER(void, CTriggerCamera_D0)
	{
        OnCameraRemoved(reinterpret_cast<CTriggerCamera *>(this));
    }

    DETOUR_DECL_MEMBER(void, CTriggerCamera_D2)
	{
        OnCameraRemoved(reinterpret_cast<CTriggerCamera *>(this));
    }

    class CMod : public IMod, IModCallbackListener
	{
	public:
		CMod() : IMod("Etc:Mapentity_Additions")
		{
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_InputIgnitePlayer, "CTFPlayer::InputIgnitePlayer");
			MOD_ADD_DETOUR_MEMBER(CBaseEntity_AcceptInput, "CBaseEntity::AcceptInput");
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_CleanUpMap, "CTFGameRules::CleanUpMap");
			MOD_ADD_DETOUR_MEMBER(CTFMedigunShield_RemoveShield, "CTFMedigunShield::RemoveShield");
			MOD_ADD_DETOUR_MEMBER(CTriggerHurt_HurtEntity, "CTriggerHurt::HurtEntity");
			MOD_ADD_DETOUR_MEMBER(CGlobalEntityList_FindEntityByName, "CGlobalEntityList::FindEntityByName");
			MOD_ADD_DETOUR_MEMBER(CGlobalEntityList_FindEntityByClassname, "CGlobalEntityList::FindEntityByClassname");
            MOD_ADD_DETOUR_MEMBER_PRIORITY(CBaseEntity_TakeDamage, "CBaseEntity::TakeDamage", HIGHEST);
            MOD_ADD_DETOUR_MEMBER(CBaseObject_InitializeMapPlacedObject, "CBaseObject::InitializeMapPlacedObject");
            MOD_ADD_DETOUR_MEMBER(CBasePlayer_CommitSuicide, "CBasePlayer::CommitSuicide");
			MOD_ADD_DETOUR_STATIC(CTFDroppedWeapon_Create, "CTFDroppedWeapon::Create");
            MOD_ADD_DETOUR_MEMBER(CBaseEntity_UpdateOnRemove, "CBaseEntity::UpdateOnRemove");
            MOD_ADD_DETOUR_STATIC(ParseKeyvalue, "ParseKeyvalue");
            MOD_ADD_DETOUR_MEMBER(CBaseEntity_KeyValue, "CBaseEntity::KeyValue");
            MOD_ADD_DETOUR_MEMBER(CBaseFilter_PassesFilterImpl, "CBaseFilter::PassesFilterImpl");

            // Fix camera despawn bug
            MOD_ADD_DETOUR_MEMBER(CTriggerCamera_D0, "~CTriggerCamera [D0]");
            MOD_ADD_DETOUR_MEMBER(CTriggerCamera_D2, "~CTriggerCamera [D2]");
            
    
		//	MOD_ADD_DETOUR_MEMBER(CTFMedigunShield_UpdateShieldPosition, "CTFMedigunShield::UpdateShieldPosition");
		//	MOD_ADD_DETOUR_MEMBER(CTFMedigunShield_ShieldThink, "CTFMedigunShield::ShieldThink");
		//	MOD_ADD_DETOUR_MEMBER(CBaseGrenade_SetDamage, "CBaseGrenade::SetDamage");
		}

        virtual void LoadStrings()
        {
            logic_case_classname = STRING(AllocPooledString("logic_case"));
            tf_gamerules_classname = STRING(AllocPooledString("tf_gamerules"));
            player_classname = STRING(AllocPooledString("player"));
            point_viewcontrol_classname = STRING(AllocPooledString("point_viewcontrol"));
            filter_keyvalue_class = STRING(AllocPooledString("$filter_keyvalue"));
            filter_variable_class = STRING(AllocPooledString("$filter_variable"));
            filter_datamap_class = STRING(AllocPooledString("$filter_datamap"));
            filter_sendprop_class = STRING(AllocPooledString("$filter_sendprop"));
            filter_proximity_class = STRING(AllocPooledString("$filter_proximity"));
            filter_bbox_class = STRING(AllocPooledString("$filter_bbox"));
            empty = STRING(AllocPooledString(""));
            less = STRING(AllocPooledString("less than"));
            equal = STRING(AllocPooledString("equal"));
            greater = STRING(AllocPooledString("greater than"));
            less_or_equal = STRING(AllocPooledString("less than or equal"));
            greater_or_equal = STRING(AllocPooledString("greater than or equal"));
        }

        virtual bool OnLoad() override
		{
            LoadStrings();
            ActivateLoadedInput();
            if (servertools->GetEntityFactoryDictionary()->FindFactory("$filter_keyvalue") == nullptr) {
                servertools->GetEntityFactoryDictionary()->InstallFactory(servertools->GetEntityFactoryDictionary()->FindFactory("filter_base"), "$filter_keyvalue");
                servertools->GetEntityFactoryDictionary()->InstallFactory(servertools->GetEntityFactoryDictionary()->FindFactory("filter_base"), "$filter_variable");
                servertools->GetEntityFactoryDictionary()->InstallFactory(servertools->GetEntityFactoryDictionary()->FindFactory("filter_base"), "$filter_datamap");
                servertools->GetEntityFactoryDictionary()->InstallFactory(servertools->GetEntityFactoryDictionary()->FindFactory("filter_base"), "$filter_sendprop");
                servertools->GetEntityFactoryDictionary()->InstallFactory(servertools->GetEntityFactoryDictionary()->FindFactory("filter_base"), "$filter_proximity");
                servertools->GetEntityFactoryDictionary()->InstallFactory(servertools->GetEntityFactoryDictionary()->FindFactory("filter_base"), "$filter_bbox");
            }

			return true;
		}
        virtual bool ShouldReceiveCallbacks() const override { return this->IsEnabled(); }

        virtual void LevelInitPreEntity() override
        {
            LoadStrings();

            send_prop_cache.clear();
            datamap_cache.clear();
        }
	};
	CMod s_Mod;
	
	
	ConVar cvar_enable("sig_etc_mapentity_additions", "0", FCVAR_NOTIFY,
		"Mod: tell maps that sigsegv extension is loaded",
		[](IConVar *pConVar, const char *pOldValue, float flOldValue){
			s_Mod.Toggle(static_cast<ConVar *>(pConVar)->GetBool());
		});
}